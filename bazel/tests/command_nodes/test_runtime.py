# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check indexed command-node entrypoints, calibration resources and gear output."""
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


class CommandRuntime(unittest.TestCase):
    if PACKAGE == 'autoware_raw_vehicle_cmd_converter':
        def test_installed_plotter(self):
            script = Path(get_package_prefix(PACKAGE)) / 'lib' / PACKAGE / 'plot_accel_brake_map.py'
            with tempfile.TemporaryDirectory() as temporary:
                env = dict(os.environ, MPLBACKEND='Agg', MPLCONFIGDIR=temporary)
                for args in [['--help'], ['--plot-dimension', '2', '--accel']]:
                    result = subprocess.run([str(script)] + args, env=env, text=True, stdout=subprocess.PIPE,
                                            stderr=subprocess.STDOUT, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout)
                    self.assertNotIn('No map file found', result.stdout)
                    if args == ['--help']:
                        self.assertIn('--plot-dimension', result.stdout)

    def test_indexed_node(self):
        rclpy.init()
        peer = rclpy.create_node('command_node_peer')
        namespace = '/bazel_command_' + str(os.getpid())
        prefix = namespace + '/under_test'
        binary = Path(get_package_prefix(PACKAGE)) / 'lib' / PACKAGE / EXECUTABLE
        self.assertTrue(binary.is_file(), binary)
        args = [str(binary), '--ros-args', '-r', '__ns:=' + namespace, '-r', '__node:=under_test']
        common = Path(get_package_share_directory('autoware_test_utils')) / 'config'
        for config in ['test_common.param.yaml', 'test_nearest_search.param.yaml', 'test_vehicle_info.param.yaml']:
            args += ['--params-file', str(common / config)]
        args += ['--params-file', str(Path(get_package_share_directory(PACKAGE)) / 'config' / CONFIG)]
        if PACKAGE == 'autoware_vehicle_cmd_gate':
            args += ['-p', 'check_external_emergency_heartbeat:=true']
        if PACKAGE in ['autoware_raw_vehicle_cmd_converter', 'autoware_external_cmd_converter']:
            maps = Path(get_package_share_directory('autoware_raw_vehicle_cmd_converter')) / 'data/default'
            for kind in ['accel', 'brake'] + (['steer'] if PACKAGE == 'autoware_raw_vehicle_cmd_converter' else []):
                path = maps / (kind + '_map.csv')
                self.assertTrue(path.is_file(), path)
                args += ['-p', 'csv_path_' + kind + '_map:=' + str(path)]
        if PACKAGE == 'autoware_shift_decider':
            from autoware_control_msgs.msg import Control
            from autoware_system_msgs.msg import AutowareState
            from autoware_vehicle_msgs.msg import GearCommand, GearReport
            from rclpy.qos import DurabilityPolicy, QoSProfile
            qos = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
            command_pub = peer.create_publisher(Control, namespace + '/input/control_cmd', qos)
            state_pub = peer.create_publisher(AutowareState, namespace + '/input/state', qos)
            gear_pub = peer.create_publisher(GearReport, namespace + '/input/current_gear', qos)
            received = []
            subscription = peer.create_subscription(GearCommand, namespace + '/output/gear_cmd', received.append, qos)
        client = peer.create_client(GetParameters, prefix + '/get_parameters')
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
                    self.fail('Command node timed out: ' + output())

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
                    self.assertNotIn('Cannot read accelmap', output())
                    self.assertNotIn('Cannot read brakemap', output())
                    if PACKAGE == 'autoware_shift_decider':
                        wait(lambda: command_pub.get_subscription_count() > 0 and state_pub.get_subscription_count() > 0
                             and gear_pub.get_subscription_count() > 0 and subscription.get_publisher_count() > 0)
                        for velocity, state, expected in [
                            (2.0, AutowareState.DRIVING, GearCommand.DRIVE),
                            (-2.0, AutowareState.DRIVING, GearCommand.REVERSE),
                            (0.0, AutowareState.DRIVING, GearCommand.REVERSE),
                            (0.0, AutowareState.ARRIVED_GOAL, GearCommand.PARK),
                        ]:
                            received.clear()

                            def publish_and_observe():
                                command = Control()
                                command.longitudinal.velocity = velocity
                                command_pub.publish(command)
                                state_pub.publish(AutowareState(state=state))
                                gear_pub.publish(GearReport(report=GearReport.DRIVE))
                                return len(received) >= 3 and all(message.command == expected for message in received[-3:])

                            wait(publish_and_observe)
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
