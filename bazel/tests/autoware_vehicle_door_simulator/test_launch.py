# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check the original door launch's service and status contract."""
from pathlib import Path
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_adapi_v1_msgs.msg import DoorCommand, DoorLayout, DoorStatus, DoorStatusArray
from autoware_adapi_v1_msgs.srv import GetDoorLayout, SetDoorCommand
import launch
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import AnyLaunchDescriptionSource
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import pytest
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile


@pytest.mark.launch_test
def generate_test_description():
    source = Path(get_package_share_directory('autoware_vehicle_door_simulator'))
    return launch.LaunchDescription([
        IncludeLaunchDescription(AnyLaunchDescriptionSource(str(source / 'launch/vehicle_door_simulator.launch.xml'))),
        launch_testing.actions.ReadyToTest(),
    ])


class TestDoors(unittest.TestCase):
    def test_layout_commands_and_status(self):
        rclpy.init()
        node = rclpy.create_node('door_integration_peer')
        try:
            messages = []
            node.create_subscription(DoorStatusArray, '/vehicle/doors/status', messages.append,
                                     QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
            layout = node.create_client(GetDoorLayout, '/vehicle/doors/layout')
            command = node.create_client(SetDoorCommand, '/vehicle/doors/command')

            def wait(condition):
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    if condition():
                        return
                    rclpy.spin_once(node, timeout_sec=0.05)
                self.fail('Door simulator did not reach its service/status gate')

            def call(client, request):
                wait(client.service_is_ready)
                future = client.call_async(request)
                wait(future.done)
                self.assertIsNone(future.exception())
                return future.result()

            result = call(layout, GetDoorLayout.Request())
            self.assertTrue(result.status.success)
            self.assertEqual(len(result.doors), 4)
            self.assertEqual([list(x.roles) for x in result.doors],
                             [[], [DoorLayout.GET_ON], [DoorLayout.GET_ON, DoorLayout.GET_OFF], [DoorLayout.GET_OFF]])
            wait(lambda: messages)
            self.assertEqual([x.status for x in messages[-1].doors], [DoorStatus.CLOSED] * 4)
            result = call(command, SetDoorCommand.Request(doors=[DoorCommand(index=1, command=DoorCommand.OPEN)]))
            self.assertTrue(result.status.success)
            wait(lambda: messages[-1].doors[1].status == DoorStatus.OPENING)
            wait(lambda: messages[-1].doors[1].status == DoorStatus.OPENED)
            result = call(command, SetDoorCommand.Request(doors=[DoorCommand(index=99, command=DoorCommand.OPEN)]))
            self.assertFalse(result.status.success)
            self.assertEqual(result.status.message, 'invalid door index')
            result = call(command, SetDoorCommand.Request(doors=[DoorCommand(index=0, command=0)]))
            self.assertFalse(result.status.success)
            self.assertEqual(result.status.message, 'invalid door command')
            result = call(command, SetDoorCommand.Request(doors=[DoorCommand(index=1, command=DoorCommand.CLOSE)]))
            self.assertTrue(result.status.success)
            wait(lambda: messages[-1].doors[1].status == DoorStatus.CLOSING)
            wait(lambda: messages[-1].doors[1].status == DoorStatus.CLOSED)
            self.assertTrue(all(x.status == DoorStatus.CLOSED for x in messages[-1].doors))
        finally:
            node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_clean_exit(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
