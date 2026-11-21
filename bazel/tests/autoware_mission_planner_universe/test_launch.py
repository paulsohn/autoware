# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise route selection, goal echo, arrival and clearing through original XML."""
import unittest

from bazel.tests.autoware_mission_planner_universe.fixture import Peer, actions
from autoware_planning_msgs.msg import RouteState
from autoware_planning_msgs.srv import SetPreferredPrimitive
import launch
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import pytest
import rclpy


@pytest.mark.launch_test
def generate_test_description():
    return launch.LaunchDescription(actions() + [launch_testing.actions.ReadyToTest()])


class TestMission(unittest.TestCase):
    def test_route_arrival_and_clear(self):
        rclpy.init()
        peer = Peer(self)
        try:
            route = peer.set_route()
            client = peer.node.create_client(SetPreferredPrimitive,
                                             '/planning/mission_planning/mission_planner/set_preferred_primitive')
            # A structurally invalid request must leave the valid route unchanged.
            result = peer.call(client, SetPreferredPrimitive.Request(uuid=route.uuid))
            self.assertFalse(result.status.success)
            self.assertIn('size of preferred_primitives', result.status.message)
            peer.pose = peer.goal
            peer.wait(lambda: peer.states[-1].state == RouteState.ARRIVED, 'arrival state')
            peer.clear()
        finally:
            peer.node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_clean_exit(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
