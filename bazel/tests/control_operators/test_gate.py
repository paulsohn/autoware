# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise command selection, safety overrides, limits and timeout fallback."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_control_msgs.msg import Control
from autoware_vehicle_msgs.msg import ControlModeReport, GearCommand, HazardLightsCommand, SteeringReport, TurnIndicatorsCommand
from diagnostic_msgs.msg import DiagnosticArray
from geometry_msgs.msg import AccelWithCovarianceStamped
from nav_msgs.msg import Odometry
from tier4_control_msgs.srv import SetPause, SetStop
from tier4_external_api_msgs.srv import SetEmergency
from tier4_system_msgs.msg import CommandFilterStatus, CommandSourceStatus
from tier4_system_msgs.srv import ChangeCommandFilter, ChangeCommandSource, SelectCommandSource
import rclpy
from rclpy.qos import QoSProfile, DurabilityPolicy

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())
HEAPHOOK = str(Path(sys.argv.pop(1)).resolve())
PREFIX = '/control_command_gate'


class CommandGate(unittest.TestCase):
    def test_selection_and_safety(self):
        rclpy.init()
        peer = rclpy.create_node('command_gate_peer')
        messages = {key: [] for key in ['control', 'gear', 'turn_indicators', 'hazard_lights', 'source', 'filter', 'diagnostics']}
        retained = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
        subscriptions = []
        for key, typ in [('control', Control), ('gear', GearCommand), ('turn_indicators', TurnIndicatorsCommand), ('hazard_lights', HazardLightsCommand)]:
            subscriptions.append(peer.create_subscription(typ, PREFIX + '/output/' + key, messages[key].append, 10))
        for key, typ in [('source', CommandSourceStatus), ('filter', CommandFilterStatus)]:
            subscriptions.append(peer.create_subscription(typ, PREFIX + '/' + key + '/status', messages[key].append, retained))
        subscriptions.append(peer.create_subscription(DiagnosticArray, '/diagnostics', messages['diagnostics'].append, 10))
        inputs = {}
        for source in ['main', 'local']:
            inputs[source] = [(key, peer.create_publisher(typ, PREFIX + '/inputs/' + source + '/' + key, 1))
                              for key, typ in [('control', Control), ('gear', GearCommand), ('turn_indicators', TurnIndicatorsCommand), ('hazard_lights', HazardLightsCommand)]]
        odom_pub = peer.create_publisher(Odometry, '/localization/kinematic_state', 1)
        accel_pub = peer.create_publisher(AccelWithCovarianceStamped, '/localization/acceleration', 1)
        steer_pub = peer.create_publisher(SteeringReport, '/vehicle/status/steering_status', 1)
        mode_pub = peer.create_publisher(ControlModeReport, '/vehicle/status/control_mode', 1)
        feed = False
        invalid = False

        def publish():
            stamp = peer.get_clock().now().to_msg()
            odom = Odometry()
            odom.header.stamp = stamp
            odom.pose.pose.orientation.w = 1.0
            odom.twist.twist.linear.x = 10.0
            odom_pub.publish(odom)
            accel = AccelWithCovarianceStamped()
            accel.header.stamp = stamp
            accel_pub.publish(accel)
            steer_pub.publish(SteeringReport(stamp=stamp))
            mode_pub.publish(ControlModeReport(stamp=stamp, mode=ControlModeReport.AUTONOMOUS))
            if not feed:
                return
            for source, pubs in inputs.items():
                control = Control(stamp=stamp)
                control.longitudinal.stamp = control.lateral.stamp = stamp
                control.longitudinal.velocity = float('nan') if invalid else 50.0 if source == 'main' else 2.0
                values = {'control': control,
                          'gear': GearCommand(stamp=stamp, command=GearCommand.DRIVE if source == 'main' else GearCommand.REVERSE),
                          'turn_indicators': TurnIndicatorsCommand(stamp=stamp, command=TurnIndicatorsCommand.ENABLE_LEFT if source == 'main' else TurnIndicatorsCommand.ENABLE_RIGHT),
                          'hazard_lights': HazardLightsCommand(stamp=stamp, command=HazardLightsCommand.DISABLE)}
                for key, pub in pubs:
                    pub.publish(values[key])

        configs = [('autoware_control_command_gate', 'default.param.yaml'), ('autoware_test_utils', 'test_vehicle_info.param.yaml')]
        args = [EXECUTABLE, '--ros-args']
        for package, config in configs:
            args += ['--params-file', str(Path(get_package_share_directory(package)) / 'config' / config)]
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                environment = dict(os.environ)
                enabled = environment.get('ENABLE_AGNOCAST') == '1'
                if enabled:
                    environment['LD_PRELOAD'] = HEAPHOOK
                else:
                    environment.pop('LD_PRELOAD', None)
                process = subprocess.Popen(args, env=environment, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def until(predicate, timeout=8):
                    deadline = time.monotonic() + timeout
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        publish()
                        rclpy.spin_once(peer, timeout_sec=0.03)
                        if predicate():
                            return
                    self.fail('Command gate condition absent: ' + output())

                def call(typ, topic, request):
                    client = peer.create_client(typ, topic)
                    try:
                        until(client.service_is_ready)
                        future = client.call_async(request)
                        until(future.done)
                        return future.result()
                    finally:
                        peer.destroy_client(client)

                def control(predicate):
                    messages['control'].clear()
                    until(lambda: any(predicate(m) for m in messages['control']))

                try:
                    until(lambda: any(m.source == 1 for m in messages['source']))
                    control(lambda m: m.longitudinal.velocity == 0 and m.longitudinal.acceleration < 0)
                    self.assertFalse(call(ChangeCommandSource, PREFIX + '/source/change', ChangeCommandSource.Request(source=999)).status.success)
                    self.assertFalse(call(ChangeCommandSource, PREFIX + '/source/change', ChangeCommandSource.Request(source=12)).status.success)
                    feed = True
                    # Input diagnostics must become fresh before a source is selectable.
                    until(lambda: any(s.name.endswith('timeout_main') and s.level == b'\x00' and any(v.key == 'duration' for v in s.values) for d in messages['diagnostics'] for s in d.status))
                    self.assertTrue(call(ChangeCommandSource, PREFIX + '/source/change', ChangeCommandSource.Request(source=12)).status.success)
                    control(lambda m: 0 < m.longitudinal.velocity <= 25.0)
                    until(lambda: any(m.command == GearCommand.DRIVE for m in messages['gear']))
                    until(lambda: any(m.command == TurnIndicatorsCommand.ENABLE_LEFT for m in messages['turn_indicators']))
                    self.assertTrue(call(ChangeCommandFilter, PREFIX + '/filter/change', ChangeCommandFilter.Request(filter=True)).status.success)
                    until(lambda: any(m.filter for m in messages['filter']))
                    self.assertTrue(call(ChangeCommandFilter, PREFIX + '/filter/change', ChangeCommandFilter.Request(filter=False)).status.success)
                    for typ, topic, request, cancel, acceleration in [
                        (SetPause, '/control/vehicle_cmd_gate/set_pause', SetPause.Request(pause=True), SetPause.Request(pause=False), -1.5),
                        (SetStop, '/control/vehicle_cmd_gate/set_stop', SetStop.Request(stop=True, request_source='bazel_test'), SetStop.Request(stop=False, request_source='bazel_test'), -1.5),
                        (SetEmergency, '/api/autoware/set/emergency', SetEmergency.Request(emergency=True), SetEmergency.Request(emergency=False), -2.4),
                    ]:
                        call(typ, topic, request)
                        control(lambda m: m.longitudinal.velocity == 0 and m.longitudinal.acceleration <= acceleration + 1e-5)
                        call(typ, topic, cancel)
                        control(lambda m: m.longitudinal.velocity > 0)
                    self.assertTrue(call(SelectCommandSource, PREFIX + '/source/select', SelectCommandSource.Request(source=13, transition=False)).status.success)
                    until(lambda: any(m.source == 13 for m in messages['source']))
                    messages['gear'].clear()
                    until(lambda: any(m.command == GearCommand.REVERSE for m in messages['gear']))
                    # Rejected nonfinite commands must not refresh the selected source.
                    invalid = True
                    messages['source'].clear()
                    until(lambda: any(m.source == 1 for m in messages['source']))
                    control(lambda m: m.longitudinal.velocity == 0 and m.longitudinal.acceleration < 0)
                    self.assertTrue(all(m.longitudinal.velocity == m.longitudinal.velocity for m in messages['control']))
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Command gate shutdown timed out: ' + output())
                    self.assertEqual(process.returncode, 0, output())
                    self.assertIn('Starting node with ' + ('agnocast' if enabled else 'rclcpp') + ' mode', output())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
