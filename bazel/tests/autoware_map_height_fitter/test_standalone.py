# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Probe whole-cloud and partial-service height fitting in either backend."""

import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest

from autoware_internal_localization_msgs.srv import PoseWithCovarianceStamped
from autoware_map_msgs.msg import PointCloudMapCellWithID
from autoware_map_msgs.srv import GetPartialPointCloudMap
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile
from sensor_msgs.msg import PointCloud2, PointField

EXECUTABLE = str(Path(sys.argv[1]).resolve())
PARTIAL = sys.argv[2] == "partial"
del sys.argv[1:3]


class HeightFitterRuntime(unittest.TestCase):
    def test_fitted_service(self):
        environment = dict(os.environ, ROS_DOMAIN_ID="94")
        enabled = environment["AUTOWARE_TEST_AGNOCAST"] == "1"
        environment["ENABLE_AGNOCAST"] = "1" if enabled else "0"
        if enabled:
            environment["LD_PRELOAD"] = environment["AGNOCAST_TEST_HEAPHOOK"]
        else:
            environment.pop("LD_PRELOAD", None)
        os.environ["ROS_DOMAIN_ID"] = environment["ROS_DOMAIN_ID"]
        rclpy.init()
        node = rclpy.create_node("height_map_peer")
        node.declare_parameter("enable_partial_load", PARTIAL)
        cloud = PointCloud2()
        cloud.header.frame_id = "map"
        cloud.height, cloud.width = 1, 2
        cloud.fields = [PointField(name=axis, offset=i * 4, datatype=PointField.FLOAT32, count=1)
                        for i, axis in enumerate(["x", "y", "z"])]
        cloud.point_step, cloud.row_step = 12, 24
        cloud.is_dense = True
        cloud.data = struct.pack("<ffffff", 10.0, 20.0, 5.0, 10.0, 20.0, 8.0)
        partial_requests = []

        def load_partial(request, response):
            partial_requests.append(request)
            response.header.frame_id = "map"
            cell = PointCloudMapCellWithID()
            cell.cell_id, cell.pointcloud = "ground", cloud
            response.new_pointcloud_with_ids = [cell]
            return response

        server = node.create_service(GetPartialPointCloudMap, "/map_height_fitter/partial_map_load", load_partial)
        publisher = node.create_publisher(PointCloud2, "/map_height_fitter/pointcloud_map",
                                          QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
        publisher.publish(cloud)
        client = node.create_client(PoseWithCovarianceStamped, "/map_height_fitter/service")
        try:
            with tempfile.TemporaryFile(mode="w+") as log:
                process = subprocess.Popen(
                    [EXECUTABLE, "--ros-args", "-p", "map_height_fitter.target:=pointcloud_map",
                     "-p", "map_height_fitter.map_loader_name:=/height_map_peer"],
                    env=environment, stdout=log, stderr=subprocess.STDOUT,
                )
                try:
                    request = PoseWithCovarianceStamped.Request()
                    pose = request.pose_with_covariance
                    pose.header.frame_id, pose.header.stamp.sec = "map", 42
                    pose.pose.pose.position.x, pose.pose.pose.position.y, pose.pose.pose.position.z = 10.0, 20.0, 100.0
                    pose.pose.pose.orientation.w = 1.0
                    pose.pose.covariance[0] = 0.25
                    response = None
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and process.poll() is None:
                        rclpy.spin_once(node, timeout_sec=0.1)
                        if not client.service_is_ready():
                            continue
                        future = client.call_async(request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        log.seek(0)
                        self.assertTrue(future.done(), "Height service timed out:\n" + log.read())
                        response = future.result()
                        self.assertIsNotNone(response)
                        if response.success:
                            break
                    log.seek(0)
                    self.assertIsNotNone(response, log.read())
                    self.assertTrue(response.success)
                    fitted = response.pose_with_covariance
                    self.assertEqual(fitted.header, pose.header)
                    self.assertEqual(fitted.pose.pose.position.x, 10.0)
                    self.assertEqual(fitted.pose.pose.position.y, 20.0)
                    self.assertEqual(fitted.pose.pose.position.z, 5.0)
                    self.assertEqual(fitted.pose.pose.orientation, pose.pose.pose.orientation)
                    self.assertEqual(list(fitted.pose.covariance), list(pose.pose.covariance))
                    self.assertEqual(bool(partial_requests), PARTIAL)
                    if PARTIAL:
                        self.assertEqual(partial_requests[-1].area.center_x, 10.0)
                        self.assertEqual(partial_requests[-1].area.center_y, 20.0)
                        self.assertEqual(partial_requests[-1].area.radius, 50.0)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail("Height fitter failed to stop on SIGINT")
                log.seek(0)
                self.assertEqual(process.returncode, 0, log.read())
        finally:
            node.destroy_service(server)
            node.destroy_node()
            rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
