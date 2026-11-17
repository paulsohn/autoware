# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check ground removal through the real component executable."""

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
del sys.argv[1:3]
VEHICLE_PARAMETERS = str(Path("share/autoware_test_utils/config/test_vehicle_info.param.yaml").absolute())


class GroundRuntime(unittest.TestCase):
    def test_downsampled_cloud(self):
        environment = dict(os.environ, ROS_DOMAIN_ID="88")
        environment.pop("LD_PRELOAD", None)
        os.environ["ROS_DOMAIN_ID"] = environment["ROS_DOMAIN_ID"]
        rclpy.init()
        node = rclpy.create_node("ground_runtime_peer")
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
                    [EXECUTABLE, "--ros-args", "--params-file", VEHICLE_PARAMETERS,
                     "--params-file", PARAMETERS, "-p", "elevation_grid_mode:=false",
                     "-p", "input_frame:=base_link", "-p", "output_frame:=base_link",
                     "-p", "global_slope_max_angle_deg:=15.0", "-p", "local_slope_max_angle_deg:=15.0"],
                    env=environment, stdout=log, stderr=subprocess.STDOUT,
                )
                try:
                    cloud = PointCloud2()
                    cloud.header.frame_id = "base_link"
                    cloud.header.stamp.sec = 42
                    cloud.height, cloud.width = 1, 4
                    cloud.fields = [
                        PointField(name=name, offset=offset, datatype=kind, count=1)
                        for name, offset, kind in [
                            ("x", 0, PointField.FLOAT32), ("y", 4, PointField.FLOAT32),
                            ("z", 8, PointField.FLOAT32), ("intensity", 12, PointField.UINT8),
                            ("return_type", 13, PointField.UINT8), ("channel", 14, PointField.UINT16),
                        ]
                    ]
                    cloud.point_step, cloud.row_step = 16, 64
                    cloud.is_dense = True
                    inputs = [(2.0, 0.0, 0.0), (3.0, 0.0, 0.0), (4.0, 0.0, 0.6), (5.0, 0.0, 2.0)]
                    cloud.data = b"".join(struct.pack("<fffBBH", *point, 10, 0, 0) for point in inputs)
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and not (clouds and times):
                        if process.poll() is not None:
                            break
                        publisher.publish(cloud)
                        rclpy.spin_once(node, timeout_sec=0.1)
                    if not (clouds and times):
                        log.seek(0)
                        self.fail("Missing ground-filter output; node log:\n" + log.read())
                    output = clouds[-1]
                    self.assertEqual(output.header.frame_id, "base_link")
                    self.assertEqual(output.header.stamp.sec, 42)
                    offsets = {field.name: field.offset for field in output.fields}
                    points = sorted(tuple(struct.unpack_from("<f", bytes(output.data), i * output.point_step + offsets[axis])[0]
                                          for axis in ["x", "y", "z"]) for i in range(output.width))
                    self.assertEqual(len(points), 2)
                    for point, expected in zip(points, [(4.0, 0.0, 0.6), (5.0, 0.0, 2.0)]):
                        for actual, value in zip(point, expected):
                            self.assertAlmostEqual(actual, value, places=5)
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
                        self.fail("Ground-filter node failed to stop on SIGINT")
                log.seek(0)
                self.assertEqual(process.returncode, 0, log.read())
        finally:
            for subscription in subscriptions:
                node.destroy_subscription(subscription)
            node.destroy_node()
            rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
