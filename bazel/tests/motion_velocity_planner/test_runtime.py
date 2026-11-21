# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0

"""Load selected plugin factories through the real motion planner."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_internal_planning_msgs.srv import LoadPlugin, UnloadPlugin
from rcl_interfaces.srv import GetParameters, SetParameters
from rclpy.parameter import Parameter
import rclpy

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())
MODULE = sys.argv.pop(1) if len(sys.argv) > 1 else 'obstacle_stop'
PLUGIN = 'autoware::motion_velocity_planner::' + ''.join(word.title() for word in MODULE.split('_')) + 'Module'
PARAMETER, EXPECTED = {
    'obstacle_stop': ('obstacle_stop.stop_planning.stop_margin', 5.0),
    'obstacle_slow_down': ('obstacle_slow_down.slow_down_planning.slow_down_min_acc', -1.0),
    'obstacle_cruise': ('obstacle_cruise.cruise_planning.idling_time', 2.0),
    'dynamic_obstacle_stop': ('dynamic_obstacle_stop.minimum_object_velocity', 0.5),
}[MODULE]


class PlannerRuntime(unittest.TestCase):
    def test_plugin_lifecycle(self):
        os.environ['ROS_DOMAIN_ID'] = '70'
        rclpy.init()
        node = rclpy.create_node('motion_planner_peer')
        namespace = f'/bazel_motion_{os.getpid()}'
        prefix = namespace + '/motion_velocity_planner'
        args = [EXECUTABLE, '--ros-args', '-r', '__ns:=' + namespace]
        share = Path(get_package_share_directory('autoware_motion_velocity_planner'))
        for name in [
            'vehicle_info.param.yaml', 'common.param.yaml', 'nearest_search.param.yaml',
            'autoware_velocity_smoother/Analytical.param.yaml',
            'autoware_velocity_smoother/velocity_smoother.param.yaml',
            'motion_velocity_planner/motion_velocity_planner.param.yaml',
        ]:
            args += ['--params-file', str(share / 'test_config' / name)]
        plugin_share = Path(get_package_share_directory('autoware_motion_velocity_' + MODULE + '_module'))
        args += ['--params-file', str(plugin_share / 'config' / (MODULE + '.param.yaml'))]
        args += ['--log-level', 'debug', '-p', 'launch_modules:=[' + PLUGIN + ']']
        load = node.create_client(LoadPlugin, prefix + '/service/load_plugin')
        unload = node.create_client(UnloadPlugin, prefix + '/service/unload_plugin')
        parameters = node.create_client(GetParameters, prefix + '/get_parameters')
        set_parameters = node.create_client(SetParameters, prefix + '/set_parameters')
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
                    wait(lambda: load.service_is_ready() and unload.service_is_ready() and
                         parameters.service_is_ready())
                    loaded = "The scene plugin '" + PLUGIN + "' is loaded."
                    wait(lambda: loaded in output())
                    maps = Path('/proc', str(process.pid), 'maps').read_text()
                    self.assertIn('libautoware_motion_velocity_' + MODULE + '_module.so', maps)
                    self.assertNotIn('libagnocast_heaphook.so', maps)
                    response = call(parameters, GetParameters.Request(
                        names=['smooth_velocity_before_planning', PARAMETER]))
                    self.assertTrue(response.values[0].bool_value)
                    self.assertEqual(response.values[1].double_value, EXPECTED)
                    call(load, LoadPlugin.Request(plugin_name=PLUGIN))
                    wait(lambda: "The plugin '" + PLUGIN + "' is already loaded." in output())
                    self.assertEqual(output().count(loaded), 1)
                    if MODULE == 'obstacle_cruise':
                        retained_name = 'obstacle_cruise.cruise_planning.pid_based_planner.min_accel_during_cruise'
                        wait(set_parameters.service_is_ready)
                        result = call(set_parameters, SetParameters.Request(parameters=[
                            Parameter(retained_name, value=-1.25).to_parameter_msg()]))
                        self.assertTrue(result.results[0].successful, result.results[0].reason)
                    call(unload, UnloadPlugin.Request(plugin_name=PLUGIN))
                    wait(lambda: "The scene plugin '" + PLUGIN + "' is unloaded." in output())
                    call(load, LoadPlugin.Request(plugin_name=PLUGIN))
                    wait(lambda: output().count(loaded) == 2)
                    if MODULE == 'obstacle_cruise':
                        result = call(parameters, GetParameters.Request(names=[retained_name]))
                        self.assertEqual(result.values[0].double_value, -1.25)
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
