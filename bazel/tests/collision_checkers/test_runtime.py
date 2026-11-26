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
from autoware_perception_msgs.msg import DetectedObjects, DetectedObject, ObjectClassification, Shape
from diagnostic_msgs.msg import DiagnosticArray
from geometry_msgs.msg import TransformStamped
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
        levels = []
        observed = set()
        diagnostics = peer.create_subscription(DiagnosticArray, '/diagnostics',
            lambda msg: (observed.update((s.hardware_id, s.name, s.level) for s in msg.status), levels.extend(s.level for s in msg.status if s.hardware_id == SHORT)), 10)
        tf_pub = peer.create_publisher(TFMessage, '/tf_static', QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
        trajectory_pub = peer.create_publisher(Trajectory, namespace + '/input/predicted_trajectory', 1)
        reference_pub = peer.create_publisher(Trajectory, namespace + '/input/reference_trajectory', 1)
        odom_pub = peer.create_publisher(Odometry, namespace + '/input/odometry', 1)
        spheres = SHORT == 'spheric_collision_detector'
        obstacle_pub = peer.create_publisher(DetectedObjects if spheres else PointCloud2,
            namespace + ('/input/object_recognition' if spheres else '/input/obstacle_pointcloud'),
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
            if spheres:
                msg = DetectedObjects()
                obj = DetectedObject()
                obj.existence_probability = 1.0
                obj.shape.type = Shape.BOUNDING_BOX
                obj.shape.dimensions.x = obj.shape.dimensions.y = obj.shape.dimensions.z = 2.0
                obj.kinematics.pose_with_covariance.pose.position.x = coordinates[0]
                obj.kinematics.pose_with_covariance.pose.position.y = coordinates[1]
                obj.kinematics.pose_with_covariance.pose.position.z = coordinates[2]
                obj.kinematics.pose_with_covariance.pose.orientation.w = 1.0
                obj.classification = [ObjectClassification(label=ObjectClassification.CAR, probability=1.0)]
                msg.objects = [obj]
            else:
                msg = PointCloud2(height=1, width=1, point_step=12, row_step=12, is_dense=True)
                msg.fields = [PointField(name=name, offset=i * 4, datatype=PointField.FLOAT32, count=1) for i, name in enumerate(['x', 'y', 'z'])]
                msg.data = struct.pack('<fff', *coordinates)
            msg.header.frame_id = 'map'
            msg.header.stamp = stamp
            obstacle_pub.publish(msg)

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

                try:
                    wait_level(0)
                    coordinates[1] = 0.0
                    wait_level(2)
                    if spheres:
                        coordinates[2] = 10.0
                        wait_level(0)
                        coordinates[2] = 0.0
                        wait_level(2)
                    coordinates[1] = 20.0
                    wait_level(0)
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
