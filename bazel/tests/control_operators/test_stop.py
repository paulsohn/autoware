# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise stop hold, route-dependent parking and stale vehicle feedback."""
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_control_msgs.msg import Control
from autoware_planning_msgs.msg import RouteState
from autoware_vehicle_msgs.msg import GearCommand, HazardLightsCommand, SteeringReport, TurnIndicatorsCommand, VelocityReport
import rclpy

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())


class StopOperator(unittest.TestCase):
    def test_commands_and_parking(self):
        rclpy.init()
        peer = rclpy.create_node('stop_operator_peer')
        messages = {key: [] for key in ['control', 'gear', 'turn_indicators', 'hazard_lights']}
        subscriptions = [peer.create_subscription(typ, '/stop_mode_operator/' + key, messages[key].append, 10)
                         for key, typ in [('control', Control), ('gear', GearCommand),
                                          ('turn_indicators', TurnIndicatorsCommand), ('hazard_lights', HazardLightsCommand)]]
        steer_pub = peer.create_publisher(SteeringReport, '/vehicle/status/steering_status', 1)
        velocity_pub = peer.create_publisher(VelocityReport, '/vehicle/status/velocity_status', 1)
        route_pub = peer.create_publisher(RouteState, '/planning/route_state', 1)
        velocity = 1.0
        route = RouteState.UNSET
        feedback = True

        def publish():
            stamp = peer.get_clock().now().to_msg()
            steer_pub.publish(SteeringReport(stamp=stamp, steering_tire_angle=0.2))
            route_pub.publish(RouteState(stamp=stamp, state=route))
            if feedback:
                report = VelocityReport(longitudinal_velocity=velocity)
                report.header.stamp = stamp
                velocity_pub.publish(report)

        args = [EXECUTABLE, '--ros-args', '--params-file',
                str(Path(get_package_share_directory('autoware_stop_mode_operator')) / 'config/default.param.yaml')]
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def pump(duration, predicate=None):
                    deadline = time.monotonic() + duration
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        publish()
                        rclpy.spin_once(peer, timeout_sec=0.03)
                        if predicate is not None and predicate():
                            return
                    if predicate is not None:
                        self.fail('Expected command absent: ' + output())

                def gear(command):
                    messages['gear'].clear()
                    pump(5, lambda: any(m.command == command for m in messages['gear']))

                try:
                    gear(GearCommand.NONE)
                    pump(5, lambda: any(abs(m.lateral.steering_tire_angle - 0.2) < 1e-5 for m in messages['control']))
                    cmd = messages['control'][-1]
                    self.assertEqual(cmd.longitudinal.velocity, 0.0)
                    self.assertAlmostEqual(cmd.longitudinal.acceleration, -1.5)
                    velocity = 0.0
                    route = RouteState.SET
                    pump(1.5)
                    gear(GearCommand.NONE)
                    route = RouteState.ARRIVED
                    gear(GearCommand.PARK)
                    velocity = 1.0
                    gear(GearCommand.NONE)
                    velocity = 0.0
                    route = RouteState.UNSET
                    gear(GearCommand.PARK)
                    feedback = False
                    gear(GearCommand.NONE)
                    self.assertTrue(messages['turn_indicators'])
                    self.assertTrue(messages['hazard_lights'])
                    self.assertTrue(all(m.command == TurnIndicatorsCommand.DISABLE for m in messages['turn_indicators']))
                    self.assertTrue(all(m.command == HazardLightsCommand.DISABLE for m in messages['hazard_lights']))
                finally:
                    process.send_signal(signal.SIGINT) if process.poll() is None else None
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Stop operator failed to shut down: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
