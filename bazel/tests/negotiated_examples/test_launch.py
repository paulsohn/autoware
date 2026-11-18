# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise original negotiated launch files and standalone example contracts."""
import os
import re
from pathlib import Path
import unittest

from ament_index_python.packages import get_package_share_directory
import launch
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
import launch_ros.actions
import launch_testing
import launch_testing.actions
import launch_testing.asserts
from lifecycle_msgs.msg import Transition
from lifecycle_msgs.srv import ChangeState, GetState
import pytest
import rclpy

CASE = os.environ['NEGOTIATED_CASE']


@pytest.mark.launch_test
def generate_test_description():
    if CASE.endswith('.py'):
        source = Path(get_package_share_directory('negotiated_examples')) / 'launch' / CASE
        actions = [IncludeLaunchDescription(PythonLaunchDescriptionSource(str(source)))]
    else:
        pairs = {
            'standalone': ('negotiated_pub_example1', 'negotiated_sub_example1'),
            'string_b': ('negotiated_pub_example1', 'negotiated_sub_example2'),
            'compatible_sub': ('negotiated_pub_example1', 'negotiated_sub_example3'),
            'compatible_pub': ('negotiated_pub_compatible_example1', 'negotiated_sub_example1'),
            'adapted': ('negotiated_pub_type_adapt_example1', 'negotiated_sub_type_adapt_example1'),
            'lifecycle': ('negotiated_lifecycle_pub', 'negotiated_sub_example1'),
        }
        actions = [launch_ros.actions.Node(package='negotiated_examples', executable=name,
                                          output='both') for name in pairs[CASE]]
    return launch.LaunchDescription(actions + [launch_testing.actions.ReadyToTest()])


class TestExamples(unittest.TestCase):
    def test_delivery(self, proc_output):
        if CASE == 'lifecycle':
            rclpy.init()
            node = rclpy.create_node('lifecycle_test_peer')
            try:
                client = node.create_client(ChangeState, '/negotiated_lifecycle_pub/change_state')
                self.assertTrue(client.wait_for_service(timeout_sec=20))
                for transition in [Transition.TRANSITION_CONFIGURE, Transition.TRANSITION_ACTIVATE]:
                    future = client.call_async(ChangeState.Request(
                        transition=Transition(id=transition)))
                    rclpy.spin_until_future_complete(node, future, timeout_sec=20)
                    self.assertTrue(future.done())
                    self.assertTrue(future.result().success)
                proc_output.assertWaitFor('user callback:', timeout=25, stream='stderr')
                future = client.call_async(ChangeState.Request(
                    transition=Transition(id=Transition.TRANSITION_DEACTIVATE)))
                rclpy.spin_until_future_complete(node, future, timeout_sec=20)
                self.assertTrue(future.done())
                self.assertTrue(future.result().success)
                state_client = node.create_client(GetState, '/negotiated_lifecycle_pub/get_state')
                self.assertTrue(state_client.wait_for_service(timeout_sec=10))
                future = state_client.call_async(GetState.Request())
                rclpy.spin_until_future_complete(node, future, timeout_sec=10)
                self.assertTrue(future.done())
                self.assertEqual(future.result().current_state.label, 'inactive')
            finally:
                node.destroy_node()
                rclpy.shutdown()
        else:
            subscriber = {
                'string_b': 'negotiated_sub_example2',
                'compatible_sub': 'negotiated_sub_example3',
                'adapted': 'negotiated_sub_type_adapt_example1',
            }.get(CASE, 'negotiated_sub_example1')
            proc_output.assertWaitFor(re.compile(
                r'\[(?:downstream\.)?' + subscriber + r'\][^\n]*user callback:'), timeout=30, stream='stderr')
            if CASE == 'string_b':
                proc_output.assertWaitFor('Hello Universe:', timeout=10, stream='stderr')
            if CASE == 'adapted':
                proc_output.assertWaitFor('String user callback:', timeout=10, stream='stderr')


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
