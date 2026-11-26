# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0

"""Exercise selected scene plugins through the real behavior-path executable."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from rcl_interfaces.msg import Parameter, ParameterType, ParameterValue
from rcl_interfaces.srv import GetParameters, SetParameters
import rclpy

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())
MODULES = sys.argv.pop(1).split(',')
MODULE = MODULES[0]
PROFILES = {
    'direction_change': ('DirectionChangeModuleManager',
                         ['direction_change.th_arrived_distance', 'direction_change.max_allowed_yaw_deg'],
                         [0.5, 20.0], 21.0),
    'dynamic_obstacle_avoidance': ('DynamicObstacleAvoidanceModuleManager',
                                  ['dynamic_avoidance.target_object.max_obstacle_vel',
                                   'dynamic_avoidance.target_object.min_obj_lat_offset_to_ego_path'],
                                  [100.0, 0.0], 0.1),
    'external_request_lane_change': ('ExternalRequestLaneChangeLeftModuleManager,ExternalRequestLaneChangeRightModuleManager',
                                     ['lane_change.time_limit', 'lane_change.backward_lane_length'],
                                     [50.0, 200.0], 201.0),
    'side_shift': ('SideShiftModuleManager',
                   ['side_shift.min_distance_to_start_shifting', 'side_shift.min_drivable_area_margin'],
                   [5.0, 0.3], 0.4),
    'bidirectional_traffic': ('BidirectionalTrafficModuleManager',
                              ['bidirectional_traffic.min_distance_from_roadside',
                               'bidirectional_traffic.wait_time_for_oncoming_car'],
                              [0.2, 15.0], None),
    'static_obstacle_avoidance': ('StaticObstacleAvoidanceModuleManager',
                                  ['avoidance.resample_interval_for_planning',
                                   'avoidance.target_filtering.max_compensation_time'],
                                  [0.3, 2.0], 2.5),
    'lane_change': ('LaneChangeLeftModuleManager,LaneChangeRightModuleManager',
                    ['lane_change.time_limit', 'lane_change.backward_lane_length'],
                    [50.0, 200.0], 201.0),
    'avoidance_by_lane_change': ('AvoidanceByLaneChangeModuleManager',
                                 ['avoidance_by_lane_change.execute_object_longitudinal_margin',
                                  'lane_change.backward_lane_length'],
                                 [80.0, 200.0], 201.0),
    'start_planner': ('StartPlannerModuleManager',
                      ['start_planner.th_arrived_distance', 'start_planner.th_stopped_time'],
                      [1.0, 1.0], 1.5),
    'goal_planner': ('GoalPlannerModuleManager',
                     ['goal_planner.th_arrived_distance', 'goal_planner.th_stopped_time'],
                     [1.0, 2.0], 2.5),
}
CLASS, PARAMETERS, VALUES, UPDATED = PROFILES[MODULE]
PLUGINS = ['autoware::behavior_path_planner::' + c
           for module in MODULES for c in PROFILES[module][0].split(',')]


class PlannerRuntime(unittest.TestCase):
    def test_plugin_lifecycle(self):
        os.environ['ROS_DOMAIN_ID'] = '71'
        rclpy.init()
        node = rclpy.create_node('behavior_planner_peer')
        namespace = f'/bazel_behavior_{os.getpid()}'
        prefix = namespace + '/behavior_path_planner'
        args = [EXECUTABLE, '--ros-args', '-r', '__ns:=' + namespace,
                '-r', '__node:=behavior_path_planner', '--log-level', 'debug']
        configs = list(dict.fromkeys(
            (['lane_change', 'static_obstacle_avoidance'] if 'avoidance_by_lane_change' in MODULES else []) +
            (['lane_change'] if 'external_request_lane_change' in MODULES else []) +
            [m for m in MODULES if m != 'external_request_lane_change']))
        for package, files in [
            ('autoware_test_utils', ['test_common.param.yaml', 'test_nearest_search.param.yaml',
                                     'test_vehicle_info.param.yaml']),
            ('autoware_behavior_path_planner', ['behavior_path_planner.param.yaml',
                                                'drivable_area_expansion.param.yaml',
                                                'scene_module_manager.param.yaml']),
        ] + [('autoware_behavior_path_' + m + '_module', [m + '.param.yaml']) for m in configs]:
            share = Path(get_package_share_directory(package))
            for name in files:
                args += ['--params-file', str(share / 'config' / name)]
        args += ['-p', 'launch_modules:=[' + ','.join(PLUGINS) + ']']
        parameters = node.create_client(GetParameters, prefix + '/get_parameters')
        update = node.create_client(SetParameters, prefix + '/set_parameters')
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def wait(condition):
                    deadline = time.monotonic() + 25
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if condition():
                            return
                        rclpy.spin_once(node, timeout_sec=0.05)
                    self.fail('Planner timed out: ' + output())

                def call(client, request):
                    future = client.call_async(request)
                    wait(future.done)
                    self.assertIsNone(future.exception())
                    return future.result()

                try:
                    wait(lambda: parameters.service_is_ready() and update.service_is_ready())
                    for plugin in PLUGINS:
                        loaded = "The scene plugin '" + plugin + "' is loaded."
                        wait(lambda: loaded in output())
                    maps = Path('/proc', str(process.pid), 'maps').read_text()
                    scene_libraries = {Path(line.split()[-1]).name for line in maps.splitlines()
                                       if 'libautoware_behavior_path_' in line}
                    # Derived plugins can supply statically linked base factories in a joint process.
                    if len(MODULES) == 1:
                        self.assertIn('libautoware_behavior_path_' + MODULE + '_module.so', scene_libraries)
                    self.assertIn('libautoware_behavior_path_planner_component.so', maps)
                    self.assertNotIn('libagnocast_heaphook.so', maps)
                    names = ['forward_path_length'] + PARAMETERS
                    response = call(parameters, GetParameters.Request(names=names))
                    self.assertEqual([v.double_value for v in response.values], [300.0] + VALUES)
                    # Bidirectional traffic currently has no runtime parameter-update implementation.
                    if UPDATED is not None:
                        response = call(update, SetParameters.Request(parameters=[Parameter(
                            name=names[-1], value=ParameterValue(
                                type=ParameterType.PARAMETER_DOUBLE, double_value=UPDATED))]))
                        self.assertEqual(len(response.results), 1)
                        self.assertTrue(response.results[0].successful, response.results[0].reason)
                        response = call(parameters, GetParameters.Request(names=[names[-1]]))
                        self.assertEqual(response.values[0].double_value, UPDATED)
                    self.assertNotIn('is not available', output())
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Shutdown timed out: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
