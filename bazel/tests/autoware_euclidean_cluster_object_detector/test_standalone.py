# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Verify both clustering executables with their shipped parameter files."""

import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest

from autoware_perception_msgs.msg import DetectedObjects
import rclpy
from sensor_msgs.msg import PointCloud2, PointField

EXECUTABLE, PARAMETERS = [str(Path(p).resolve()) for p in sys.argv[1:3]]
del sys.argv[1:3]


class ClusterRuntime(unittest.TestCase):
    def test_two_clusters(self):
        os.environ["ROS_DOMAIN_ID"] = "89" if "voxel" in EXECUTABLE else "90"
        environment = dict(os.environ)
        environment.pop("LD_PRELOAD", None)
        rclpy.init()
        node = rclpy.create_node("cluster_runtime_peer")
        objects, debug = [], []
        subscriptions = [
            node.create_subscription(DetectedObjects, "output", objects.append, 10),
            node.create_subscription(PointCloud2, "debug/clusters", debug.append, 10),
        ]
        publisher = node.create_publisher(PointCloud2, "input", 10)
        try:
            with tempfile.TemporaryFile(mode="w+") as log:
                process = subprocess.Popen([EXECUTABLE, "--ros-args", "--params-file", PARAMETERS],
                                           env=environment, stdout=log, stderr=subprocess.STDOUT)
                try:
                    cloud = PointCloud2()
                    cloud.header.frame_id = "base_link"
                    cloud.header.stamp.sec = 42
                    cloud.height, cloud.width = 1, 24
                    cloud.fields = [PointField(name=axis, offset=i * 4, datatype=PointField.FLOAT32, count=1)
                                    for i, axis in enumerate(["x", "y", "z"])]
                    cloud.point_step, cloud.row_step = 12, 24 * 12
                    cloud.is_dense = True
                    cloud.data = b"".join(struct.pack("<fff", x + i * 0.01, y, z)
                                          for x, y, z in [(1.0, 0.0, 0.2), (5.0, 1.0, 1.2)]
                                          for i in range(12))
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and not (objects and debug):
                        if process.poll() is not None:
                            break
                        publisher.publish(cloud)
                        rclpy.spin_once(node, timeout_sec=0.1)
                    if not (objects and debug):
                        log.seek(0)
                        self.fail("Missing cluster output; node log:\n" + log.read())
                    output = objects[-1]
                    self.assertEqual(output.header.frame_id, "base_link")
                    self.assertEqual(output.header.stamp.sec, 42)
                    self.assertEqual(len(output.objects), 2)
                    centers = sorted((obj.kinematics.pose_with_covariance.pose.position.x,
                                      obj.kinematics.pose_with_covariance.pose.position.y,
                                      obj.kinematics.pose_with_covariance.pose.position.z) for obj in output.objects)
                    for center, expected in zip(centers, [(1.055, 0.0, 0.2), (5.055, 1.0, 1.2)]):
                        for actual, value in zip(center, expected):
                            self.assertAlmostEqual(actual, value, places=5)
                    for obj in output.objects:
                        self.assertEqual(len(obj.classification), 1)
                        self.assertEqual(obj.classification[0].probability, 1.0)
                    self.assertEqual(debug[-1].width * debug[-1].height, 24)
                    self.assertEqual(debug[-1].header.frame_id, "base_link")
                    self.assertEqual(debug[-1].header.stamp.sec, 42)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail("Clustering node failed to stop on SIGINT")
                log.seek(0)
                self.assertEqual(process.returncode, 0, log.read())
        finally:
            for subscription in subscriptions:
                node.destroy_subscription(subscription)
            node.destroy_node()
            rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
