# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise installed follower executables and the simple follower's ROS output."""
import math
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
from autoware_planning_msgs.msg import Trajectory, TrajectoryPoint
from nav_msgs.msg import Odometry
from rcl_interfaces.srv import GetParameters
import rclpy

PROFILE = sys.argv.pop(1)
PACKAGE = 'autoware_trajectory_follower_node'


class FollowerRuntime(unittest.TestCase):
    def test_installed_follower(self):
        rclpy.init()
        peer = rclpy.create_node('follower_peer')
        name = 'bazel_follower_' + str(os.getpid())
        prefix = '/' + name
        executable = 'controller_node_exe' if PROFILE == 'mpc' else 'simple_trajectory_follower_exe'
        binary = Path(get_package_prefix(PACKAGE)) / 'lib' / PACKAGE / executable
        self.assertTrue(binary.is_file(), binary)
        args = [str(binary), '--ros-args', '-r', '__node:=' + name]
        share = Path(get_package_share_directory(PACKAGE))
        if PROFILE == 'mpc':
            lateral = Path(get_package_share_directory('autoware_mpc_lateral_controller'))
            longitudinal = Path(get_package_share_directory('autoware_pid_longitudinal_controller'))
            for config in [lateral / 'param/lateral_controller_defaults.param.yaml',
                           lateral / 'param/steer_offset.param.yaml',
                           longitudinal / 'config/autoware_pid_longitudinal_controller.param.yaml',
                           share / 'test/test_vehicle_info.param.yaml',
                           share / 'test/test_nearest_search.param.yaml',
                           share / 'param/trajectory_follower_node.param.yaml']:
                args += ['--params-file', str(config)]
            args += ['-p', 'lateral_controller_mode:=mpc', '-p', 'longitudinal_controller_mode:=pid',
                     '-p', 'steer_offset_param_name:=steer_offset']
            parameter, expected = 'ctrl_period', 0.03
        else:
            # Values from the original simple follower launch defaults; its YAML needs launch substitutions.
            args += ['-p', 'use_external_target_vel:=true', '-p', 'external_target_vel:=5.0', '-p', 'lateral_deviation:=0.0']
            parameter, expected = 'external_target_vel', 5.0
            odometry_pub = peer.create_publisher(Odometry, prefix + '/input/kinematics', 1)
            trajectory_pub = peer.create_publisher(Trajectory, prefix + '/input/trajectory', 1)
            received = []
            subscription = peer.create_subscription(Control, prefix + '/output/control_cmd', received.append, 1)
        client = peer.create_client(GetParameters, prefix + '/get_parameters')
        try:
            with tempfile.TemporaryFile() as log:
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    return os.pread(log.fileno(), os.fstat(log.fileno()).st_size, 0).decode(errors='replace')

                def wait(condition):
                    deadline = time.monotonic() + 25
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if condition():
                            return
                        rclpy.spin_once(peer, timeout_sec=0.05)
                    self.fail('Follower timed out: ' + output())

                try:
                    wait(client.service_is_ready)
                    future = client.call_async(GetParameters.Request(names=['use_sim_time', parameter]))
                    wait(future.done)
                    self.assertIsNone(future.exception())
                    values = future.result().values
                    self.assertEqual(len(values), 2, output())
                    self.assertFalse(values[0].bool_value)
                    self.assertAlmostEqual(values[1].double_value, expected)
                    maps = Path('/proc', str(process.pid), 'maps').read_text()
                    component = 'controller' if PROFILE == 'mpc' else 'simple'
                    self.assertIn('lib' + PACKAGE + '_' + component + '_component.so', maps)
                    self.assertNotIn('libagnocast_heaphook.so', maps)
                    if PROFILE == 'simple':
                        wait(lambda: odometry_pub.get_subscription_count() > 0 and trajectory_pub.get_subscription_count() > 0
                             and subscription.get_publisher_count() > 0)
                        odometry = Odometry()
                        odometry.header.frame_id = 'map'
                        odometry.child_frame_id = 'base_link'
                        odometry.pose.pose.orientation.w = 1.0
                        odometry.twist.twist.linear.x = 1.0
                        trajectory = Trajectory()
                        trajectory.header.frame_id = 'map'
                        for position in range(25):
                            point = TrajectoryPoint()
                            point.pose.position.x = float(position)
                            point.pose.orientation.w = 1.0
                            point.longitudinal_velocity_mps = 2.0
                            trajectory.points.append(point)

                        def publish_and_observe():
                            stamp = peer.get_clock().now().to_msg()
                            odometry.header.stamp = trajectory.header.stamp = stamp
                            odometry_pub.publish(odometry)
                            trajectory_pub.publish(trajectory)
                            return len(received) >= 3

                        wait(publish_and_observe)
                        for message in received[-3:]:
                            self.assertAlmostEqual(message.longitudinal.velocity, 5.0)
                            self.assertAlmostEqual(message.lateral.steering_tire_angle, 0.0)
                            self.assertTrue(math.isfinite(message.longitudinal.acceleration))
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
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
