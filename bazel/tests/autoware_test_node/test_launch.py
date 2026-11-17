# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check the original standalone launch and the exported component together."""
from pathlib import Path
import time
import unittest

from ament_index_python.packages import get_package_share_directory
import launch
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import AnyLaunchDescriptionSource
from launch_ros.actions import ComposableNodeContainer
from launch_ros.descriptions import ComposableNode
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import pytest
from rcl_interfaces.srv import GetParameters
import rclpy


@pytest.mark.launch_test
def generate_test_description():
    source = Path(get_package_share_directory('autoware_test_node')) / 'launch/autoware_test_node.launch.xml'
    return launch.LaunchDescription([
        IncludeLaunchDescription(AnyLaunchDescriptionSource(str(source))),
        ComposableNodeContainer(
            name='test_component_container', namespace='component_check',
            package='rclcpp_components', executable='component_container',
            composable_node_descriptions=[ComposableNode(
                package='autoware_test_node', plugin='autoware::test_node::TestNode',
                name='loaded_node', namespace='component_check')]),
        launch_testing.actions.ReadyToTest(),
    ])


class TestNodes(unittest.TestCase):
    def test_standalone_and_component_parameter_services(self):
        rclpy.init()
        node = rclpy.create_node('test_node_peer')
        try:
            for name in ['/test_ns1/test_node1', '/component_check/loaded_node']:
                client = node.create_client(GetParameters, name + '/get_parameters')
                self.assertTrue(client.wait_for_service(timeout_sec=20), name)
                future = client.call_async(GetParameters.Request(names=['use_sim_time']))
                deadline = time.monotonic() + 20
                while not future.done() and time.monotonic() < deadline:
                    rclpy.spin_once(node, timeout_sec=0.05)
                self.assertTrue(future.done(), name)
                self.assertFalse(future.result().values[0].bool_value)
        finally:
            node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
