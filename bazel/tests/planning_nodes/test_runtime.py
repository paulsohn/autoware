# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise indexed executable startup separately from in-process native tests."""
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

PACKAGE = sys.argv.pop(1)
EXECUTABLE = sys.argv.pop(1)
CONFIG = sys.argv.pop(1)


class IndexedNode(unittest.TestCase):
    if PACKAGE == 'autoware_path_smoother':
        def test_plotter_entrypoint(self):
            binary = Path(get_package_prefix(PACKAGE)) / 'lib' / PACKAGE / 'calculation_time_plotter.py'
            result = subprocess.run([str(binary), '--help'], env=dict(os.environ, MPLBACKEND='Agg'),
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=25)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn('--functions', result.stdout)

    def test_startup_and_shutdown(self):
        rclpy.init()
        peer = rclpy.create_node('planning_executable_peer')
        name = 'bazel_planning_' + str(os.getpid())
        binary = Path(get_package_prefix(PACKAGE)) / 'lib' / PACKAGE / EXECUTABLE
        self.assertTrue(binary.is_file(), binary)
        args = [str(binary), '--ros-args', '-r', '__node:=' + name]
        common = Path(get_package_share_directory('autoware_test_utils')) / 'config'
        for config in ['test_common.param.yaml', 'test_nearest_search.param.yaml', 'test_vehicle_info.param.yaml']:
            args += ['--params-file', str(common / config)]
        args += ['--params-file', str(Path(get_package_share_directory(PACKAGE)) / 'config' / CONFIG)]
        client = peer.create_client(GetParameters, '/' + name + '/get_parameters')
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
                        rclpy.spin_once(peer, timeout_sec=0.05)
                    self.fail('Startup timed out: ' + output())

                try:
                    wait(client.service_is_ready)
                    future = client.call_async(GetParameters.Request(names=['use_sim_time']))
                    wait(future.done)
                    self.assertIsNone(future.exception())
                    self.assertFalse(future.result().values[0].bool_value)
                    maps = Path('/proc', str(process.pid), 'maps').read_text()
                    self.assertIn('lib' + PACKAGE + '_component.so', maps)
                    self.assertNotIn('libagnocast_heaphook.so', maps)
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
