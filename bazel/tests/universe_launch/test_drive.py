# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the original RViz pose/goal/Auto ROS workflow without a GUI."""
import math
from pathlib import Path
import time
import unittest

from autoware_adapi_v1_msgs.msg import LocalizationInitializationState, OperationModeState, RouteState
from autoware_adapi_v1_msgs.srv import ChangeOperationMode, SetRoutePoints
from autoware_planning_msgs.msg import Trajectory
from autoware_map_msgs.msg import LaneletMapBin
from composition_interfaces.srv import ListNodes
from geometry_msgs.msg import Pose, PoseWithCovarianceStamped
from nav_msgs.msg import Odometry
from sensor_msgs.msg import PointCloud2
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile
from python.runfiles import runfiles
import yaml

from test_launch import Composition, Shutdown, generate_test_description  # noqa: F401


class LaneDriving(unittest.TestCase):
    def test_pose_goal_auto_arrival(self):
        fixture = runfiles.Create().Rlocation('_main/bazel/tests/planning_simulator_resources/scenario.yaml')
        scenario = yaml.safe_load(Path(fixture).read_text())
        rclpy.init()
        peer = rclpy.create_node('lane_driving_acceptance_peer')
        latest = {}
        history = []
        retained = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)

        def save(key):
            return lambda message: latest.update({key: message})

        def odometry(message):
            latest['odometry'] = message
            history.append(message)

        peer.create_subscription(LocalizationInitializationState, '/api/localization/initialization_state', save('localization'), retained)
        peer.create_subscription(OperationModeState, '/api/operation_mode/state', save('operation'), retained)
        peer.create_subscription(RouteState, '/api/routing/state', save('route'), retained)
        peer.create_subscription(Odometry, '/localization/kinematic_state', odometry, 10)
        peer.create_subscription(Trajectory, '/planning/trajectory', save('trajectory'), 10)
        peer.create_subscription(LaneletMapBin, '/map/vector_map', save('map'), retained)
        peer.create_subscription(PointCloud2, '/map/pointcloud_map', save('cloud'), retained)
        initial = peer.create_publisher(PoseWithCovarianceStamped, '/initialpose', 10)

        def context():
            return {key: str(value)[:900] for key, value in latest.items() if key not in ('trajectory', 'map', 'cloud')}

        def wait(condition, description, timeout=60):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                rclpy.spin_once(peer, timeout_sec=0.05)
                if condition():
                    return
            self.fail(description + ': ' + str(context()))

        def call(kind, name, request):
            client = peer.create_client(kind, name)
            wait(client.service_is_ready, 'Missing service ' + name)
            future = client.call_async(request)
            wait(future.done, 'No service response ' + name)
            self.assertIsNone(future.exception())
            return future.result()

        def pose(values):
            x, y, z, yaw = values
            result = Pose()
            result.position.x, result.position.y, result.position.z = x, y, z
            result.orientation.z, result.orientation.w = math.sin(yaw / 2), math.cos(yaw / 2)
            return result

        def distance(message, target):
            position = message.pose.pose.position
            return math.hypot(position.x - target[0], position.y - target[1])

        try:
            wait(lambda: 'map' in latest and 'cloud' in latest, 'Official map was not delivered')
            wait(lambda: len(call(ListNodes, '/adapi/container/_container/list_nodes', ListNodes.Request()).full_node_names) == 19,
                 'AD API components did not finish loading')
            wait(lambda: initial.get_subscription_count() > 0, 'Initial-pose adaptor missing')
            start = PoseWithCovarianceStamped()
            start.header.frame_id = scenario['frame_id']
            start.header.stamp = peer.get_clock().now().to_msg()
            start.pose.pose = pose(scenario['initial_pose'])
            start.pose.covariance[0] = start.pose.covariance[7] = 0.25
            start.pose.covariance[35] = 0.0685
            print('Publishing initial pose', flush=True)
            initial.publish(start)
            wait(lambda: latest.get('localization') and latest['localization'].state == LocalizationInitializationState.INITIALIZED,
                 'Localization did not initialize')
            wait(lambda: latest.get('odometry') and distance(latest['odometry'], scenario['initial_pose']) < 0.5,
                 'Vehicle did not start at the selected pose')
            wait(lambda: latest.get('route') and latest['route'].state == RouteState.UNSET,
                 'Routing did not become ready for a new route')
            request = SetRoutePoints.Request()
            request.header.frame_id = scenario['frame_id']
            request.header.stamp = peer.get_clock().now().to_msg()
            request.goal = pose(scenario['goal_pose'])
            print('Setting route', flush=True)
            response = call(SetRoutePoints, '/api/routing/set_route_points', request)
            self.assertTrue(response.status.success, str(response.status))
            wait(lambda: latest.get('route') and latest['route'].state == RouteState.SET, 'Route was not set')
            wait(lambda: latest.get('trajectory') and len(latest['trajectory'].points) >= 2, 'No planned trajectory')
            wait(lambda: latest.get('operation') and latest['operation'].is_autonomous_mode_available,
                 'Autonomous mode is unavailable')
            print('Requesting autonomous mode', flush=True)
            response = call(ChangeOperationMode, '/api/operation_mode/change_to_autonomous', ChangeOperationMode.Request())
            self.assertTrue(response.status.success, str(response.status))
            wait(lambda: latest['operation'].mode == OperationModeState.AUTONOMOUS and
                 latest['operation'].is_autoware_control_enabled and not latest['operation'].is_in_transition,
                 'Autonomous control did not engage')
            wait(lambda: latest['route'].state == RouteState.ARRIVED and
                 distance(latest['odometry'], scenario['goal_pose']) < 2.0 and
                 abs(latest['odometry'].twist.twist.linear.x) < 0.1,
                 'Vehicle did not arrive and stop', timeout=180)
            travel = max(distance(message, scenario['initial_pose']) for message in history)
            speed = max(abs(message.twist.twist.linear.x) for message in history)
            self.assertGreater(travel, 5.0)
            self.assertGreater(speed, 0.2)
            print(f'Arrived: displacement={travel:.2f}m peak_speed={speed:.2f}m/s '
                  f'goal_error={distance(latest["odometry"], scenario["goal_pose"]):.2f}m', flush=True)
        finally:
            peer.destroy_node()
            rclpy.shutdown()
