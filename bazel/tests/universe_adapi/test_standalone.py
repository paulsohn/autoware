# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Start each Universe entry selected by the original API launch dictionary."""
import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_prefix, get_package_share_directory
from rcl_interfaces.srv import GetParameters
import rclpy

PACKAGE = 'autoware_default_adapi_universe'
SHARE = Path(get_package_share_directory(PACKAGE))
spec = importlib.util.spec_from_file_location('original_api_launch', SHARE / 'launch/default_adapi.launch.py')
original = importlib.util.module_from_spec(spec)
spec.loader.exec_module(original)


class Standalone(unittest.TestCase):
    pass


def profile(node_name, executable):
    def test(self):
        rclpy.init()
        peer = rclpy.create_node('standalone_api_peer')
        full_name = '/adapi/node/' + node_name
        binary = Path(get_package_prefix(PACKAGE)) / 'lib' / PACKAGE / executable
        vehicle = Path(get_package_share_directory('sample_vehicle_description')) / 'config/vehicle_info.param.yaml'
        command = [str(binary), '--ros-args', '-r', '__ns:=' + str(Path(full_name).parent),
                   '-r', '__node:=' + Path(full_name).name, '--params-file', str(vehicle),
                   '--params-file', str(SHARE / 'config/default_adapi.param.yaml')]
        try:
            with tempfile.TemporaryFile() as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    return os.pread(log.fileno(), os.fstat(log.fileno()).st_size, 0).decode(errors='replace')

                def wait(condition):
                    deadline = time.monotonic() + 25
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if condition():
                            return
                        rclpy.spin_once(peer, timeout_sec=0.05)
                    self.fail('Standalone API timed out: ' + output())

                try:
                    client = peer.create_client(GetParameters, full_name + '/get_parameters')
                    wait(client.service_is_ready)
                    future = client.call_async(GetParameters.Request(names=['use_sim_time']))
                    wait(future.done)
                    self.assertIsNone(future.exception())
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
    return test


for key, (package, name, _, executable) in original.AGNOCAST_WRAPPER_NODES.items():
    if package == PACKAGE:
        setattr(Standalone, 'test_' + key, profile(name, executable))


if __name__ == '__main__':
    unittest.main()
