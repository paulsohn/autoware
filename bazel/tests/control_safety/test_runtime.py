# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Start installed safety components with their indexed configuration closure."""
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

PACKAGE, EXECUTABLE, CONFIG = [sys.argv.pop(1) for _ in range(3)]


class SafetyRuntime(unittest.TestCase):
    def test_indexed_node(self):
        rclpy.init()
        peer = rclpy.create_node('safety_node_peer')
        namespace = '/bazel_safety_' + str(os.getpid())
        binary = Path(get_package_prefix(PACKAGE)) / 'lib' / PACKAGE / EXECUTABLE
        self.assertTrue(binary.is_file(), binary)
        args = [str(binary), '--ros-args', '-r', '__ns:=' + namespace, '-r', '__node:=under_test']
        common = Path(get_package_share_directory('autoware_test_utils')) / 'config'
        for config in ['test_common.param.yaml', 'test_nearest_search.param.yaml', 'test_vehicle_info.param.yaml']:
            args += ['--params-file', str(common / config)]
        args += ['--params-file', str(Path(get_package_share_directory(PACKAGE)) / 'config' / CONFIG)]
        if PACKAGE == 'autoware_control_evaluator':
            args += ['-p', 'output_metrics:=true']
        client = peer.create_client(GetParameters, namespace + '/under_test/get_parameters')
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
                    self.fail('Safety node timed out: ' + output())

                try:
                    wait(client.service_is_ready)
                    future = client.call_async(GetParameters.Request(names=['use_sim_time']))
                    wait(future.done)
                    self.assertIsNone(future.exception())
                    self.assertEqual(len(future.result().values), 1, output())
                    self.assertFalse(future.result().values[0].bool_value)
                    loaded = Path('/proc', str(process.pid), 'maps').read_text()
                    self.assertIn('lib' + PACKAGE + '_component.so', loaded)
                    self.assertNotIn('libagnocast_heaphook.so', loaded)
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
