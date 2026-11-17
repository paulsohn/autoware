# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check executable initialization transactions through ordinary ROS peers."""

import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from autoware_adapi_v1_msgs.msg import LocalizationInitializationState as State
from autoware_internal_localization_msgs.srv import PoseWithCovarianceStamped as Align
from autoware_localization_msgs.srv import InitializeLocalization as Initialize
from geometry_msgs.msg import PoseWithCovarianceStamped
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile
from std_srvs.srv import SetBool

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())


class PoseInitializerRuntime(unittest.TestCase):
    def test_initialization_transactions(self):
        os.environ['ROS_DOMAIN_ID'] = '102'
        environment = dict(os.environ, ENABLE_AGNOCAST=os.environ['AUTOWARE_TEST_AGNOCAST'])
        if environment['ENABLE_AGNOCAST'] == '1':
            environment['LD_PRELOAD'] = environment['AGNOCAST_TEST_HEAPHOOK']
        else:
            environment.pop('LD_PRELOAD', None)
        rclpy.init()
        node = rclpy.create_node('pose_initializer_peer')
        triggers, alignments, resets, states = [], [], [], []

        def trigger(request, response):
            triggers.append(request.data)
            response.success = True
            return response

        def align(request, response):
            alignments.append(request)
            response.success = True
            response.reliable = True
            response.pose_with_covariance = request.pose_with_covariance
            response.pose_with_covariance.pose.pose.position.x += 1.0
            return response

        node.create_service(SetBool, 'ekf_trigger_node', trigger)
        node.create_service(SetBool, 'ndt_trigger_node', trigger)
        node.create_service(Align, 'ndt_align', align)
        node.create_subscription(PoseWithCovarianceStamped, 'pose_reset', resets.append, 10)
        node.create_subscription(State, '/localization/initialization_state', states.append,
                                 QoSProfile(depth=10, durability=DurabilityPolicy.TRANSIENT_LOCAL))
        client = node.create_client(Initialize, '/localization/initialize')
        parameters = {'ekf_enabled': True, 'ndt_enabled': True, 'gnss_enabled': False,
                      'yabloc_enabled': False, 'stop_check_enabled': False,
                      'pose_error_check_enabled': False, 'user_defined_initial_pose.enable': False,
                      'output_pose_covariance': [0.01] * 36, 'gnss_particle_covariance': [0.02] * 36}
        try:
            with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryFile(mode='w+') as log:
                path = Path(directory) / 'parameters.yaml'
                path.write_text(json.dumps({'/**': {'ros__parameters': parameters}}))
                process = subprocess.Popen([EXECUTABLE, '--ros-args', '--params-file', str(path)],
                                           env=environment, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def wait(predicate):
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if predicate():
                            return
                        rclpy.spin_once(node, timeout_sec=0.1)
                    self.fail('Initialization timed out: ' + output())

                try:
                    wait(lambda: client.service_is_ready() and states and
                         len(node.get_subscriptions_info_by_topic('pose_reset')) >= 1)
                    self.assertEqual(states[-1].state, State.UNINITIALIZED)
                    for method, x, expected in [(Initialize.Request.DIRECT, 10.0, 10.0),
                                                 (Initialize.Request.AUTO, 20.0, 21.0)]:
                        start = len(resets)
                        pose = PoseWithCovarianceStamped()
                        pose.header.frame_id = 'map'
                        pose.pose.pose.position.x = x
                        pose.pose.pose.orientation.w = 1.0
                        request = Initialize.Request(method=method, pose_with_covariance=[pose])
                        future = client.call_async(request)
                        wait(future.done)
                        self.assertTrue(future.result().status.success, output())
                        wait(lambda: len(resets) > start and states[-1].state == State.INITIALIZED)
                        self.assertEqual(resets[-1].header.frame_id, 'map')
                        self.assertAlmostEqual(resets[-1].pose.pose.position.x, expected)
                        self.assertEqual(list(resets[-1].pose.covariance), [0.01] * 36)
                    self.assertEqual(triggers, [False, False, True, True] * 2)
                    self.assertEqual(len(alignments), 1)
                    future = client.call_async(Initialize.Request(method=Initialize.Request.DIRECT))
                    wait(future.done)
                    self.assertFalse(future.result().status.success)
                    wait(lambda: states[-1].state == State.UNINITIALIZED)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Pose initializer failed to stop: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
