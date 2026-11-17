# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0

"""Validate the real smoother pipeline using ordinary ROS inputs and outputs."""

import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_adapi_v1_msgs.msg import OperationModeState
from autoware_internal_planning_msgs.msg import VelocityLimit
from autoware_planning_msgs.msg import Trajectory, TrajectoryPoint
from geometry_msgs.msg import AccelWithCovarianceStamped
from nav_msgs.msg import Odometry
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())
EXPECTED_ENABLED = sys.argv.pop(1)
ENABLED = EXPECTED_ENABLED == '1'


class SmootherRuntime(unittest.TestCase):
    def exercise(self, algorithm):
        self.assertEqual(os.environ.get('ENABLE_AGNOCAST'), EXPECTED_ENABLED)
        os.environ['ROS_DOMAIN_ID'] = '68'
        rclpy.init()
        node = rclpy.create_node('velocity_smoother_peer')
        namespace = f'/bazel_smoother_{os.getpid()}_{algorithm.lower()}'
        prefix = namespace + '/velocity_smoother'
        durable = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
        received, limits = [], []
        traj_pub = node.create_publisher(Trajectory, prefix + '/input/trajectory', 10)
        odom_pub = node.create_publisher(Odometry, '/localization/kinematic_state', 10)
        acc_pub = node.create_publisher(
            AccelWithCovarianceStamped, prefix + '/input/acceleration', 10
        )
        limit_pub = node.create_publisher(
            VelocityLimit, prefix + '/input/external_velocity_limit_mps', durable
        )
        mode_pub = node.create_publisher(
            OperationModeState, prefix + '/input/operation_mode_state', durable
        )
        node.create_subscription(Trajectory, prefix + '/output/trajectory',
                                 received.append, 10)
        node.create_subscription(VelocityLimit, prefix + '/output/current_velocity_limit_mps',
                                 limits.append, durable)
        args = [EXECUTABLE, '--ros-args', '-r', '__ns:=' + namespace]
        for package, files in [
            ('autoware_test_utils', ['test_common.param.yaml', 'test_nearest_search.param.yaml',
                                     'test_vehicle_info.param.yaml']),
            ('autoware_velocity_smoother', ['default_velocity_smoother.param.yaml',
                                          'default_common.param.yaml', algorithm + '.param.yaml']),
        ]:
            share = Path(get_package_share_directory(package))
            for name in files:
                args += ['--params-file', str(share / 'config' / name)]
        args += ['-p', 'algorithm_type:=' + algorithm, '-p', 'publish_debug_trajs:=false']
        env = dict(os.environ)
        env.pop('LD_PRELOAD', None)
        if ENABLED:
            hook = env['AGNOCAST_TEST_HEAPHOOK']
            self.assertTrue(Path(hook).is_file())
            env['LD_PRELOAD'] = hook
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(args, env=env, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def wait(condition, publish=None):
                    deadline = time.monotonic() + 25
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if condition():
                            return
                        if publish:
                            publish()
                        rclpy.spin_once(node, timeout_sec=0.05)
                    self.fail(algorithm + ' timed out: ' + output())

                try:
                    wait(lambda: traj_pub.get_subscription_count() > 0 and
                         odom_pub.get_subscription_count() > 0 and
                         acc_pub.get_subscription_count() > 0 and
                         limit_pub.get_subscription_count() > 0 and
                         mode_pub.get_subscription_count() > 0 and
                         node.count_publishers(prefix + '/output/trajectory') > 0)
                    maps = Path('/proc', str(process.pid), 'maps').read_text()
                    self.assertEqual('libagnocast_heaphook.so' in maps, ENABLED)
                    self.assertNotIn('libosqp.so', maps)
                    trajectory = Trajectory()
                    trajectory.header.frame_id = 'map'
                    trajectory.header.stamp = node.get_clock().now().to_msg()
                    for i in range(101):
                        point = TrajectoryPoint()
                        point.pose.position.x = float(i)
                        point.pose.orientation.w = 1.0
                        point.longitudinal_velocity_mps = 5.0 if i < 100 else 0.0
                        trajectory.points.append(point)
                    odom = Odometry()
                    odom.header.frame_id = 'map'
                    odom.pose.pose.orientation.w = 1.0
                    odom.twist.twist.linear.x = 2.0
                    acceleration = AccelWithCovarianceStamped()
                    acceleration.header.frame_id = 'base_link'
                    mode = OperationModeState()
                    mode.mode = OperationModeState.AUTONOMOUS
                    mode.is_autoware_control_enabled = True
                    limit = VelocityLimit()
                    limit.max_velocity = 3.0
                    limit.sender = 'bazel_velocity_smoother'

                    def publish():
                        odom_pub.publish(odom)
                        acc_pub.publish(acceleration)
                        mode_pub.publish(mode)
                        limit_pub.publish(limit)
                        traj_pub.publish(trajectory)

                    wait(lambda: any(m.sender == limit.sender and m.max_velocity == 3.0
                                     for m in limits), publish)
                    # Check a new request after the limit is acknowledged, not an older output.
                    trajectory.header.stamp = node.get_clock().now().to_msg()
                    wait(lambda: any(m.header == trajectory.header for m in received), publish)
                    actual = next(m for m in received if m.header == trajectory.header)
                    self.assertGreater(len(actual.points), 2)
                    self.assertEqual(actual.points[-1].longitudinal_velocity_mps, 0.0)
                    velocities = [p.longitudinal_velocity_mps for p in actual.points]
                    self.assertTrue(all(math.isfinite(v) and v >= 0.0 for v in velocities))
                    self.assertGreater(max(velocities), 0.0)
                    self.assertLessEqual(max(velocities), 3.02)
                    self.assertTrue(all(math.isfinite(p.acceleration_mps2) for p in actual.points))
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
            node.destroy_node()
            rclpy.shutdown()

    def test_jerk_filtered(self):
        self.exercise('JerkFiltered')

    def test_l2(self):
        self.exercise('L2')

    def test_linf(self):
        self.exercise('Linf')

    def test_analytical(self):
        self.exercise('Analytical')


if __name__ == '__main__':
    unittest.main()
