# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check lane selection against the real Universe mission planner."""
from copy import deepcopy
import unittest

from bazel.tests.autoware_mission_planner_universe.fixture import Peer, actions, include
from autoware_internal_debug_msgs.msg import Int32Stamped
from autoware_planning_msgs.msg import LaneletPrimitive
from autoware_planning_msgs.srv import SetLaneletRoute
import launch
from launch.actions import GroupAction
from launch_ros.actions import PushRosNamespace
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import pytest
import rclpy
from tier4_external_api_msgs.srv import SetPreferredLane
from tier4_planning_msgs.msg import RerouteAvailability


@pytest.mark.launch_test
def generate_test_description():
    return launch.LaunchDescription(actions() + [
        GroupAction(actions=[
            PushRosNamespace('planning/mission_planning'),
            include('autoware_manual_lane_change_handler', 'manual_lane_change_handler.launch.xml'),
        ]),
        launch_testing.actions.ReadyToTest(),
    ])


class TestLaneSelection(unittest.TestCase):
    def test_guards_shift_and_reset(self):
        rclpy.init()
        peer = Peer(self)
        try:
            shifts = []
            peer.node.create_subscription(Int32Stamped, '/planning/mission_planning/manual_lane_change_handler/debug/shift_number', shifts.append, 10)
            client = peer.node.create_client(SetPreferredLane, '/planning/mission_planning/manual_lane_change_handler/set_preferred_lane')
            request = SetPreferredLane.Request(lane_change_direction=SetPreferredLane.Request.LEFT)
            result = peer.call(client, request)
            self.assertFalse(result.status.success)
            self.assertEqual(result.status.message, 'No current route available.')
            original = peer.set_route()
            peer.clear()
            # The waypoint planner omits this solid-boundary neighbor. Supply explicit
            # route alternatives through the supported lanelet-route service instead.
            segments = deepcopy(original.segments)
            segments[3].primitives.append(LaneletPrimitive(id=10310, primitive_type='lane'))
            lanelet_client = peer.node.create_client(SetLaneletRoute, '/planning/set_lanelet_route')
            lanelet_request = SetLaneletRoute.Request(goal_pose=peer.goal, segments=segments, uuid=original.uuid)
            lanelet_request.header.frame_id = 'map'
            result = peer.call(lanelet_client, lanelet_request)
            self.assertTrue(result.status.success, result.status.message)
            peer.wait(lambda: any(p.id == 10310 for p in peer.routes[-1].segments[3].primitives),
                      'explicit lanelet alternatives')
            original = peer.routes[-1]
            # Lane 9178 has a road lane on its left (10310); lane 9102 has none.
            peer.pose.position.x, peer.pose.position.y = 3753.36, 73739.80
            peer.wait(lambda: shifts, 'manual handler received route')
            result = peer.call(client, request)
            self.assertFalse(result.status.success)
            self.assertIn('Not in lane driving state', result.status.message)
            reroute = peer.node.create_publisher(
                RerouteAvailability,
                '/planning/scenario_planning/lane_driving/behavior_planning/behavior_path_planner/output/is_reroute_available', 1)
            peer.wait(lambda: reroute.get_subscription_count() >= 2, 'reroute subscribers')
            publish = peer.publish

            def publish_ready():
                publish()
                reroute.publish(RerouteAvailability(availability=True))

            peer.publish = publish_ready

            def shift_when_ready():
                result = peer.call(client, request)
                if not result.status.success and result.status.message.startswith('Not in lane driving state'):
                    return False
                self.assertTrue(result.status.success, result.status.message)
                return True

            peer.wait(shift_when_ready, 'reroute availability delivered before shift')
            peer.wait(lambda: shifts[-1].data == -1 and
                      peer.routes[-1].segments[3].preferred_primitive.id == 10310,
                      'preferred primitive shift')
            self.assertEqual(peer.routes[-1].uuid, original.uuid)
            result = peer.call(client, SetPreferredLane.Request(lane_change_direction=SetPreferredLane.Request.AUTO))
            self.assertTrue(result.status.success, result.status.message)
            peer.wait(lambda: shifts[-1].data == 0 and
                      [s.preferred_primitive.id for s in peer.routes[-1].segments] ==
                      [s.preferred_primitive.id for s in original.segments], 'AUTO restored original route')
            peer.clear()
        finally:
            peer.node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_clean_exit(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
