# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Drive the actual NDT executable through map loading and initial alignment."""

import math
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest

from autoware_internal_localization_msgs.srv import PoseWithCovarianceStamped as Align
from autoware_map_msgs.msg import PointCloudMapCellWithID
from autoware_map_msgs.srv import GetDifferentialPointCloudMap
from geometry_msgs.msg import PoseWithCovarianceStamped, TransformStamped
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile
from sensor_msgs.msg import PointCloud2, PointField
from std_srvs.srv import SetBool
from tf2_msgs.msg import TFMessage

EXECUTABLE, PARAMETERS = [str(Path(p).resolve()) for p in sys.argv[1:3]]
del sys.argv[1:3]


def cloud(step, offset, frame):
    points = []
    count = round(20 / step) + 1
    for i in range(count):
        for j in range(count):
            u, v = i * step, j * step
            points.extend([(u + offset, v + offset, 0.0),
                           (offset, u + offset, v), (u + offset, offset, v)])
    result = PointCloud2()
    result.header.frame_id = frame
    result.height, result.width = 1, len(points)
    result.fields = [PointField(name=name, offset=4 * i, datatype=PointField.FLOAT32, count=1)
                     for i, name in enumerate(['x', 'y', 'z'])]
    result.point_step, result.row_step = 12, 12 * len(points)
    result.is_dense = True
    result.data = b''.join(struct.pack('<fff', *p) for p in points)
    return result


class NdtRuntime(unittest.TestCase):
    def test_initial_alignment(self):
        os.environ['ROS_DOMAIN_ID'] = '98'
        environment = dict(os.environ, ENABLE_AGNOCAST=os.environ['AUTOWARE_TEST_AGNOCAST'])
        if environment['ENABLE_AGNOCAST'] == '1':
            environment['LD_PRELOAD'] = environment['AGNOCAST_TEST_HEAPHOOK']
        else:
            environment.pop('LD_PRELOAD', None)
        rclpy.init()
        node = rclpy.create_node('ndt_runtime_peer')
        requests = []
        map_cloud = cloud(0.2, 100.0, 'map')

        def load(request, response):
            requests.append(request)
            response.header.frame_id = 'map'
            if '0' not in request.cached_ids:
                cell = PointCloudMapCellWithID(cell_id='0', pointcloud=map_cloud)
                cell.metadata.min_x, cell.metadata.min_y = 100.0, 100.0
                cell.metadata.max_x, cell.metadata.max_y = 120.0, 120.0
                response.new_pointcloud_with_ids = [cell]
            return response

        node.create_service(GetDifferentialPointCloudMap, 'pcd_loader_service', load)
        activation = node.create_client(SetBool, 'trigger_node_srv')
        alignment = node.create_client(Align, 'ndt_align_srv')
        poses = node.create_publisher(PoseWithCovarianceStamped, 'ekf_pose_with_covariance', 10)
        scans = node.create_publisher(PointCloud2, 'points_raw', 10)
        tf = node.create_publisher(TFMessage, '/tf_static',
                                   QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
        transform = TransformStamped(child_frame_id='sensor_frame')
        transform.header.frame_id = 'base_link'
        transform.transform.rotation.w = 1.0
        sensor = cloud(1.0, 0.0, 'sensor_frame')
        pose = PoseWithCovarianceStamped()
        pose.header.frame_id = 'map'
        pose.pose.pose.position.x, pose.pose.pose.position.y = 100.0, 100.0
        pose.pose.pose.orientation.w = 1.0
        for index, value in [(0, 0.25), (7, 0.25), (14, 0.0025),
                             (21, 0.000685), (28, 0.000685), (35, 0.0685)]:
            pose.pose.covariance[index] = value
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(
                    [EXECUTABLE, '--ros-args', '--params-file', PARAMETERS,
                     '-p', 'ndt.num_threads:=1', '-p', 'initial_pose_estimation.particles_num:=30',
                     '-p', 'initial_pose_estimation.n_startup_trials:=30'],
                    env=environment, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def wait(predicate, seconds=30, publish=False):
                    deadline = time.monotonic() + seconds
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if predicate():
                            return
                        if publish:
                            stamp = node.get_clock().now().to_msg()
                            pose.header.stamp = stamp
                            sensor.header.stamp = stamp
                            tf.publish(TFMessage(transforms=[transform]))
                            poses.publish(pose)
                            scans.publish(sensor)
                        rclpy.spin_once(node, timeout_sec=0.1)
                    self.fail('Timed out waiting for NDT: ' + output())

                try:
                    wait(lambda: activation.service_is_ready() and alignment.service_is_ready())
                    future = activation.call_async(SetBool.Request(data=True))
                    wait(future.done)
                    self.assertTrue(future.result().success)
                    wait(lambda: len(requests) > 0, publish=True)
                    # Give the input callback time to store a scan before aligning.
                    deadline = time.monotonic() + 2
                    while time.monotonic() < deadline:
                        tf.publish(TFMessage(transforms=[transform]))
                        sensor.header.stamp = node.get_clock().now().to_msg()
                        scans.publish(sensor)
                        rclpy.spin_once(node, timeout_sec=0.1)
                    future = alignment.call_async(Align.Request(pose_with_covariance=pose))
                    wait(future.done, seconds=90)
                    response = future.result()
                    self.assertTrue(response.success, output())
                    result = response.pose_with_covariance
                    self.assertEqual(result.header.frame_id, 'map')
                    for actual, expected in [(result.pose.pose.position.x, 100.0),
                                             (result.pose.pose.position.y, 100.0),
                                             (result.pose.pose.position.z, 0.0)]:
                        self.assertTrue(math.isfinite(actual))
                        self.assertAlmostEqual(actual, expected, delta=2.0)
                    self.assertGreater(requests[0].area.radius, 0.0)
                    future = activation.call_async(SetBool.Request(data=False))
                    wait(future.done)
                    self.assertTrue(future.result().success)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('NDT failed to stop: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
