# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check GNSS projection, height and fix status with ordinary ROS peers in either backend."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from autoware_internal_debug_msgs.msg import BoolStamped
from autoware_map_msgs.msg import MapProjectorInfo
from geometry_msgs.msg import PoseStamped, PoseWithCovarianceStamped
from sensor_msgs.msg import NavSatFix, NavSatStatus
from rclpy.qos import DurabilityPolicy, QoSProfile
import rclpy

EXECUTABLE, PARAMETERS, GEOID = [str(Path(p).resolve()) for p in sys.argv[1:4]]
del sys.argv[1:4]


class GnssRuntime(unittest.TestCase):
    def test_standalone_projection(self):
        environment = dict(os.environ)
        enabled = environment["AUTOWARE_TEST_AGNOCAST"] == "1"
        environment["ENABLE_AGNOCAST"] = "1" if enabled else "0"
        environment["ROS_DOMAIN_ID"] = "83"
        environment["GEOGRAPHICLIB_GEOID_PATH"] = str(Path(GEOID).parent)
        if enabled:
            environment["LD_PRELOAD"] = environment["AGNOCAST_TEST_HEAPHOOK"]
        else:
            environment.pop("LD_PRELOAD", None)
        os.environ["ROS_DOMAIN_ID"] = environment["ROS_DOMAIN_ID"]
        rclpy.init()
        node = rclpy.create_node("gnss_runtime_peer")
        received, covariances, flags = [], [], []
        subscriptions = [
            node.create_subscription(PoseStamped, "gnss_pose", received.append, 10),
            node.create_subscription(PoseWithCovarianceStamped, "gnss_pose_cov", covariances.append, 10),
            node.create_subscription(BoolStamped, "gnss_fixed", flags.append, 10),
        ]
        publisher = node.create_publisher(NavSatFix, "fix", 10)
        projector_publisher = node.create_publisher(
            MapProjectorInfo, "/map/map_projector_info",
            QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL),
        )
        try:
            with tempfile.TemporaryFile(mode="w+") as log:
                process = subprocess.Popen(
                    [EXECUTABLE, "--ros-args", "--params-file", PARAMETERS, "-p", "use_gnss_ins_orientation:=false"],
                    env=environment, stdout=log, stderr=subprocess.STDOUT,
                )
                try:
                    info = MapProjectorInfo()
                    info.projector_type = MapProjectorInfo.LOCAL_CARTESIAN
                    info.vertical_datum = MapProjectorInfo.EGM2008
                    info.map_origin.latitude = 35.0
                    info.map_origin.longitude = 139.0
                    report = NavSatFix()
                    report.header.frame_id = "base_link"
                    report.header.stamp.sec = 42
                    report.status.status = NavSatStatus.STATUS_FIX
                    report.latitude, report.longitude, report.altitude = 35.0, 139.0, 100.0
                    report.position_covariance_type = NavSatFix.COVARIANCE_TYPE_KNOWN
                    report.position_covariance[0] = 1.0
                    report.position_covariance[4] = 2.0
                    report.position_covariance[8] = 3.0
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and not (received and covariances and flags):
                        if process.poll() is not None:
                            break
                        projector_publisher.publish(info)
                        publisher.publish(report)
                        rclpy.spin_once(node, timeout_sec=0.1)
                    if not (received and covariances and flags):
                        log.seek(0)
                        self.fail("Missing GNSS outputs; node log:\n" + log.read())
                    output = received[-1]
                    self.assertEqual(output.header.frame_id, "map")
                    self.assertEqual(output.header.stamp.sec, 42)
                    self.assertAlmostEqual(output.pose.position.x, 0.0, places=5)
                    self.assertAlmostEqual(output.pose.position.y, 0.0, places=5)
                    self.assertGreater(abs(output.pose.position.z - 100.0), 1.0)
                    self.assertAlmostEqual(output.pose.orientation.w, 1.0)
                    self.assertTrue(flags[-1].data)
                    self.assertEqual(flags[-1].stamp.sec, 42)
                    self.assertEqual([covariances[-1].pose.covariance[i] for i in [0, 7, 14]], [1.0, 2.0, 3.0])
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail("GNSS poser failed to stop on SIGINT")
                log.seek(0)
                output_log = log.read()
                self.assertEqual(process.returncode, 0, output_log)
                mode = "agnocast" if enabled else "rclcpp"
                self.assertIn(f"Starting node with {mode} mode", output_log)
        finally:
            for subscription in subscriptions:
                node.destroy_subscription(subscription)
            node.destroy_node()
            rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
