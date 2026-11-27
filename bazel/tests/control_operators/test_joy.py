# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Drive all four joystick profiles without physical joystick hardware."""
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory, get_package_prefix
from autoware_control_msgs.msg import Control
from nav_msgs.msg import Odometry
from sensor_msgs.msg import Joy
from tier4_external_api_msgs.msg import Heartbeat, TurnSignalStamped, GearShiftStamped, ControlCommandStamped
from tier4_external_api_msgs.srv import SetEmergency, Engage
import rclpy

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())


class Joystick(unittest.TestCase):
    def test_profiles(self):
        prefix = Path(get_package_prefix('joy'))
        self.assertTrue((prefix / 'lib/joy/joy_node').is_file())
        subprocess.run([str(prefix / 'lib/joy/joy_enumerate_devices')], check=True, timeout=10, capture_output=True, text=True)
        for profile in ['ds4', 'g29', 'p65', 'xbox']:
            with self.subTest(profile=profile):
                self.run_profile(profile)

    def run_profile(self, profile):
        rclpy.init()
        peer = rclpy.create_node('joystick_peer')
        commands, heartbeats, indicators, emergencies, gears, external = [], [], [], [], [], []
        subscriptions = [peer.create_subscription(Control, '/output/control_command', commands.append, 10),
                         peer.create_subscription(Heartbeat, '/output/heartbeat', heartbeats.append, 10),
                         peer.create_subscription(TurnSignalStamped, '/output/turn_signal', indicators.append, 10),
                         peer.create_subscription(GearShiftStamped, '/output/shift', gears.append, 10),
                         peer.create_subscription(ControlCommandStamped, '/output/external_control_command', external.append, 10)]

        def emergency(req, res):
            emergencies.append(req.emergency)
            res.status.code = 0
            return res

        services = [peer.create_service(SetEmergency, '/service/emergency_stop', emergency),
                    peer.create_service(Engage, '/service/autoware_engage', lambda req, res: res)]
        joy_pub = peer.create_publisher(Joy, '/input/joy', 1)
        odom_pub = peer.create_publisher(Odometry, '/input/odometry', 1)
        joy = Joy(axes=[0.0] * 8, buttons=[0] * 32)
        if profile in ['p65', 'xbox']:
            joy.axes[4 if profile == 'xbox' else 2] = 1.0
            joy.axes[5] = 1.0
        joy.axes[0] = 0.5
        feed = True

        def pedals(accel, brake):
            if profile == 'ds4':
                joy.buttons[0], joy.buttons[3] = int(accel), int(brake)
            elif profile == 'g29':
                joy.axes[2], joy.axes[3] = float(accel), float(brake)
            elif profile == 'p65':
                joy.axes[5], joy.axes[2] = 1.0 - 2 * accel, 1.0 - 2 * brake
            else:
                joy.axes[4], joy.axes[5] = 1.0 - 2 * accel, 1.0 - 2 * brake

        def publish():
            stamp = peer.get_clock().now().to_msg()
            joy.header.stamp = stamp
            if feed:
                joy_pub.publish(joy)
            odom = Odometry()
            odom.header.stamp = stamp
            odom.pose.pose.orientation.w = 1.0
            odom.twist.twist.linear.x = 1.0
            odom_pub.publish(odom)

        args = [EXECUTABLE, '--ros-args', '--params-file', str(Path(get_package_share_directory('autoware_joy_controller')) / f'config/joy_controller_{profile}.param.yaml')]
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
                        self.fail('Joystick output absent: ' + output())

                def command(accel, brake, expected_acceleration, expected_velocity):
                    pedals(accel, brake)
                    commands.clear()
                    pump(8, lambda: any(abs(m.longitudinal.acceleration - expected_acceleration) < 1e-5 and abs(m.longitudinal.velocity - expected_velocity) < 1e-5 and abs(m.lateral.steering_tire_angle - 0.25) < 1e-5 for m in commands))

                try:
                    command(1, 0, 3.0, 10.0)
                    command(0, 1, -5.0, 0.0)
                    command(1, 1, 1.0, -2.0)
                    pump(5, lambda: bool(heartbeats) and any(m.control.throttle > 0 and m.control.brake > 0 for m in external))
                    joy.axes[4 if profile == 'g29' else 6] = 1.0
                    pump(5, lambda: any(m.gear_shift.data == 4 for m in gears))
                    joy.axes[4 if profile == 'g29' else 6] = 0.0
                    left = {'ds4': 4, 'g29': 5, 'p65': 4, 'xbox': 6}[profile]
                    joy.buttons[left] = 1
                    pump(5, lambda: any(m.turn_signal.data == 1 for m in indicators))
                    joy.buttons[left] = 0
                    stop = {'ds4': 10, 'g29': 24, 'p65': 6, 'xbox': 15}[profile]
                    joy.buttons[stop] = 1
                    pump(5, lambda: True in emergencies)
                    joy.buttons[stop] = 0
                    if profile in ['ds4', 'g29']:
                        joy.buttons[8] = joy.buttons[24 if profile == 'g29' else 10] = 1
                    else:
                        joy.buttons[7 if profile == 'p65' else 11] = 1
                    pump(5, lambda: False in emergencies)
                    feed = False
                    pump(2.4)
                    commands.clear()
                    pump(0.4)
                    self.assertFalse(commands, 'Expired joystick input still produced commands')
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Joystick shutdown timed out: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
