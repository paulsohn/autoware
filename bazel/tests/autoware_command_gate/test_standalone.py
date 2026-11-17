# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the native command-gate executable and its state/gear outputs."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from autoware_adapi_v1_msgs.msg import OperationModeState
from autoware_system_msgs.srv import ChangeOperationMode
from autoware_vehicle_msgs.msg import GearCommand
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())


class CommandGateRuntime(unittest.TestCase):
    def test_modes_and_rejection(self):
        os.environ['ROS_DOMAIN_ID'] = '62'
        rclpy.init()
        node = rclpy.create_node('command_gate_peer')
        api, system, gears = [], [], []
        qos = QoSProfile(depth=10, durability=DurabilityPolicy.TRANSIENT_LOCAL)
        node.create_subscription(OperationModeState, '/api/operation_mode/state', api.append, qos)
        node.create_subscription(OperationModeState, '/system/operation_mode/state', system.append, qos)
        node.create_subscription(GearCommand, '/control/command/gear_cmd', gears.append, 10)
        client = node.create_client(ChangeOperationMode, '/system/operation_mode/change_operation_mode')
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen([EXECUTABLE], stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def wait(predicate):
                    deadline = time.monotonic() + 15
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if predicate():
                            return
                        rclpy.spin_once(node, timeout_sec=0.1)
                    self.fail('Command gate timed out: ' + output())

                try:
                    wait(lambda: client.service_is_ready() and
                         node.count_publishers('/control/command/gear_cmd') > 0)
                    for mode, gear in [(ChangeOperationMode.Request.STOP, GearCommand.PARK),
                                       (ChangeOperationMode.Request.AUTONOMOUS, GearCommand.DRIVE),
                                       (ChangeOperationMode.Request.LOCAL, GearCommand.NONE),
                                       (ChangeOperationMode.Request.REMOTE, GearCommand.NONE)]:
                        counts = (len(api), len(system), len(gears))
                        future = client.call_async(ChangeOperationMode.Request(mode=mode))
                        wait(future.done)
                        self.assertTrue(future.result().status.success)
                        wait(lambda: len(api) > counts[0] and len(system) > counts[1] and
                             len(gears) > counts[2])
                        self.assertEqual(api[-1].mode, mode)
                        self.assertEqual(system[-1].mode, mode)
                        self.assertEqual(gears[-1].command, gear)
                        self.assertEqual(api[-1].stamp, gears[-1].stamp)
                    counts = (len(api), len(system), len(gears))
                    future = client.call_async(ChangeOperationMode.Request(mode=65535))
                    wait(future.done)
                    self.assertFalse(future.result().status.success)
                    self.assertNotEqual(future.result().status.code, 0)
                    deadline = time.monotonic() + 0.3
                    while time.monotonic() < deadline:
                        rclpy.spin_once(node, timeout_sec=0.05)
                    self.assertEqual((len(api), len(system), len(gears)), counts)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Command gate failed to stop: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
