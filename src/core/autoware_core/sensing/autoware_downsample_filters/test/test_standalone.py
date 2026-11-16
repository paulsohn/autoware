# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check voxel centroids and random-sample membership through real nodes."""

import math
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
import rclpy
from rclpy.qos import QoSProfile, ReliabilityPolicy
from sensor_msgs.msg import PointCloud2, PointField

EXECUTABLE, PARAMETERS = [str(Path(p).resolve()) for p in sys.argv[1:3]]
MODE = sys.argv[3]
del sys.argv[1:4]


class DownsampleRuntime(unittest.TestCase):
    def test_downsampled_cloud(self):
        environment = dict(os.environ, ROS_DOMAIN_ID="85" if MODE == "voxel" else "86")
        environment.pop("LD_PRELOAD", None)
        os.environ["ROS_DOMAIN_ID"] = environment["ROS_DOMAIN_ID"]
        rclpy.init()
        node = rclpy.create_node("downsample_runtime_peer")
        clouds, times = [], []
        subscriptions = [
            node.create_subscription(PointCloud2, "output", clouds.append,
                                     QoSProfile(depth=10, reliability=ReliabilityPolicy.BEST_EFFORT)),
            node.create_subscription(PublishedTime, "/output/debug/published_time", times.append, 10),
        ]
        publisher = node.create_publisher(PointCloud2, "input", 10)
        try:
            with tempfile.TemporaryFile(mode="w+") as log:
                process = subprocess.Popen(
                    [EXECUTABLE, "--ros-args", "--params-file", PARAMETERS] + (
                        ["-p", "sample_num:=1", "-p", "input_frame:=base_link", "-p", "output_frame:=base_link"]
                        if MODE == "random" else []),
                    env=environment, stdout=log, stderr=subprocess.STDOUT,
                )
                try:
                    cloud = PointCloud2()
                    cloud.header.frame_id = "base_link"
                    cloud.header.stamp.sec = 42
                    cloud.height, cloud.width = 1, 3
                    cloud.fields = [
                        PointField(name=name, offset=offset, datatype=kind, count=1)
                        for name, offset, kind in [
                            ("x", 0, PointField.FLOAT32), ("y", 4, PointField.FLOAT32),
                            ("z", 8, PointField.FLOAT32), ("intensity", 12, PointField.UINT8),
                            ("return_type", 13, PointField.UINT8), ("channel", 14, PointField.UINT16),
                        ]
                    ]
                    cloud.point_step, cloud.row_step = 16, 48
                    cloud.is_dense = True
                    inputs = [(0.1, 0.1, 0.1), (0.3, 0.3, 0.3), (3.1, 3.1, 3.1)]
                    cloud.data = b"".join(struct.pack("<fffBBH", *point, 10, 0, 0) for point in inputs)
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and not (clouds and times):
                        if process.poll() is not None:
                            break
                        publisher.publish(cloud)
                        rclpy.spin_once(node, timeout_sec=0.1)
                    if not (clouds and times):
                        log.seek(0)
                        self.fail("Missing downsampled output; node log:\n" + log.read())
                    output = clouds[-1]
                    self.assertEqual(output.header.frame_id, "base_link")
                    self.assertEqual(output.header.stamp.sec, 42)
                    offsets = {field.name: field.offset for field in output.fields}
                    points = sorted(tuple(struct.unpack_from("<f", bytes(output.data), i * output.point_step + offsets[axis])[0]
                                          for axis in ["x", "y", "z"]) for i in range(output.width))
                    if MODE == "voxel":
                        self.assertEqual(len(points), 2)
                        for point, expected in zip(points, [(0.2, 0.2, 0.2), (3.1, 3.1, 3.1)]):
                            for actual, value in zip(point, expected):
                                self.assertAlmostEqual(actual, value, places=5)
                    else:
                        self.assertEqual(len(points), 1)
                        self.assertTrue(any(all(math.isclose(a, b, abs_tol=1e-6) for a, b in zip(points[0], candidate))
                                            for candidate in inputs))
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
                        self.fail("Downsample node failed to stop on SIGINT")
                log.seek(0)
                self.assertEqual(process.returncode, 0, log.read())
        finally:
            for subscription in subscriptions:
                node.destroy_subscription(subscription)
            node.destroy_node()
            rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
