# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise online estimates and off/manual/auto calibration persistence."""
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_internal_debug_msgs.msg import Float32Stamped
from autoware_vehicle_msgs.msg import SteeringReport
from geometry_msgs.msg import PoseStamped
from std_srvs.srv import Trigger
import rclpy
import yaml

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())
PREFIX = '/steer_offset_estimator'


class SteeringEstimator(unittest.TestCase):
    def test_calibration_modes(self):
        for mode in ['off', 'manual', 'auto']:
            with self.subTest(mode=mode):
                self.run_mode(mode)

    def run_mode(self, mode):
        rclpy.init()
        peer = rclpy.create_node('steering_estimator_peer')
        offsets, covariances = [], []
        subscriptions = [peer.create_subscription(Float32Stamped, PREFIX + '/output/steering_offset', offsets.append, 10),
                         peer.create_subscription(Float32Stamped, PREFIX + '/output/steering_offset_covariance', covariances.append, 10)]
        pose_pub = peer.create_publisher(PoseStamped, PREFIX + '/input/pose', 10)
        steer_pub = peer.create_publisher(SteeringReport, PREFIX + '/input/steer', 10)
        client = peer.create_client(Trigger, PREFIX + '/trigger_steer_offset_calibration')
        started = time.monotonic()
        feeding = False

        def publish():
            if not feeding:
                return
            stamp = peer.get_clock().now().to_msg()
            pose = PoseStamped()
            pose.header.stamp = stamp
            pose.header.frame_id = 'map'
            pose.pose.orientation.w = 1.0
            pose.pose.position.x = 5.0 * (time.monotonic() - started)
            pose_pub.publish(pose)
            steer_pub.publish(SteeringReport(stamp=stamp, steering_tire_angle=0.01))

        try:
            with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryFile(mode='w+') as log:
                estimate_file = Path(directory) / 'estimate.yaml'
                calibration_file = Path(directory) / 'calibration.yaml'
                estimate_file.write_text('/**:\n  ros__parameters:\n    steer_offset: 0.0\n')
                calibration_file.write_text('/**:\n  ros__parameters:\n    residual_steering_offset: 0.0\n')
                args = [EXECUTABLE, '--ros-args']
                for package, config in [('autoware_steer_offset_estimator', 'steer_offset_estimator.param.yaml'), ('autoware_test_utils', 'test_vehicle_info.param.yaml')]:
                    args += ['--params-file', str(Path(get_package_share_directory(package)) / 'config' / config)]
                parameters = {'calibration.mode': '"' + mode + '"', 'calibration.min_steady_duration': '0.0',
                              'calibration.covariance_th': '0.1', 'calibration.min_update_interval': '100.0' if mode != 'auto' else '0.0',
                              'steer_offset_param_path': str(estimate_file), 'steer_offset_param_name': 'steer_offset',
                              'calibration_file_path': str(calibration_file), 'calibration_param_name': 'residual_steering_offset', 'steer_offset': '0.0'}
                for key, value in parameters.items():
                    args += ['-p', key + ':=' + value]
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def wait(predicate):
                    deadline = time.monotonic() + 12
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        publish()
                        rclpy.spin_once(peer, timeout_sec=0.02)
                        if predicate():
                            return
                    self.fail('Steering estimator condition absent: ' + output())

                def trigger():
                    wait(client.service_is_ready)
                    future = client.call_async(Trigger.Request())
                    wait(future.done)
                    return future.result()

                def read(path, name):
                    return yaml.safe_load(path.read_text())['/**']['ros__parameters'][name]

                try:
                    self.assertFalse(trigger().success)
                    feeding = True
                    wait(lambda: bool(offsets) and offsets[-1].data < -0.008 and bool(covariances) and 0 <= covariances[-1].data < 0.1)
                    self.assertAlmostEqual(offsets[-1].data, -0.01, delta=0.002)
                    self.assertLess(read(estimate_file, 'steer_offset'), -0.005)
                    if mode == 'off':
                        self.assertFalse(trigger().success)
                        self.assertEqual(read(calibration_file, 'residual_steering_offset'), 0.0)
                    elif mode == 'manual':
                        self.assertEqual(read(calibration_file, 'residual_steering_offset'), 0.0)
                        self.assertTrue(trigger().success)
                        first = read(calibration_file, 'residual_steering_offset')
                        self.assertAlmostEqual(first, -0.01, delta=0.002)
                        self.assertTrue(trigger().success)
                        self.assertAlmostEqual(read(calibration_file, 'residual_steering_offset'), first - 0.01, delta=0.002)
                    else:
                        wait(lambda: read(calibration_file, 'residual_steering_offset') < 0)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Steering estimator shutdown timed out: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
