#!/usr/bin/env python3
"""Drive the planning simulation from start to goal through the AD API and check that the vehicle moves.

Run while planning_simulator.launch.xml is up. Exit status 0 when the vehicle has driven at least --min-distance
metres toward the goal within --timeout seconds, 1 otherwise. The default poses are on lanelet 102 of
sample-map-planning (20 % and 80 % along it, heading along the lane), computed from the map's OSM file.
"""

import argparse
import math
import sys
import time

from autoware_adapi_v1_msgs.srv import ClearRoute
from autoware_adapi_v1_msgs.srv import SetRoutePoints
from geometry_msgs.msg import Pose
from geometry_msgs.msg import PoseWithCovarianceStamped
from autoware_system_msgs.srv import ChangeOperationMode
from nav_msgs.msg import Odometry
import rclpy
from rclpy.node import Node

START = (3647.13, 73515.49, 19.08, 0.750489, 0.660883)  # x, y, z, qz, qw
GOAL = (3638.57, 73582.70, 19.62, 0.750493, 0.660879)


def make_pose(p):
    pose = Pose()
    pose.position.x, pose.position.y, pose.position.z = p[0], p[1], p[2]
    pose.orientation.z, pose.orientation.w = p[3], p[4]
    return pose


class DriveCheck(Node):
    def __init__(self):
        super().__init__("drive_check")
        self.odom = None
        self.create_subscription(Odometry, "/localization/kinematic_state", self.on_odom, 10)
        self.initial_pose = self.create_publisher(PoseWithCovarianceStamped, "/initialpose3d", 1)
        self.clear_route = self.create_client(ClearRoute, "/api/routing/clear_route")
        self.set_route = self.create_client(SetRoutePoints, "/api/routing/set_route_points")
        self.change_mode = self.create_client(ChangeOperationMode, "/system/operation_mode/change_operation_mode")

    def on_odom(self, msg):
        self.odom = msg

    def spin_until(self, predicate, timeout, step=None):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if step:
                step()
            rclpy.spin_once(self, timeout_sec=0.2)
            if predicate():
                return True
        return False

    def call(self, client, request, timeout=5.0):
        if not client.wait_for_service(timeout_sec=timeout):
            return None
        future = client.call_async(request)
        rclpy.spin_until_future_complete(self, future, timeout_sec=timeout)
        return future.result()

    def position(self):
        p = self.odom.pose.pose.position
        return p.x, p.y


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--min-distance", type=float, default=10.0)
    args = parser.parse_args()

    rclpy.init()
    node = DriveCheck()
    log = node.get_logger()

    def publish_initial_pose():
        msg = PoseWithCovarianceStamped()
        msg.header.frame_id = "map"
        msg.header.stamp = node.get_clock().now().to_msg()
        msg.pose.pose = make_pose(START)
        node.initial_pose.publish(msg)

    def near_start():
        return node.odom is not None and math.dist(node.position(), START[:2]) < 1.0

    if not node.spin_until(near_start, 60.0, publish_initial_pose):
        log.error("the simulator did not report the initial pose")
        return 1
    log.info("vehicle at the start pose")

    # A route left over from an earlier run makes set_route_points fail with "The route is already set".
    node.call(node.clear_route, ClearRoute.Request())
    request = SetRoutePoints.Request()
    request.header.frame_id = "map"
    request.goal = make_pose(GOAL)
    route_set = False
    for _ in range(30):
        response = node.call(node.set_route, request, timeout=30.0)
        if response is not None and response.status.success:
            route_set = True
            break
        log.info("set_route_points not accepted yet: %s" % (response.status.message if response else "no service"))
        node.spin_until(lambda: False, 2.0)
    if not route_set:
        log.error("could not set the route")
        return 1
    log.info("route set")

    # autoware_core has no /api/operation_mode/change_to_autonomous (that is universe's operation mode transition
    # manager); its autoware_command_gate switches modes through the system service, with no availability check.
    response = node.call(node.change_mode, ChangeOperationMode.Request(mode=ChangeOperationMode.Request.AUTONOMOUS))
    if response is None or not response.status.success:
        log.error("change_to_autonomous failed: %s" % (response.status.message if response else "no service"))
        return 1
    log.info("autonomous mode engaged")

    start_to_goal = math.dist(START[:2], GOAL[:2])

    def progressed():
        return start_to_goal - math.dist(node.position(), GOAL[:2]) >= args.min_distance

    moved = node.spin_until(progressed, args.timeout)
    driven = start_to_goal - math.dist(node.position(), GOAL[:2])
    speed = node.odom.twist.twist.linear.x
    if moved:
        log.info("OK: drove %.1f m toward the goal (speed %.1f m/s)" % (driven, speed))
        return 0
    log.error("vehicle progressed only %.1f m toward the goal in %.0f s" % (driven, args.timeout))
    return 1


if __name__ == "__main__":
    sys.exit(main())
