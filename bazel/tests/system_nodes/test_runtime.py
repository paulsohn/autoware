# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise installed system executables and their observable ROS contracts."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_prefix, get_package_share_directory
from autoware_control_msgs.msg import Control
from autoware_internal_debug_msgs.msg import Float64Stamped, StringStamped
from tier4_metric_msgs.msg import MetricArray
from autoware_internal_planning_msgs.msg import VelocityLimit, VelocityLimitClearCommand
from autoware_system_msgs.msg import HazardStatusStamped
from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus
from rcl_interfaces.srv import GetParameters
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile
from tier4_system_msgs.msg import (
    CommandModeAvailability, CommandModeAvailabilityItem, DiagGraphStatus, DiagGraphStruct,
    DiagNodeStatus, DiagNodeStruct, ModeChangeAvailable,
    MrmBehaviorStatus, OperationModeAvailability,
)
from tier4_system_msgs.srv import OperateMrm

PACKAGE, EXECUTABLE, CONFIG = [sys.argv.pop(1) for _ in range(3)]
RETAINED = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)


class SystemRuntime(unittest.TestCase):
    def test_installed_contract(self):
        binary = Path(get_package_prefix(PACKAGE)) / 'lib' / PACKAGE / EXECUTABLE
        share = Path(get_package_share_directory(PACKAGE))
        self.assertTrue(binary.is_file(), binary)
        if EXECUTABLE == 'tree':
            result = subprocess.run([str(binary), str(share / 'example/graph/main.yaml')],
                                    capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Top-level trees', result.stdout)
            self.assertIn('/autoware/modes/autonomous', result.stdout)
            return
        rclpy.init()
        peer = rclpy.create_node('system_test_peer')
        namespace = '/bazel_system_' + str(os.getpid())
        self.private = namespace + '/under_test'
        self.peer = peer
        args = [str(binary), '--ros-args', '-r', '__ns:=' + namespace, '-r', '__node:=under_test']
        if CONFIG != '-':
            args += ['--params-file', str(share / 'config' / CONFIG)]
        if PACKAGE == 'autoware_component_state_monitor':
            args += ['-p', 'autonomous.planning:=[bazel/test]']
        if PACKAGE == 'autoware_processing_time_checker':
            args += ['-p', 'output_metrics:=true', '-p',
                     'processing_time_topic_name_list:=[' + namespace + '/probe/debug/processing_time_ms]']
        if EXECUTABLE == 'aggregator_node':
            args += ['-p', 'graph_file:=' + str(share / 'example/graph/main.yaml')]
        if EXECUTABLE == 'logging_node':
            for parameter in ['root_path:=/autoware/modes/autonomous', 'max_depth:=3', 'show_rate:=10.0',
                              'enable_terminal_log:=false', 'ignore_dependent_error:=false']:
                args += ['-p', parameter]
        try:
            with tempfile.TemporaryDirectory() as logs, tempfile.TemporaryFile() as log:
                environment = dict(os.environ, ROS_LOG_DIR=logs)
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, env=environment)

                def output():
                    return os.pread(log.fileno(), os.fstat(log.fileno()).st_size, 0).decode(errors='replace')

                def wait(condition, publish=None):
                    deadline = time.monotonic() + 25
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if condition():
                            return
                        if publish:
                            publish()
                        rclpy.spin_once(peer, timeout_sec=0.05)
                    self.fail('System node timed out: ' + output())

                self.wait = wait
                self.output = output
                try:
                    client = peer.create_client(GetParameters, self.private + '/get_parameters')
                    wait(client.service_is_ready)
                    future = client.call_async(GetParameters.Request(names=['use_sim_time']))
                    wait(future.done)
                    self.assertIsNone(future.exception())
                    self.assertFalse(future.result().values[0].bool_value)
                    loaded = Path('/proc', str(process.pid), 'maps').read_text()
                    self.assertIn('lib' + PACKAGE + '_component.so', loaded)
                    self.assertNotIn('libagnocast_heaphook.so', loaded)
                    if PACKAGE == 'autoware_component_state_monitor':
                        self.component_state()
                    elif PACKAGE == 'autoware_duplicated_node_checker':
                        self.duplicate_nodes(namespace)
                    elif PACKAGE == 'autoware_processing_time_checker':
                        self.processing_time(namespace)
                    elif PACKAGE == 'autoware_pipeline_latency_monitor':
                        self.latency()
                    elif PACKAGE in ('autoware_mrm_comfortable_stop_operator', 'autoware_mrm_emergency_stop_operator'):
                        self.mrm_operator()
                    elif PACKAGE == 'autoware_diagnostic_graph_aggregator':
                        self.aggregator() if EXECUTABLE == 'aggregator_node' else self.mode_converter()
                    elif PACKAGE in ('autoware_diagnostic_graph_utils', 'autoware_hazard_status_converter'):
                        self.graph_consumer()
                    elif PACKAGE == 'autoware_dummy_diag_publisher':
                        messages = self.subscribe(DiagnosticArray, '/diagnostics')
                        wait(lambda: any(s.name.endswith('velodyne_temperature') and s.level == DiagnosticStatus.WARN
                                         for m in messages for s in m.status))
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
                if PACKAGE == 'autoware_processing_time_checker':
                    files = list(Path(logs).glob('autoware_metrics/autoware_processing_time_checker-*.json'))
                    self.assertEqual(len(files), 1, output())
                    metrics = json.loads(files[0].read_text())
                    self.assertEqual(metrics['probe/min'], 12.5)
                    self.assertGreater(metrics['probe/count'], 0)
        finally:
            peer.destroy_node()
            rclpy.shutdown()

    def subscribe(self, message_type, topic, qos=10):
        messages = []
        self.peer.create_subscription(message_type, topic, messages.append, qos)
        return messages

    def component_state(self):
        states = self.subscribe(ModeChangeAvailable, self.private + '/autonomous/planning', RETAINED)
        publisher = self.peer.create_publisher(DiagnosticArray, '/diagnostics', 10)
        self.wait(lambda: states and not states[-1].available)
        for level, expected in [(DiagnosticStatus.OK, True), (DiagnosticStatus.ERROR, False)]:
            states.clear()
            message = DiagnosticArray(status=[DiagnosticStatus(
                name='bazel/test', hardware_id='topic_state_monitor', level=level)])
            self.wait(lambda: states and states[-1].available == expected, lambda: publisher.publish(message))

    def duplicate_nodes(self, namespace):
        diagnostics = self.subscribe(DiagnosticArray, '/diagnostics')
        nodes = [rclpy.create_node('duplicate_probe', namespace=namespace, enable_rosout=False) for _ in range(2)]
        try:
            self.wait(lambda: any(s.level == DiagnosticStatus.ERROR and 'duplicate_probe' in s.message
                                  for m in diagnostics for s in m.status))
        finally:
            for node in nodes:
                node.destroy_node()

    def processing_time(self, namespace):
        messages = self.subscribe(MetricArray, self.private + '/metrics')
        publisher = self.peer.create_publisher(Float64Stamped, namespace + '/probe/debug/processing_time_ms', 10)
        self.wait(lambda: any(m.name == 'processing_time/probe' and float(m.value) == 12.5
                              for msg in messages for m in msg.metric_array),
                  lambda: publisher.publish(Float64Stamped(data=12.5)))

    def latency(self):
        messages = self.subscribe(Float64Stamped, self.private + '/output/total_latency_ms')
        publisher = self.peer.create_publisher(Float64Stamped, '/control/control_component_latency', 10)
        # Original offsets total 330 ms; the last step converts 0.02 seconds to 20 ms.
        self.wait(lambda: any(abs(msg.data - 350.0) < 1e-6 for msg in messages),
                  lambda: publisher.publish(Float64Stamped(stamp=self.peer.get_clock().now().to_msg(), data=0.02)))

    def mrm_operator(self):
        emergency = PACKAGE == 'autoware_mrm_emergency_stop_operator'
        mode = 'emergency_stop' if emergency else 'comfortable_stop'
        statuses = self.subscribe(MrmBehaviorStatus, self.private + '/output/mrm/' + mode + '/status')
        client = self.peer.create_client(OperateMrm, self.private + '/input/mrm/' + mode + '/operate')
        self.wait(client.service_is_ready)
        self.wait(lambda: statuses and statuses[-1].state == MrmBehaviorStatus.AVAILABLE)
        if emergency:
            commands = self.subscribe(Control, self.private + '/output/mrm/emergency_stop/control_cmd')
        else:
            limits = self.subscribe(VelocityLimit, self.private + '/output/velocity_limit', RETAINED)
            clears = self.subscribe(VelocityLimitClearCommand, self.private + '/output/velocity_limit/clear', RETAINED)
        for operate, expected in [(True, MrmBehaviorStatus.OPERATING), (False, MrmBehaviorStatus.AVAILABLE)]:
            future = client.call_async(OperateMrm.Request(operate=operate))
            self.wait(future.done)
            self.assertTrue(future.result().response.success)
            self.wait(lambda: statuses[-1].state == expected)
            if operate:
                if emergency:
                    self.wait(lambda: any(command.longitudinal.acceleration < 0 for command in commands))
                else:
                    self.wait(lambda: limits and limits[-1].max_velocity == 0.0)
            elif not emergency:
                self.wait(lambda: clears and clears[-1].command)

    def aggregator(self):
        structures = self.subscribe(DiagGraphStruct, self.private + '/struct', RETAINED)
        statuses = self.subscribe(DiagGraphStatus, self.private + '/status')
        self.wait(lambda: structures and statuses)
        self.assertIn('/autoware/modes/autonomous', [node.path for node in structures[-1].nodes])
        self.assertEqual(structures[-1].id, statuses[-1].id)
        self.assertEqual(len(structures[-1].nodes), len(statuses[-1].nodes))

    def mode_converter(self):
        messages = self.subscribe(OperationModeAvailability, self.private + '/operation_mode/availability')
        publisher = self.peer.create_publisher(CommandModeAvailability, self.private + '/command_mode/availability', 10)
        message = CommandModeAvailability(items=[CommandModeAvailabilityItem(mode=1002, available=True)])
        self.wait(lambda: messages and messages[-1].autonomous, lambda: publisher.publish(message))
        self.assertFalse(messages[-1].remote)
        message.items[0].available = False
        self.wait(lambda: messages and not messages[-1].autonomous, lambda: publisher.publish(message))

    def graph_consumer(self):
        structure = DiagGraphStruct(id='bazel', nodes=[DiagNodeStruct(path='/autoware/modes/autonomous', type='and')])
        status = DiagGraphStatus(id='bazel', nodes=[DiagNodeStatus(level=DiagnosticStatus.ERROR)])
        struct_pub = self.peer.create_publisher(DiagGraphStruct, '/diagnostics_graph/struct', RETAINED)
        status_pub = self.peer.create_publisher(DiagGraphStatus, '/diagnostics_graph/status', 10)
        if PACKAGE == 'autoware_hazard_status_converter':
            messages = self.subscribe(HazardStatusStamped, self.private + '/hazard_status')

            def condition():
                return messages and messages[-1].status.emergency
        elif EXECUTABLE == 'converter_node':
            messages = self.subscribe(DiagnosticArray, '/diagnostics_array')

            def condition():
                return any(s.name == '/autoware/modes/autonomous' and s.level == DiagnosticStatus.ERROR
                           for m in messages for s in m.status)
        elif EXECUTABLE == 'logging_node':
            messages = self.subscribe(StringStamped, self.private + '/debug/error_graph_text')

            def condition():
                return messages and '/autoware/modes/autonomous ERROR' in messages[-1].data
        else:
            # Dump exposes its graph table on stdout.

            def condition():
                text = self.output()
                return '/autoware/modes/autonomous' in text and 'ERROR' in text
        struct_pub.publish(structure)
        self.wait(lambda: struct_pub.get_subscription_count() > 0)
        self.wait(condition, lambda: status_pub.publish(status))


if __name__ == '__main__':
    unittest.main()
