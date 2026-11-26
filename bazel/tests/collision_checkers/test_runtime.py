# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Drive collision diagnostics through actual component executables and DDS."""
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_planning_msgs.msg import Trajectory, TrajectoryPoint
from autoware_perception_msgs.msg import DetectedObjects, DetectedObject, ObjectClassification, Shape, PredictedObjects, PredictedObject
from diagnostic_msgs.msg import DiagnosticArray
from geometry_msgs.msg import TransformStamped, AccelWithCovarianceStamped
from tier4_control_msgs.msg import IsStopped
from tier4_control_msgs.srv import SetStop
from nav_msgs.msg import Odometry
from sensor_msgs.msg import PointCloud2, PointField
from tf2_msgs.msg import TFMessage
import rclpy
from rclpy.qos import QoSProfile, DurabilityPolicy, qos_profile_sensor_data

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())
PACKAGE = sys.argv.pop(1)
SHORT = PACKAGE.removeprefix('autoware_')


class CollisionRuntime(unittest.TestCase):
    def test_clear_collision_recovery(self):
        rclpy.init()
        peer = rclpy.create_node('collision_peer')
        namespace = f'/bazel_collision_{os.getpid()}'
        predicted = SHORT == 'predicted_path_checker'
        input_prefix = namespace + ('/predicted_path_checker_node' if predicted else '')
        requests = []
        stopped = IsStopped()
        stop_pub = peer.create_publisher(IsStopped, '/control/vehicle_cmd_gate/is_stopped', QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))

        def set_stop(request, response):
            requests.append((request.stop, request.request_source))
            stopped.data = request.stop
            stopped.requested_sources = [request.request_source] if request.stop else []
            response.status.success = True
            return response

        stop_service = peer.create_service(SetStop, '/control/vehicle_cmd_gate/set_stop', set_stop) if predicted else None
        accel_pub = peer.create_publisher(AccelWithCovarianceStamped, input_prefix + '/input/current_accel', 1) if predicted else None
        levels = []
        observed = set()
        diagnostics = peer.create_subscription(DiagnosticArray, '/diagnostics',
            lambda msg: (observed.update((s.hardware_id, s.name, s.level) for s in msg.status), levels.extend(s.level for s in msg.status if s.hardware_id == SHORT)), 10)
        tf_pub = peer.create_publisher(TFMessage, '/tf_static', QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
        trajectory_pub = peer.create_publisher(Trajectory, input_prefix + '/input/predicted_trajectory', 1)
        reference_pub = peer.create_publisher(Trajectory, input_prefix + '/input/reference_trajectory', 1)
        odom_pub = peer.create_publisher(Odometry, input_prefix + '/input/odometry', 1)
        spheres = SHORT == 'spheric_collision_detector'
        obstacle_pub = peer.create_publisher(PredictedObjects if predicted else DetectedObjects if spheres else PointCloud2,
            input_prefix + ('/input/objects' if predicted else '/input/object_recognition' if spheres else '/input/obstacle_pointcloud'),
            1 if spheres else qos_profile_sensor_data)
        trajectory = Trajectory()
        trajectory.header.frame_id = 'map'
        for i in range(81):
            point = TrajectoryPoint()
            point.pose.position.x = i * 0.5
            point.pose.orientation.w = 1.0
            point.longitudinal_velocity_mps = 10.0
            trajectory.points.append(point)
        odom = Odometry()
        odom.pose.pose.orientation.w = 1.0
        odom.twist.twist.linear.x = 10.0
        transform = TransformStamped()
        transform.header.frame_id = 'map'
        transform.child_frame_id = 'base_link'
        transform.transform.rotation.w = 1.0
        coordinates = [8.0, 20.0, 0.0]

        def publish():
            stamp = peer.get_clock().now().to_msg()
            transform.header.stamp = stamp
            tf_pub.publish(TFMessage(transforms=[transform]))
            trajectory.header.stamp = stamp
            trajectory_pub.publish(trajectory)
            reference_pub.publish(trajectory)
            odom.header.stamp = stamp
            odom_pub.publish(odom)
            if predicted or spheres:
                msg = PredictedObjects() if predicted else DetectedObjects()
                obj = PredictedObject() if predicted else DetectedObject()
                obj.existence_probability = 1.0
                pose = (obj.kinematics.initial_pose_with_covariance if predicted else obj.kinematics.pose_with_covariance).pose
                obj.shape.type = Shape.BOUNDING_BOX
                obj.shape.dimensions.x = obj.shape.dimensions.y = obj.shape.dimensions.z = 2.0
                pose.position.x = coordinates[0]
                pose.position.y = coordinates[1]
                pose.position.z = coordinates[2]
                pose.orientation.w = 1.0
                obj.classification = [ObjectClassification(label=ObjectClassification.CAR, probability=1.0)]
                msg.objects = [obj]
            else:
                msg = PointCloud2(height=1, width=1, point_step=12, row_step=12, is_dense=True)
                msg.fields = [PointField(name=name, offset=i * 4, datatype=PointField.FLOAT32, count=1) for i, name in enumerate(['x', 'y', 'z'])]
                msg.data = struct.pack('<fff', *coordinates)
            msg.header.frame_id = 'map'
            msg.header.stamp = stamp
            obstacle_pub.publish(msg)
            if predicted:
                accel = AccelWithCovarianceStamped()
                accel.header.stamp = stamp
                accel_pub.publish(accel)
                stopped.stamp = stamp
                stop_pub.publish(stopped)

        args = [EXECUTABLE, '--ros-args', '-r', '__ns:=' + namespace]
        for package, config in [(PACKAGE, SHORT + '.param.yaml'), ('autoware_test_utils', 'test_vehicle_info.param.yaml')]:
            args += ['--params-file', str(Path(get_package_share_directory(package)) / 'config' / config)]
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def wait_level(level):
                    levels.clear()
                    deadline = time.monotonic() + 15
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        publish()
                        rclpy.spin_once(peer, timeout_sec=0.05)
                        if bytes([level]) in levels:
                            return
                    self.fail(f'Diagnostic level {level} absent; observed={observed}; nodes={peer.get_node_names_and_namespaces()}; topics={peer.get_topic_names_and_types()}: {output()}')

                def wait_request(stop):
                    deadline = time.monotonic() + 5
                    while time.monotonic() < deadline:
                        publish()
                        rclpy.spin_once(peer, timeout_sec=0.05)
                        if (stop, 'predicted_path_checker') in requests:
                            return
                    self.fail(f'Stop request {stop} absent: {requests}; {output()}')

                try:
                    wait_level(0)
                    coordinates[1] = 0.0
                    wait_level(2)
                    if predicted:
                        wait_request(True)
                    if spheres:
                        coordinates[2] = 10.0
                        wait_level(0)
                        coordinates[2] = 0.0
                        wait_level(2)
                    coordinates[1] = 20.0
                    wait_level(0)
                    if predicted:
                        wait_request(False)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Node shutdown timed out: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
