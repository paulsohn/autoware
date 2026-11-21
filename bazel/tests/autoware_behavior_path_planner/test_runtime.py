# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0

"""Exercise the side-shift plugin through the real behavior-path executable."""

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
PLUGIN = 'autoware::behavior_path_planner::SideShiftModuleManager'


class PlannerRuntime(unittest.TestCase):
    def test_plugin_lifecycle(self):
        os.environ['ROS_DOMAIN_ID'] = '71'
        rclpy.init()
        node = rclpy.create_node('behavior_planner_peer')
        namespace = f'/bazel_behavior_{os.getpid()}'
        prefix = namespace + '/behavior_path_planner'
        args = [EXECUTABLE, '--ros-args', '-r', '__ns:=' + namespace,
                '-r', '__node:=behavior_path_planner', '--log-level', 'debug']
        for package, files in [
            ('autoware_test_utils', ['test_common.param.yaml', 'test_nearest_search.param.yaml',
                                     'test_vehicle_info.param.yaml']),
            ('autoware_behavior_path_planner', ['behavior_path_planner.param.yaml',
                                                'drivable_area_expansion.param.yaml',
                                                'scene_module_manager.param.yaml']),
            ('autoware_behavior_path_side_shift_module', ['side_shift.param.yaml']),
        ]:
            share = Path(get_package_share_directory(package))
            for name in files:
                args += ['--params-file', str(share / 'config' / name)]
        args += ['-p', 'launch_modules:=[' + PLUGIN + ']']
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
                    loaded = "The scene plugin '" + PLUGIN + "' is loaded."
                    wait(lambda: loaded in output())
                    maps = Path('/proc', str(process.pid), 'maps').read_text()
                    self.assertIn('libautoware_behavior_path_side_shift_module.so', maps)
                    self.assertIn('libautoware_behavior_path_planner_component.so', maps)
                    self.assertNotIn('libagnocast_heaphook.so', maps)
                    names = ['forward_path_length', 'side_shift.min_distance_to_start_shifting',
                             'side_shift.min_drivable_area_margin']
                    response = call(parameters, GetParameters.Request(names=names))
                    self.assertEqual([v.double_value for v in response.values], [300.0, 5.0, 0.3])
                    response = call(update, SetParameters.Request(parameters=[Parameter(
                        name=names[-1], value=ParameterValue(
                            type=ParameterType.PARAMETER_DOUBLE, double_value=0.4))]))
                    self.assertEqual(len(response.results), 1)
                    self.assertTrue(response.results[0].successful, response.results[0].reason)
                    response = call(parameters, GetParameters.Request(names=[names[-1]]))
                    self.assertEqual(response.values[0].double_value, 0.4)
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
