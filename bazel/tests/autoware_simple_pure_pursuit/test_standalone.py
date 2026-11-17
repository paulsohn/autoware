# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check the native controller executable with ordinary odometry/trajectory peers."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python import get_package_share_directory
from autoware_control_msgs.msg import Control
from autoware_planning_msgs.msg import Trajectory, TrajectoryPoint
from nav_msgs.msg import Odometry
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())


class PurePursuitRuntime(unittest.TestCase):
    def test_tracking_and_stopping(self):
        os.environ['ROS_DOMAIN_ID'] = '63'
        rclpy.init()
        node = rclpy.create_node('pure_pursuit_peer')
        commands = []
        qos = QoSProfile(depth=10, durability=DurabilityPolicy.TRANSIENT_LOCAL)
        node.create_subscription(Control, '/simple_pure_pursuit/output/control_command', commands.append, qos)
        odom_pub = node.create_publisher(Odometry, '/simple_pure_pursuit/input/odometry', 10)
        traj_pub = node.create_publisher(Trajectory, '/simple_pure_pursuit/input/trajectory', 10)
        vehicle = str(Path(get_package_share_directory('autoware_test_utils')) /
                      'config/test_vehicle_info.param.yaml')
        parameters = str(Path(get_package_share_directory('autoware_simple_pure_pursuit')) /
                         'config/simple_pure_pursuit.param.yaml')
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen([EXECUTABLE, '--ros-args', '--params-file', vehicle,
                                            '--params-file', parameters, '-r', '__node:=simple_pure_pursuit'],
                                           stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                try:
                    for case, x, y, empty, velocity, acceleration in [
                        (1, 0.0, 0.0, False, 1.0, 1.0),
                        (2, 6.0, 0.0, False, 0.0, -10.0),
                        (3, 0.0, 0.0, True, 0.0, 0.0),
                        (4, 0.0, 1.0, False, 1.0, 1.0),
                    ]:
                        odom = Odometry()
                        odom.header.frame_id = 'map'
                        odom.header.stamp.sec = case
                        odom.pose.pose.position.x, odom.pose.pose.position.y = x, y
                        odom.pose.pose.orientation.w = 1.0
                        trajectory = Trajectory(header=odom.header)
                        if not empty:
                            for i in range(7):
                                point = TrajectoryPoint(longitudinal_velocity_mps=1.0)
                                point.pose.position.x = float(i)
                                point.pose.orientation.w = 1.0
                                trajectory.points.append(point)
                        deadline = time.monotonic() + 15
                        while time.monotonic() < deadline:
                            self.assertIsNone(process.poll(), output())
                            odom_pub.publish(odom)
                            traj_pub.publish(trajectory)
                            rclpy.spin_once(node, timeout_sec=0.1)
                            if commands and commands[-1].stamp.sec == case:
                                break
                        self.assertTrue(commands and commands[-1].stamp.sec == case, output())
                        command = commands[-1]
                        self.assertAlmostEqual(command.longitudinal.velocity, velocity, places=4)
                        self.assertAlmostEqual(command.longitudinal.acceleration, acceleration, places=4)
                        if case == 4:
                            self.assertLess(command.lateral.steering_tire_angle, 0.0)
                        else:
                            self.assertAlmostEqual(command.lateral.steering_tire_angle, 0.0, places=4)
                        if case == 2:
                            self.assertTrue(command.longitudinal.is_defined_acceleration)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Controller failed to stop: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
