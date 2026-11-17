# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0

"""Exercise composed planning executables with ordinary ROS peers."""

from contextlib import contextmanager
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_adapi_v1_msgs.msg import ResponseStatus as ApiResponseStatus
from autoware_adapi_v1_msgs.srv import SetRoutePoints
from autoware_planning_msgs.msg import LaneletRoute, Path as RosPath, PathPoint, Trajectory
from autoware_planning_msgs.srv import ClearRoute, SetWaypointRoute
from geometry_msgs.msg import PoseStamped
from rcl_interfaces.srv import GetParameters
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile

MODE, EXECUTABLE = sys.argv[1:3]
del sys.argv[1:3]
EXECUTABLE = str(Path(EXECUTABLE).resolve())


class PlanningRuntime(unittest.TestCase):
    def setUp(self):
        os.environ['ROS_DOMAIN_ID'] = '66'
        rclpy.init()
        self.node = rclpy.create_node('planning_executable_peer')
        self.durable = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)

    def tearDown(self):
        self.node.destroy_node()
        rclpy.shutdown()

    @contextmanager
    def child(self, args=()):
        with tempfile.TemporaryFile(mode='w+') as log:
            process = subprocess.Popen(
                [EXECUTABLE, *args], stdout=log, stderr=subprocess.STDOUT
            )
            self.process, self.log = process, log
            try:
                yield
            finally:
                if process.poll() is None:
                    process.send_signal(signal.SIGINT)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    self.fail('Shutdown timed out: ' + self.output())
                self.assertEqual(process.returncode, 0, self.output())

    def output(self):
        self.log.seek(0)
        return self.log.read()

    def wait(self, condition):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            self.assertIsNone(self.process.poll(), self.output())
            if condition():
                return
            rclpy.spin_once(self.node, timeout_sec=0.05)
        self.fail('Behavior timed out: ' + self.output())

    def test_converter(self):
        received = []
        pub = self.node.create_publisher(RosPath, '/probe/path', 10)
        self.node.create_subscription(Trajectory, '/probe/trajectory', received.append, 10)
        with self.child(['--ros-args', '-p', 'input_topic:=/probe/path',
                         '-p', 'output_topic:=/probe/trajectory']):
            self.wait(lambda: pub.get_subscription_count() > 0 and
                      self.node.count_publishers('/probe/trajectory') > 0)
            for stamp, size in [(1, 2), (2, 0)]:
                msg = RosPath()
                msg.header.frame_id = 'map'
                msg.header.stamp.sec = stamp
                for i in range(size):
                    point = PathPoint()
                    point.pose.position.x = float(i + 1)
                    point.pose.orientation.w = 1.0
                    point.longitudinal_velocity_mps = 3.0
                    point.lateral_velocity_mps = 0.5
                    point.heading_rate_rps = 0.25
                    msg.points.append(point)
                pub.publish(msg)
                self.wait(lambda: any(m.header.stamp.sec == stamp for m in received))
                actual = next(m for m in received if m.header.stamp.sec == stamp)
                self.assertEqual(actual.header, msg.header)
                self.assertEqual(len(actual.points), size)
                for source, target in zip(msg.points, actual.points):
                    self.assertEqual(source.pose, target.pose)
                    self.assertEqual(target.longitudinal_velocity_mps, 3.0)
                    self.assertEqual(target.lateral_velocity_mps, 0.5)
                    self.assertEqual(target.heading_rate_rps, 0.25)

    def test_goal_visualizer(self):
        received = []
        pub = self.node.create_publisher(LaneletRoute, '/input/route', self.durable)
        self.node.create_subscription(PoseStamped, '/output/goal_pose',
                                      received.append, self.durable)
        with self.child():
            self.wait(lambda: pub.get_subscription_count() > 0 and
                      self.node.count_publishers('/output/goal_pose') > 0)
            route = LaneletRoute()
            route.header.frame_id = 'map'
            route.header.stamp.sec = 123
            route.goal_pose.position.x = 12.5
            route.goal_pose.position.y = -3.0
            route.goal_pose.orientation.w = 1.0
            pub.publish(route)
            self.wait(lambda: bool(received))
            self.assertEqual(received[-1].header, route.header)
            self.assertEqual(received[-1].pose, route.goal_pose)

    @staticmethod
    def parameters(package, *files):
        share = Path(get_package_share_directory(package))
        return [part for f in files for part in ['--params-file', str(share / f)]]

    def test_path_generator(self):
        args = ['--ros-args'] + self.parameters(
            'autoware_test_utils', 'config/test_vehicle_info.param.yaml',
            'config/test_nearest_search.param.yaml'
        ) + self.parameters('autoware_path_generator', 'config/path_generator.param.yaml')
        client = self.node.create_client(GetParameters, '/path_generator/get_parameters')
        received = []
        from autoware_internal_planning_msgs.msg import PathWithLaneId
        self.node.create_subscription(PathWithLaneId, '/path_generator/output/path',
                                      received.append, 10)
        with self.child(args):
            self.wait(lambda: client.service_is_ready() and
                      self.node.count_subscribers('/path_generator/input/vector_map') > 0)
            future = client.call_async(GetParameters.Request(
                names=['planning_hz', 'path_length.forward']
            ))
            self.wait(future.done)
            self.assertEqual([p.double_value for p in future.result().values], [10.0, 300.0])
            # Missing map/route/odometry must not produce a fabricated path.
            deadline = time.monotonic() + 0.35
            while time.monotonic() < deadline:
                self.assertIsNone(self.process.poll(), self.output())
                rclpy.spin_once(self.node, timeout_sec=0.05)
            self.assertEqual(received, [])

    def test_mission_planner(self):
        args = ['--ros-args'] + self.parameters(
            'autoware_test_utils', 'config/test_vehicle_info.param.yaml'
        ) + self.parameters('autoware_mission_planner', 'config/mission_planner.param.yaml')
        clear = self.node.create_client(ClearRoute, '/planning/clear_route')
        waypoint = self.node.create_client(SetWaypointRoute, '/planning/set_waypoint_route')
        with self.child(args):
            self.wait(lambda: clear.service_is_ready() and waypoint.service_is_ready())
            future = clear.call_async(ClearRoute.Request())
            self.wait(future.done)
            self.assertTrue(future.result().status.success)
            self.assertEqual(future.result().status.code, ApiResponseStatus.NO_EFFECT)
            request = SetWaypointRoute.Request()
            request.header.frame_id = 'map'
            request.goal_pose.orientation.w = 1.0
            future = waypoint.call_async(request)
            self.wait(future.done)
            self.assertFalse(future.result().status.success)
            self.assertEqual(future.result().status.code,
                             SetRoutePoints.Response.ERROR_INVALID_STATE)


if __name__ == '__main__':
    unittest.main(defaultTest='PlanningRuntime.test_' + MODE)
