# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the original CPU occupancy launch with its actual pointcloud converter."""
from pathlib import Path
import struct
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from geometry_msgs.msg import TransformStamped
import launch
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
import launch_testing
import launch_testing.actions
import launch_testing.asserts
from nav_msgs.msg import OccupancyGrid
import pytest
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile, qos_profile_sensor_data
from sensor_msgs.msg import PointCloud2, PointField
from tf2_msgs.msg import TFMessage


@pytest.mark.launch_test
def generate_test_description():
    source = Path(get_package_share_directory('autoware_probabilistic_occupancy_grid_map'))
    return launch.LaunchDescription([
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(str(source / 'launch/laserscan_based_occupancy_grid_map.launch.py')),
            launch_arguments={
                'use_pointcloud_container': 'false',
                'input_obstacle_pointcloud': 'true',
                'input_obstacle_and_raw_pointcloud': 'false',
                'input/obstacle_pointcloud': '/bazel_occupancy/points',
                'output': '/bazel_occupancy/map',
                'individual_container_name': 'bazel_occupancy_container',
            }.items()),
        launch_testing.actions.ReadyToTest(),
    ])


class TestOccupancy(unittest.TestCase):
    def test_cloud_to_probability_grid(self):
        rclpy.init()
        node = rclpy.create_node('occupancy_integration_peer')
        try:
            maps = []
            subscription = node.create_subscription(OccupancyGrid, '/bazel_occupancy/map', maps.append, 1)
            publisher = node.create_publisher(PointCloud2, '/bazel_occupancy/points', qos_profile_sensor_data)
            static_tf = node.create_publisher(TFMessage, '/tf_static',
                                              QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
            transform = TransformStamped()
            transform.header.frame_id = 'map'
            transform.child_frame_id = 'base_link'
            transform.transform.rotation.w = 1.0
            transform.header.stamp = node.get_clock().now().to_msg()
            static_tf.publish(TFMessage(transforms=[transform]))

            message = PointCloud2(height=1, width=3, point_step=12, row_step=36, is_dense=True)
            message.header.frame_id = 'base_link'
            message.fields = [PointField(name=name, offset=offset, datatype=PointField.FLOAT32, count=1)
                              for name, offset in [('x', 0), ('y', 4), ('z', 8)]]
            message.data = list(struct.pack('<fffffffff', 2.0, -0.5, 0.1, 2.0, 0.0, 0.1, 2.0, 0.5, 0.1))
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                message.header.stamp = node.get_clock().now().to_msg()
                publisher.publish(message)
                rclpy.spin_once(node, timeout_sec=0.1)
                if maps and max(maps[-1].data) > 50:
                    break
            self.assertGreater(subscription.get_publisher_count(), 0)
            self.assertTrue(maps, 'The original occupancy launch produced no grid')
            result = maps[-1]
            self.assertEqual(result.header.frame_id, 'map')
            self.assertEqual((result.info.width, result.info.height), (300, 300))
            self.assertAlmostEqual(result.info.resolution, 0.5)
            self.assertEqual(len(result.data), 90000)
            self.assertTrue(all(-1 <= cell <= 100 for cell in result.data))
            self.assertTrue(any(0 <= cell < 50 for cell in result.data))
            x = int((2.0 - result.info.origin.position.x) / result.info.resolution)
            y = int((0.0 - result.info.origin.position.y) / result.info.resolution)
            neighborhood = [result.data[row * result.info.width + col]
                            for row in range(y - 1, y + 2) for col in range(x - 1, x + 2)
                            if 0 <= row < result.info.height and 0 <= col < result.info.width]
            self.assertTrue(neighborhood)
            self.assertGreater(max(neighborhood), 50)
        finally:
            node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_clean_exit(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
