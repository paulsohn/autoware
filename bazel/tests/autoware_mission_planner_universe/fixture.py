# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Original map/mission launches and a bounded ROS service peer."""
from pathlib import Path
import time

from ament_index_python.packages import get_package_share_directory
from autoware_adapi_v1_msgs.msg import OperationModeState
from autoware_planning_msgs.msg import LaneletRoute, RouteState
from autoware_planning_msgs.srv import ClearRoute, SetWaypointRoute
from geometry_msgs.msg import Pose, PoseStamped
from launch.actions import GroupAction, IncludeLaunchDescription
from launch.launch_description_sources import AnyLaunchDescriptionSource
from launch_ros.actions import PushRosNamespace, SetParametersFromFile
from nav_msgs.msg import Odometry
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile


def share(package):
    return Path(get_package_share_directory(package))


def include(package, filename, arguments=None):
    return IncludeLaunchDescription(
        AnyLaunchDescriptionSource(str(share(package) / 'launch' / filename)),
        launch_arguments=(arguments or {}).items())


def actions():
    lanelet_map = str(share('autoware_test_utils') / 'test_map/lanelet2_map.osm')
    return [
        include('autoware_core_map', 'autoware_core_map.launch.xml', {
            'lanelet2_map_path': lanelet_map,
            'map_projector_info_path': str(Path(__file__).parent / 'projector.yaml'),
            'pointcloud_map_path': str(Path(__file__).parent / 'single.pcd'),
            'pointcloud_map_metadata_path': str(Path(__file__).parent / 'absent.yaml'),
        }),
        GroupAction(actions=[
            PushRosNamespace('planning/mission_planning'),
            SetParametersFromFile(str(share('autoware_test_utils') / 'config/test_vehicle_info.param.yaml')),
            include('autoware_mission_planner_universe', 'mission_planner.launch.xml'),
            include('autoware_mission_planner_universe', 'goal_pose_visualizer.launch.xml'),
        ]),
    ]


class Peer:
    def __init__(self, case):
        self.case = case
        self.node = rclpy.create_node('mission_integration_peer')
        self.routes, self.public_routes, self.states, self.goals = [], [], [], []
        self.durable = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
        self.node.create_subscription(LaneletRoute, '/planning/mission_planning/route', self.routes.append, self.durable)
        self.node.create_subscription(LaneletRoute, '/planning/route', self.public_routes.append, self.durable)
        self.node.create_subscription(RouteState, '/planning/route_state', self.states.append, self.durable)
        self.node.create_subscription(PoseStamped, '/planning/mission_planning/echo_back_goal_pose', self.goals.append, self.durable)
        self.odom = self.node.create_publisher(Odometry, '/localization/kinematic_state', 10)
        self.mode = self.node.create_publisher(OperationModeState, '/system/operation_mode/state', self.durable)
        self.pose = Pose()
        self.pose.position.x, self.pose.position.y = 3717.239501953125, 73720.84375
        self.pose.orientation.z, self.pose.orientation.w = 0.24122096, 0.97047022
        self.goal = Pose()
        self.goal.position.x, self.goal.position.y = 3810.24951171875, 73769.2578125
        self.goal.orientation.z, self.goal.orientation.w = 0.23908402523702438, 0.9709988820160721

    def publish(self):
        odom = Odometry()
        odom.header.frame_id = 'map'
        odom.header.stamp = self.node.get_clock().now().to_msg()
        odom.pose.pose = self.pose
        self.odom.publish(odom)
        self.mode.publish(OperationModeState(mode=OperationModeState.STOP))

    def wait(self, condition, label):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if condition():
                return
            self.publish()
            rclpy.spin_once(self.node, timeout_sec=0.05)
        self.case.fail('Mission planning did not reach: ' + label)

    def call(self, client, request):
        self.wait(client.service_is_ready, client.srv_name)
        future = client.call_async(request)
        self.wait(future.done, client.srv_name + ' response')
        self.case.assertIsNone(future.exception())
        return future.result()

    def set_route(self):
        self.wait(lambda: self.states and self.states[-1].state == RouteState.UNSET, 'ready UNSET state')
        client = self.node.create_client(SetWaypointRoute, '/planning/set_waypoint_route')
        request = SetWaypointRoute.Request(goal_pose=self.goal)
        request.header.frame_id = 'map'
        result = self.call(client, request)
        self.case.assertTrue(result.status.success, result.status.message)
        self.wait(lambda: self.routes and self.public_routes and self.goals and self.states[-1].state == RouteState.SET, 'route and goal echo')
        self.case.assertEqual([s.preferred_primitive.id for s in self.routes[-1].segments], [9102, 9540, 9546, 9178, 52, 124])
        self.case.assertTrue(any(self.routes[-1].uuid.uuid))
        self.case.assertEqual(self.public_routes[-1].uuid, self.routes[-1].uuid)
        self.case.assertEqual(self.goals[-1].header.frame_id, 'map')
        self.case.assertEqual(self.goals[-1].pose, self.routes[-1].goal_pose)
        self.case.assertAlmostEqual(self.goals[-1].pose.position.x, self.goal.position.x)
        self.case.assertAlmostEqual(self.goals[-1].pose.position.y, self.goal.position.y)
        return self.routes[-1]

    def clear(self):
        client = self.node.create_client(ClearRoute, '/planning/clear_route')
        result = self.call(client, ClearRoute.Request())
        self.case.assertTrue(result.status.success, result.status.message)
        self.wait(lambda: self.states[-1].state == RouteState.UNSET, 'cleared route')
