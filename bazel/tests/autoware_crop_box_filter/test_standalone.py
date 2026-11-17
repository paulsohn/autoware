# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check the standalone crop-box node and its declared instrumentation."""

import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest

from autoware_internal_msgs.msg import PublishedTime
from geometry_msgs.msg import PolygonStamped
import rclpy
from rclpy.qos import QoSProfile, ReliabilityPolicy
from sensor_msgs.msg import PointCloud2, PointField

EXECUTABLE, PARAMETERS = [str(Path(p).resolve()) for p in sys.argv[1:3]]
del sys.argv[1:3]


class CropBoxRuntime(unittest.TestCase):
    def test_cloud_and_instrumentation(self):
        environment = dict(os.environ, ROS_DOMAIN_ID="84")
        environment.pop("LD_PRELOAD", None)
        os.environ["ROS_DOMAIN_ID"] = environment["ROS_DOMAIN_ID"]
        rclpy.init()
        node = rclpy.create_node("crop_box_runtime_peer")
        clouds, polygons, times = [], [], []
        subscriptions = [
            node.create_subscription(PointCloud2, "output", clouds.append,
                                     QoSProfile(depth=10, reliability=ReliabilityPolicy.BEST_EFFORT)),
            node.create_subscription(PolygonStamped, "/crop_box_filter/crop_box_polygon", polygons.append, 10),
            node.create_subscription(PublishedTime, "/output/debug/published_time", times.append, 10),
        ]
        publisher = node.create_publisher(PointCloud2, "input", 10)
        try:
            with tempfile.TemporaryFile(mode="w+") as log:
                process = subprocess.Popen(
                    [EXECUTABLE, "--ros-args", "--params-file", PARAMETERS],
                    env=environment, stdout=log, stderr=subprocess.STDOUT,
                )
                try:
                    cloud = PointCloud2()
                    cloud.header.frame_id = "base_link"
                    cloud.header.stamp.sec = 42
                    cloud.height, cloud.width = 1, 2
                    cloud.fields = [
                        PointField(name=name, offset=offset, datatype=kind, count=1)
                        for name, offset, kind in [
                            ("x", 0, PointField.FLOAT32), ("y", 4, PointField.FLOAT32),
                            ("z", 8, PointField.FLOAT32), ("intensity", 12, PointField.UINT8),
                            ("return_type", 13, PointField.UINT8), ("channel", 14, PointField.UINT16),
                        ]
                    ]
                    cloud.point_step, cloud.row_step = 16, 32
                    cloud.is_dense = True
                    cloud.data = struct.pack("<fffBBH", 0.0, 0.0, 0.0, 10, 0, 1) + struct.pack(
                        "<fffBBH", 10.0, 0.0, 0.0, 90, 0, 3
                    )
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and not (clouds and polygons and times):
                        if process.poll() is not None:
                            break
                        publisher.publish(cloud)
                        rclpy.spin_once(node, timeout_sec=0.1)
                    if not (clouds and polygons and times):
                        log.seek(0)
                        self.fail("Missing cloud/instrumentation output; node log:\n" + log.read())
                    output = clouds[-1]
                    self.assertEqual((output.height, output.width, output.point_step), (1, 1, 16))
                    self.assertEqual(output.header.frame_id, "base_link")
                    self.assertEqual(output.header.stamp.sec, 42)
                    self.assertEqual(struct.unpack("<fffBBH", bytes(output.data)), (10.0, 0.0, 0.0, 90, 0, 3))
                    self.assertEqual(polygons[-1].header.frame_id, "base_link")
                    self.assertEqual(len(polygons[-1].polygon.points), 16)
                    self.assertEqual(times[-1].header.stamp.sec, 42)
                    self.assertGreater(times[-1].published_stamp.sec, 0)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail("Crop-box node failed to stop on SIGINT")
                log.seek(0)
                self.assertEqual(process.returncode, 0, log.read())
        finally:
            for subscription in subscriptions:
                node.destroy_subscription(subscription)
            node.destroy_node()
            rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
