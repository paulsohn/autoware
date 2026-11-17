# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise original core XML launch composition and shutdown."""

import os
from pathlib import Path
import time
import unittest

from ament_index_python.packages import get_package_share_directory
import launch
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import AnyLaunchDescriptionSource
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import pytest
from rcl_interfaces.srv import GetParameters
import rclpy

MODE = os.environ['CORE_LAUNCH_MODE']
PLANNING = MODE == 'planning'


@pytest.mark.launch_test
def generate_test_description():
    package = 'autoware_core_' + ('planning' if PLANNING else 'control')
    vehicle = str(Path(get_package_share_directory('autoware_test_utils')) /
                  'config/test_vehicle_info.param.yaml')
    arguments = {'vehicle_param_file' if PLANNING else 'vehicle_info_param_file': vehicle}
    if MODE == 'control_gate':
        arguments['launch_command_gate'] = 'true'
    source = str(Path(get_package_share_directory(package)) / 'launch' / (package + '.launch.xml'))
    return launch.LaunchDescription([
        IncludeLaunchDescription(AnyLaunchDescriptionSource(source),
                                 launch_arguments=arguments.items()),
        launch_testing.actions.ReadyToTest(),
    ])


class TestComposition(unittest.TestCase):
    def test_configuration_and_remaps(self):
        rclpy.init()
        node = rclpy.create_node('core_launch_peer')
        try:
            def wait(condition):
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline:
                    if condition():
                        return
                    rclpy.spin_once(node, timeout_sec=0.05)
                self.fail('Core launch did not reach the expected graph/configuration')

            def parameter(name, key):
                client = node.create_client(GetParameters, name + '/get_parameters')
                wait(client.service_is_ready)
                future = client.call_async(GetParameters.Request(names=[key]))
                wait(future.done)
                self.assertIsNone(future.exception())
                return future.result().values[0]

            def names():
                return {namespace.rstrip('/') + '/' + name
                        for name, namespace in node.get_node_names_and_namespaces()}

            if PLANNING:
                root = '/planning/scenario_planning/lane_driving/'
                behavior = root + 'behavior_planning/behavior_velocity_planner'
                path = root + 'behavior_planning/path_generator'
                motion = root + 'motion_planning/motion_velocity_planner'
                converter = root + 'motion_planning/path_to_trajectory_converter'
                smoother = '/planning/scenario_planning/velocity_smoother'
                expected = {behavior, path, motion, converter, smoother,
                            '/planning/mission_planning/mission_planner'}
                wait(lambda: expected <= names())
                self.assertEqual(parameter(path, 'planning_hz').double_value, 10.0)
                self.assertEqual(parameter(behavior, 'stop_line.required_stop_duration_sec').double_value, 1.0)
                self.assertEqual(parameter(motion, 'obstacle_stop.stop_planning.stop_margin').double_value, 5.0)
                self.assertEqual(parameter(smoother, 'algorithm_type').string_value, 'JerkFiltered')
                wait(lambda: node.count_publishers('/planning/trajectory') > 0 and
                     node.count_subscribers(root + 'behavior_planning/path_with_lane_id') > 0 and
                     node.count_publishers(root + 'motion_planning/path_optimizer/trajectory') > 0 and
                     node.count_subscribers(root + 'motion_planning/path_optimizer/trajectory') > 0)
            else:
                pursuit = '/control/simple_pure_pursuit'
                gate = '/control/autoware_command_gate'
                wait(lambda: pursuit in names() and (MODE != 'control_gate' or gate in names()))
                self.assertEqual(parameter(pursuit, 'lookahead_gain').double_value, 1.0)
                if MODE == 'control':
                    self.assertNotIn(gate, names())
                wait(lambda: node.count_publishers('/control/command/control_cmd') > 0 and
                     node.count_subscribers('/planning/trajectory') > 0 and
                     node.count_subscribers('/localization/kinematic_state') > 0)
        finally:
            node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
