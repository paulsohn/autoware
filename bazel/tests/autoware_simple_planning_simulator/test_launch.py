# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the original simulator launch with declared parameter resources."""
from pathlib import Path
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_control_msgs.msg import Control
from autoware_vehicle_msgs.msg import GearCommand
from geometry_msgs.msg import PoseWithCovarianceStamped
import launch
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
import launch_testing
import launch_testing.actions
import launch_testing.asserts
from nav_msgs.msg import Odometry
import pytest
from rcl_interfaces.srv import GetParameters
import rclpy


@pytest.mark.launch_test
def generate_test_description():
    source = Path(get_package_share_directory('autoware_simple_planning_simulator'))
    vehicle = Path(get_package_share_directory('autoware_sample_vehicle_description'))
    return launch.LaunchDescription([
        IncludeLaunchDescription(PythonLaunchDescriptionSource(str(source / 'launch/simple_planning_simulator.launch.py')),
                                 launch_arguments={'initial_engage_state': 'true',
                                                   'vehicle_model_pkg': str(vehicle),
                                                   'vehicle_info_param_file': str(vehicle / 'config/vehicle_info.param.yaml'),
                                                   'motion_publish_mode': 'full_motion'}.items()),
        launch_testing.actions.ReadyToTest(),
    ])


class TestSimulator(unittest.TestCase):
    def test_original_launch_and_motion(self):
        rclpy.init()
        node = rclpy.create_node('simulator_integration_peer')
        try:
            received = []
            node.create_subscription(Odometry, '/localization/kinematic_state', received.append, 10)
            initial = node.create_publisher(PoseWithCovarianceStamped, '/initialpose3d', 10)
            control = node.create_publisher(Control, '/control/command/control_cmd', 10)
            gear = node.create_publisher(GearCommand, '/control/command/gear_cmd', 10)

            def wait(condition, publish=None):
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline:
                    if condition():
                        return
                    if publish:
                        publish()
                    rclpy.spin_once(node, timeout_sec=0.05)
                self.fail('Original simulator launch did not reach its motion/resource gate')

            client = node.create_client(GetParameters, '/simulation/simple_planning_simulator/get_parameters')
            wait(client.service_is_ready)
            future = client.call_async(GetParameters.Request(names=['wheel_base', 'vehicle_model_type']))
            wait(future.done)
            self.assertIsNone(future.exception())
            self.assertEqual(future.result().values[0].double_value, 2.79)
            self.assertEqual(future.result().values[1].string_value, 'DELAY_STEER_ACC_GEARED')
            wait(lambda: initial.get_subscription_count() and control.get_subscription_count() and gear.get_subscription_count())
            pose = PoseWithCovarianceStamped()
            pose.header.frame_id = 'map'
            pose.pose.pose.orientation.w = 1.0

            def initialize():
                pose.header.stamp = node.get_clock().now().to_msg()
                initial.publish(pose)

            wait(lambda: received, initialize)
            self.assertAlmostEqual(received[-1].pose.pose.position.x, 0.0, places=3)
            self.assertEqual(received[-1].header.frame_id, 'map')
            self.assertEqual(received[-1].child_frame_id, 'base_link')
            command = Control()
            command.longitudinal.velocity = 3.0
            command.longitudinal.acceleration = 1.0
            drive = GearCommand(command=GearCommand.DRIVE)

            def send():
                command.stamp = drive.stamp = node.get_clock().now().to_msg()
                gear.publish(drive)
                control.publish(command)

            wait(lambda: received[-1].pose.pose.position.x > 1.0, send)
            self.assertGreater(received[-1].twist.twist.linear.x, 0.1)
            self.assertLess(abs(received[-1].pose.pose.position.y), 0.01)
            command.longitudinal.velocity = 0.0
            command.longitudinal.acceleration = -3.0
            wait(lambda: abs(received[-1].twist.twist.linear.x) < 0.05, send)
        finally:
            node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_clean_exit(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
