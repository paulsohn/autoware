# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Render the original model and observe the original robot state publisher."""

import os
from pathlib import Path
import time
import unittest
import xml.etree.ElementTree as ET

from ament_index_python.packages import get_package_share_directory
import launch
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import AnyLaunchDescriptionSource
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import pytest
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile
from std_msgs.msg import String
from tf2_msgs.msg import TFMessage


@pytest.mark.launch_test
def generate_test_description():
    source = Path(get_package_share_directory('autoware_core_vehicle')) / 'launch/autoware_core_vehicle.launch.xml'
    return launch.LaunchDescription([
        IncludeLaunchDescription(AnyLaunchDescriptionSource(str(source)), launch_arguments={
            'vehicle_model': os.environ.get('VEHICLE_MODEL', 'autoware_sample_vehicle'),
            'sensor_model': os.environ['SENSOR_MODEL'],
        }.items()),
        launch_testing.actions.ReadyToTest(),
    ])


class TestVehicleDescription(unittest.TestCase):
    def test_rendered_description_and_calibrated_transforms(self):
        rclpy.init()
        node = rclpy.create_node('vehicle_description_peer')
        descriptions = []
        transforms = {}
        qos = QoSProfile(depth=10, durability=DurabilityPolicy.TRANSIENT_LOCAL)
        node.create_subscription(String, '/robot_description', descriptions.append, qos)

        def receive(message):
            transforms.update({item.child_frame_id: item for item in message.transforms})

        node.create_subscription(TFMessage, '/tf_static', receive, qos)
        try:
            deadline = time.monotonic() + 30
            while (not descriptions or 'sensor_kit_base_link' not in transforms) and time.monotonic() < deadline:
                rclpy.spin_once(node, timeout_sec=0.1)
            self.assertTrue(descriptions, 'No rendered robot description')
            robot = ET.fromstring(descriptions[-1].data)
            self.assertEqual(robot.tag, 'robot')
            self.assertNotIn('xacro:', descriptions[-1].data)
            links = {item.attrib['name'] for item in robot.findall('link')}
            self.assertTrue({'base_link', 'sensor_kit_base_link', 'velodyne_top_base_link'} <= links)
            self.assertIn('sensor_kit_base_link', transforms)
            transform = transforms['sensor_kit_base_link']
            self.assertEqual(transform.header.frame_id, 'base_link')
            self.assertAlmostEqual(transform.transform.translation.x, 0.9)
            self.assertAlmostEqual(transform.transform.translation.y, 0.0)
            self.assertAlmostEqual(transform.transform.translation.z, 2.0)
            if os.environ['SENSOR_MODEL'] in ('autoware_sample_sensor_kit', 'sample_sensor_kit'):
                self.assertIn('velodyne_rear_base_link', links)
            # Every mesh URI must resolve to a declared runtime resource.
            for mesh in robot.iter('mesh'):
                uri = mesh.attrib['filename']
                self.assertTrue(uri.startswith('package://'), uri)
                package, relative = uri[len('package://'):].split('/', 1)
                self.assertTrue((Path(get_package_share_directory(package)) / relative).is_file(), uri)
        finally:
            node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
