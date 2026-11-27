# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Qualify published tracking metrics with deterministic straight-line feedback."""
import math
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_control_msgs.msg import Control
from autoware_control_performance_analysis.msg import DrivingMonitorStamped, ErrorStamped
from autoware_planning_msgs.msg import Trajectory, TrajectoryPoint
from autoware_vehicle_msgs.msg import SteeringReport
from nav_msgs.msg import Odometry
import rclpy

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())
PREFIX = '/control_performance_analysis'


class Metrics(unittest.TestCase):
    def test_tracking_error_and_recovery(self):
        rclpy.init()
        peer = rclpy.create_node('performance_peer')
        errors, driving = [], []
        subscriptions = [peer.create_subscription(ErrorStamped, PREFIX + '/output/error_stamped', errors.append, 10),
                         peer.create_subscription(DrivingMonitorStamped, PREFIX + '/output/driving_status_stamped', driving.append, 10)]
        trajectory_pub = peer.create_publisher(Trajectory, PREFIX + '/input/reference_trajectory', 1)
        control_pub = peer.create_publisher(Control, PREFIX + '/input/control_raw', 1)
        steer_pub = peer.create_publisher(SteeringReport, PREFIX + '/input/measured_steering', 1)
        odom_pub = peer.create_publisher(Odometry, PREFIX + '/input/odometry', 1)
        trajectory = Trajectory()
        trajectory.header.frame_id = 'map'
        for i in range(121):
            point = TrajectoryPoint()
            point.pose.position.x = i * 0.5
            point.pose.orientation.w = 1.0
            point.longitudinal_velocity_mps = 1.0
            trajectory.points.append(point)
        lateral = 0.0
        started = time.monotonic()

        def publish():
            stamp = peer.get_clock().now().to_msg()
            trajectory.header.stamp = stamp
            trajectory_pub.publish(trajectory)
            control = Control(stamp=stamp)
            control.longitudinal.stamp = control.lateral.stamp = stamp
            control.longitudinal.velocity = 1.0
            control_pub.publish(control)
            steer_pub.publish(SteeringReport(stamp=stamp))
            odom = Odometry()
            odom.header.stamp = stamp
            odom.header.frame_id = 'map'
            position = 15.0 + time.monotonic() - started
            odom.pose.pose.position.x = position
            odom.pose.pose.position.y = lateral
            odom.pose.pose.orientation.w = 1.0
            odom.twist.twist.linear.x = 1.0
            odom_pub.publish(odom)

        args = [EXECUTABLE, '--ros-args']
        for package, config in [('autoware_control_performance_analysis', 'control_performance_analysis.param.yaml'), ('autoware_test_utils', 'test_vehicle_info.param.yaml')]:
            args += ['--params-file', str(Path(get_package_share_directory(package)) / 'config' / config)]
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def wait_error(expected):
                    errors.clear()
                    deadline = time.monotonic() + 12
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        publish()
                        rclpy.spin_once(peer, timeout_sec=0.05)
                        for msg in errors:
                            if abs(msg.error.lateral_error - expected) < 0.15:
                                return msg.error
                    self.fail(f'Expected lateral error {expected} absent: {output()}')

                try:
                    baseline = wait_error(0.0)
                    self.assertAlmostEqual(baseline.heading_error, 0.0, places=6)
                    lateral = 1.0
                    displaced = wait_error(1.0)
                    self.assertAlmostEqual(displaced.value_approximation, 2.342, places=3)
                    self.assertAlmostEqual(displaced.curvature_estimate, 0.0, places=5)
                    self.assertAlmostEqual(displaced.vehicle_velocity_error, 0.0, places=5)
                    lateral = 0.0
                    wait_error(0.0)
                    self.assertTrue(driving)
                    for msg in errors:
                        self.assertTrue(all(math.isfinite(getattr(msg.error, name)) for name in msg.error.get_fields_and_field_types()))
                    for name in driving[-1].get_fields_and_field_types():
                        self.assertTrue(math.isfinite(getattr(driving[-1], name).data))
                    self.assertAlmostEqual(driving[-1].longitudinal_acceleration.data, 0.0, places=5)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Performance node shutdown timed out: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
