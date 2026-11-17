# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check API and adaptor transactions through the original assembled launch."""

import os
from pathlib import Path
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_adapi_version_msgs.srv import InterfaceVersion
from autoware_adapi_v1_msgs.msg import LocalizationInitializationState, RouteState as ApiRouteState
from autoware_adapi_v1_msgs.srv import InitializeLocalization as ApiInitialize
from autoware_adapi_v1_msgs.srv import SetRoutePoints
from autoware_localization_msgs.srv import InitializeLocalization
from autoware_planning_msgs.msg import RouteState
from autoware_planning_msgs.srv import ClearRoute, SetWaypointRoute
from geometry_msgs.msg import PoseStamped, PoseWithCovarianceStamped
import launch
from launch.actions import IncludeLaunchDescription, SetLaunchConfiguration
from launch.launch_description_sources import AnyLaunchDescriptionSource
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import pytest
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile


@pytest.mark.launch_test
def generate_test_description():
    source = str(Path(get_package_share_directory('autoware_core_api')) /
                 'launch/autoware_core_api.launch.xml')
    return launch.LaunchDescription([
        SetLaunchConfiguration('agnocast_heaphook_path', os.environ.get('AGNOCAST_TEST_HEAPHOOK', '')),
        # Vector-map mode starts without a pointcloud-loader parameter service.
        SetLaunchConfiguration('rviz_initial_pose_auto_fix_target', 'vector_map'),
        IncludeLaunchDescription(AnyLaunchDescriptionSource(source)),
        launch_testing.actions.ReadyToTest(),
    ])


class TestTransactions(unittest.TestCase):
    def test_api_and_adaptors(self, proc_info):
        rclpy.init()
        node = rclpy.create_node('api_integration_peer')
        try:
            durable = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
            poses, routes, clears, states, route_states = [], [], [], [], []

            def initialize(req, res):
                poses.append(req)
                res.status.success = True
                res.status.code = 0
                res.status.message = 'initialized by integration peer'
                return res

            def set_route(req, res):
                routes.append(req)
                res.status.success = True
                res.status.code = 0
                res.status.message = 'routed by integration peer'
                return res

            def clear_route(req, res):
                clears.append(req)
                res.status.success = True
                return res

            node.create_service(InitializeLocalization, '/localization/initialize', initialize)
            node.create_service(SetWaypointRoute, '/planning/set_waypoint_route', set_route)
            node.create_service(ClearRoute, '/planning/clear_route', clear_route)
            localization_pub = node.create_publisher(
                LocalizationInitializationState, '/localization/initialization_state', durable)
            route_pub = node.create_publisher(RouteState, '/planning/route_state', durable)
            initial_pub = node.create_publisher(PoseWithCovarianceStamped, '/initialpose', 10)
            goal_pub = node.create_publisher(PoseStamped, '/planning/mission_planning/goal', 10)
            node.create_subscription(LocalizationInitializationState,
                                     '/api/localization/initialization_state', states.append, durable)
            node.create_subscription(ApiRouteState, '/api/routing/state', route_states.append, durable)
            version = node.create_client(InterfaceVersion, '/api/interface/version')
            api_initialize = node.create_client(ApiInitialize, '/api/localization/initialize')
            api_route = node.create_client(SetRoutePoints, '/api/routing/set_route_points')

            def wait(condition, publish=None):
                deadline = time.monotonic() + 25
                while time.monotonic() < deadline:
                    if condition():
                        return
                    if publish:
                        publish()
                    rclpy.spin_once(node, timeout_sec=0.05)
                self.fail('API transaction timed out')

            def call(client, req):
                wait(client.service_is_ready)
                future = client.call_async(req)
                wait(future.done)
                self.assertIsNone(future.exception())
                return future.result()

            response = call(version, InterfaceVersion.Request())
            self.assertEqual((response.major, response.minor, response.patch), (1, 9, 1))
            state = LocalizationInitializationState()
            state.state = LocalizationInitializationState.INITIALIZED
            state.stamp = node.get_clock().now().to_msg()
            wait(lambda: any(s == state for s in states), lambda: localization_pub.publish(state))
            pose = PoseWithCovarianceStamped()
            pose.header.frame_id = 'map'
            pose.pose.pose.orientation.w = 1.0
            pose.pose.pose.position.x = 12.0
            request = ApiInitialize.Request(pose=[pose])
            response = call(api_initialize, request)
            self.assertTrue(response.status.success)
            self.assertEqual(response.status.message, 'initialized by integration peer')
            self.assertEqual(poses[-1].method, InitializeLocalization.Request.AUTO)
            self.assertEqual(poses[-1].pose_with_covariance, [pose])
            route_state = RouteState()
            route_state.state = RouteState.UNSET
            route_state.stamp = node.get_clock().now().to_msg()
            wait(lambda: any(s.state == ApiRouteState.UNSET for s in route_states),
                 lambda: route_pub.publish(route_state))
            goal = PoseStamped()
            goal.header.frame_id = 'map'
            goal.pose.orientation.w = 1.0
            goal.pose.position.x = 42.0
            request = SetRoutePoints.Request()
            request.header = goal.header
            request.goal = goal.pose
            response = call(api_route, request)
            self.assertTrue(response.status.success)
            self.assertEqual(response.status.message, 'routed by integration peer')
            self.assertEqual(routes[-1].goal_pose, goal.pose)
            before = len(routes)
            wait(lambda: goal_pub.get_subscription_count() > 0)
            goal.pose.position.x = 84.0
            goal_pub.publish(goal)
            wait(lambda: len(routes) > before)
            self.assertEqual(routes[-1].goal_pose, goal.pose)
            self.assertFalse(routes[-1].allow_modification)
            before = len(poses)
            wait(lambda: initial_pub.get_subscription_count() > 0)
            initial_pub.publish(pose)
            wait(lambda: len(poses) > before)
            self.assertEqual(poses[-1].pose_with_covariance[0].pose.pose, pose.pose.pose)
            covariance = poses[-1].pose_with_covariance[0].pose.covariance
            self.assertEqual([covariance[i] for i in [0, 7, 14, 21, 28, 35]],
                             [4.0, 4.0, 0.01, 0.01, 0.01, 1.0])
            self.assertTrue(all(v == 0.0 for i, v in enumerate(covariance)
                                if i not in [0, 7, 14, 21, 28, 35]))
            enabled = os.environ['ENABLE_AGNOCAST'] == '1'
            processes = proc_info.processes()
            self.assertEqual(len(processes), 5 if enabled else 3)
            for process in processes:
                maps = Path('/proc', str(process.process_details['pid']), 'maps').read_text()
                self.assertEqual('libagnocast_heaphook.so' in maps, enabled)
        finally:
            node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
