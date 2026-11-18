# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run the original composed GPU image pipeline and verify the returned bytes."""
from pathlib import Path
import tempfile
import time
import unittest
from ament_index_python.packages import get_package_share_directory
import launch
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import pytest
import rclpy
from sensor_msgs.msg import Image

_DIRECTORY = None


@pytest.mark.launch_test
def generate_test_description():
    global _DIRECTORY
    _DIRECTORY = tempfile.TemporaryDirectory(prefix='cuda-blackboard-')
    image = Path(_DIRECTORY.name) / 'image.ppm'
    image.write_bytes(b'P6\n8 4\n255\n' + bytes([10, 20, 30]) * 32)
    source = Path(get_package_share_directory('cuda_blackboard')) / 'launch/example.launch.py'
    return launch.LaunchDescription([
        IncludeLaunchDescription(PythonLaunchDescriptionSource(str(source)),
                                 launch_arguments={'image_path': str(image)}.items()),
        launch_testing.actions.ReadyToTest(),
    ])


class TestImagePipeline(unittest.TestCase):
    def test_gpu_image_delivery(self):
        rclpy.init()
        node = rclpy.create_node('cuda_image_peer')
        received = []
        try:
            node.create_subscription(Image, '/output_image', received.append, 10)
            deadline = time.monotonic() + 30
            while not received and time.monotonic() < deadline:
                rclpy.spin_once(node, timeout_sec=0.1)
            self.assertTrue(received, 'original GPU component pipeline produced no image')
            msg = received[-1]
            self.assertEqual((msg.width, msg.height, msg.step), (8, 4, 24))
            self.assertEqual(msg.encoding, 'bgr8')
            self.assertEqual(msg.header.frame_id, 'camera_frame')
            self.assertGreater(msg.header.stamp.sec, 0)
            self.assertEqual(bytes(msg.data), bytes([30, 20, 10]) * 32)
        finally:
            node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        try:
            launch_testing.asserts.assertExitCodes(proc_info)
        finally:
            _DIRECTORY.cleanup()
