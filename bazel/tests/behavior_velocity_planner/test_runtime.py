# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0

"""Load each stop-line factory through its real planner executable."""

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
from rcl_interfaces.srv import GetParameters
import rclpy

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())
EXPERIMENTAL = sys.argv.pop(1) == '1'
PLUGIN = ('autoware::behavior_velocity_planner::' +
          ('experimental::' if EXPERIMENTAL else '') + 'StopLineModulePlugin')


class PlannerRuntime(unittest.TestCase):
    def test_plugin_lifecycle(self):
        os.environ['ROS_DOMAIN_ID'] = '70'
        rclpy.init()
        node = rclpy.create_node('behavior_planner_peer')
        namespace = f'/bazel_behavior_{os.getpid()}'
        prefix = namespace + '/behavior_velocity_planner_node'
        args = [EXECUTABLE, '--ros-args', '-r', '__ns:=' + namespace]
        for package, files in [
            ('autoware_test_utils', ['test_common.param.yaml', 'test_nearest_search.param.yaml',
                                     'test_vehicle_info.param.yaml']),
            ('autoware_velocity_smoother', ['default_velocity_smoother.param.yaml',
                                          'Analytical.param.yaml']),
            ('autoware_behavior_velocity_planner_common',
             ['behavior_velocity_planner_common.param.yaml']),
            ('autoware_behavior_velocity_planner', ['behavior_velocity_planner.param.yaml']),
            ('autoware_behavior_velocity_stop_line_module', ['stop_line.param.yaml']),
        ]:
            share = Path(get_package_share_directory(package))
            for name in files:
                args += ['--params-file', str(share / 'config' / name)]
        args += ['-p', 'is_simulation:=false', '-p', 'launch_modules:=[' + PLUGIN + ']']
        load = node.create_client(LoadPlugin, prefix + '/service/load_plugin')
        unload = node.create_client(UnloadPlugin, prefix + '/service/unload_plugin')
        parameters = node.create_client(GetParameters, prefix + '/get_parameters')
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
                    self.assertIn('libautoware_behavior_velocity_stop_line_module.so', maps)
                    self.assertNotIn('libagnocast_heaphook.so', maps)
                    response = call(parameters, GetParameters.Request(
                        names=['forward_path_length', 'stop_line.required_stop_duration_sec']))
                    self.assertEqual([v.double_value for v in response.values], [1000.0, 1.0])
                    call(unload, UnloadPlugin.Request(plugin_name="stop_line"))
                    wait(lambda: "The scene plugin 'stop_line' is unloaded." in output())
                    call(load, LoadPlugin.Request(plugin_name=PLUGIN))
                    wait(lambda: output().count(loaded) == 2)
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
