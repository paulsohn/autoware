# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check selected validator factories in their installed executable closure."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_prefix, get_package_share_directory
from rcl_interfaces.srv import GetParameters
import rclpy

PROFILE = sys.argv.pop(1)
MODULES = ['latency_checker', 'trajectory_checker', 'intersection_collision_checker', 'rear_collision_checker']
if PROFILE != 'selected':
    MODULES = [] if PROFILE in ['base', 'invalid'] else [PROFILE]
PACKAGE = 'autoware_planning_validator'


class ValidatorRuntime(unittest.TestCase):
    def test_indexed_factories(self):
        rclpy.init()
        peer = rclpy.create_node('validator_peer')
        name = 'bazel_validator_' + str(os.getpid())
        executable = 'invalid_trajectory_publisher' if PROFILE == 'invalid' else PACKAGE + '_node'
        binary = Path(get_package_prefix(PACKAGE)) / 'lib' / PACKAGE / executable
        args = [str(binary), '--ros-args', '-r', '__node:=' + name, '--log-level', 'debug']
        classes = ['autoware::planning_validator::' + ''.join(w.title() for w in m.split('_')) for m in MODULES]
        if PROFILE != 'invalid':
            share = Path(get_package_share_directory(PACKAGE))
            args += ['--params-file', str(share / 'config/planning_validator.param.yaml')]
            common = Path(get_package_share_directory('autoware_test_utils')) / 'config'
            for config in ['test_common.param.yaml', 'test_vehicle_info.param.yaml', 'test_nearest_search.param.yaml']:
                args += ['--params-file', str(common / config)]
            for module in MODULES:
                share = Path(get_package_share_directory(PACKAGE + '_' + module))
                args += ['--params-file', str(share / 'config' / (module + '.param.yaml'))]
            args += ['-p', 'launch_modules:=[' + ','.join(classes + ['""']) + ']']
        client = peer.create_client(GetParameters, '/' + name + '/get_parameters')
        try:
            with tempfile.TemporaryFile() as log:
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    return os.pread(log.fileno(), os.fstat(log.fileno()).st_size, 0).decode(errors='replace')

                def wait(condition):
                    deadline = time.monotonic() + 25
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if condition():
                            return
                        rclpy.spin_once(peer, timeout_sec=0.05)
                    self.fail('Validator timed out: ' + output())

                try:
                    wait(client.service_is_ready)
                    for plugin in classes:
                        wait(lambda: "The plugin '" + plugin + "' has been loaded" in output())
                    request = GetParameters.Request(names=['use_sim_time'] + ([] if PROFILE == 'invalid' else ['th_traffic_light_timeout']))
                    future = client.call_async(request)
                    wait(future.done)
                    self.assertIsNone(future.exception())
                    self.assertEqual(len(future.result().values), len(request.names), output())
                    self.assertFalse(future.result().values[0].bool_value)
                    if PROFILE != 'invalid':
                        self.assertEqual(future.result().values[1].double_value, 0.5)
                    maps = Path('/proc', str(process.pid), 'maps').read_text()
                    component = PACKAGE + ('_invalid_component' if PROFILE == 'invalid' else '_component')
                    self.assertIn('lib' + component + '.so', maps)
                    if len(MODULES) == 1:
                        self.assertIn('lib' + PACKAGE + '_' + MODULES[0] + '.so', maps)
                    self.assertNotIn('libagnocast_heaphook.so', maps)
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
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
