# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run original standalone/composed topic and TF monitor launch files."""
import os
from pathlib import Path
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus
from geometry_msgs.msg import TransformStamped
import launch
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import AnyLaunchDescriptionSource
from launch_ros.actions import ComposableNodeContainer
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import pytest
from rcl_interfaces.msg import Parameter, ParameterType, ParameterValue
from rcl_interfaces.srv import SetParameters
import rclpy
from std_msgs.msg import String
from tf2_msgs.msg import TFMessage

MODE = os.environ['MONITOR_MODE']
TF = MODE.startswith('tf')


@pytest.mark.launch_test
def generate_test_description():
    composed = MODE.endswith('_composed')
    arguments = {'node_name_suffix': 'fixture', 'topic': '/tf' if TF else '/monitor_fixture',
                 'diag_name': 'fixture', 'warn_rate': '0.0', 'error_rate': '0.0', 'timeout': '0.4'}
    if TF:
        arguments.update(frame_id='map', child_frame_id='base_link')
    else:
        arguments['topic_type'] = 'std_msgs/msg/String'
    entries = []
    if composed:
        entries.append(ComposableNodeContainer(name='monitor_container', namespace='', package='rclcpp_components',
                                               executable='component_container', composable_node_descriptions=[]))
        arguments['target_container'] = '/monitor_container'
    filename = ('load_' if composed else '') + 'topic_state_monitor' + ('_tf' if TF else '') + '.launch.xml'
    source = Path(get_package_share_directory('autoware_topic_state_monitor')) / 'launch' / filename
    entries.append(IncludeLaunchDescription(AnyLaunchDescriptionSource(str(source)), launch_arguments=arguments.items()))
    return launch.LaunchDescription(entries + [launch_testing.actions.ReadyToTest()])


class Monitor(unittest.TestCase):
    def test_diagnostics(self):
        rclpy.init()
        peer = rclpy.create_node('topic_monitor_peer')
        statuses = []
        peer.create_subscription(DiagnosticArray, '/diagnostics',
                                 lambda msg: statuses.extend(s for s in msg.status if s.name.endswith(': fixture')), 10)
        publisher = peer.create_publisher(TFMessage if TF else String, '/tf' if TF else '/monitor_fixture', 10)

        def wait(predicate, action=lambda: None):
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                action()
                rclpy.spin_once(peer, timeout_sec=0.03)
                if predicate():
                    return
            self.fail(str([(s.level, [(v.key, v.value) for v in s.values]) for s in statuses[-5:]]))

        def status(value, level):
            return any(s.level == level and any(v.key == 'status' and v.value == value for v in s.values) for s in statuses)

        try:
            wait(lambda: status('NotReceived', DiagnosticStatus.ERROR))
            if TF:
                wrong = TFMessage(transforms=[TransformStamped(child_frame_id='other')])
                wrong.transforms[0].header.frame_id = 'map'
                publisher.publish(wrong)
                # An unrelated frame must not satisfy the monitor.
                deadline = time.monotonic() + 0.5
                statuses.clear()
                while time.monotonic() < deadline:
                    rclpy.spin_once(peer, timeout_sec=0.03)
                self.assertTrue(status('NotReceived', DiagnosticStatus.ERROR))
                message = TFMessage(transforms=[TransformStamped(child_frame_id='base_link')])
                message.transforms[0].header.frame_id = 'map'
                message.transforms[0].transform.rotation.w = 1.0
            else:
                message = String(data='fixture')
            statuses.clear()
            wait(lambda: status('OK', DiagnosticStatus.OK), lambda: publisher.publish(message))
            statuses.clear()
            wait(lambda: status('Timeout', DiagnosticStatus.ERROR))
            client = peer.create_client(SetParameters, '/topic_state_monitor_fixture/set_parameters')
            wait(client.service_is_ready)
            # Disable timeout and force the observed finite rate below the error threshold.
            params = [Parameter(name=k, value=ParameterValue(type=ParameterType.PARAMETER_DOUBLE, double_value=v))
                      for k, v in [('timeout', 0.0), ('error_rate', 1e9)]]
            future = client.call_async(SetParameters.Request(parameters=params))
            wait(future.done)
            self.assertTrue(all(result.successful for result in future.result().results))
            statuses.clear()
            wait(lambda: status('ErrorRate', DiagnosticStatus.ERROR), lambda: publisher.publish(message))
            params = [Parameter(name=k, value=ParameterValue(type=ParameterType.PARAMETER_DOUBLE, double_value=v))
                      for k, v in [('error_rate', 0.0), ('warn_rate', 1e9)]]
            future = client.call_async(SetParameters.Request(parameters=params))
            wait(future.done)
            self.assertTrue(all(result.successful for result in future.result().results))
            statuses.clear()
            wait(lambda: status('WarnRate', DiagnosticStatus.WARN), lambda: publisher.publish(message))
        finally:
            peer.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class Shutdown(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
