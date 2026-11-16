# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check stop-filter outputs with ordinary ROS peers in either backend."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from autoware_internal_debug_msgs.msg import BoolStamped
from nav_msgs.msg import Odometry
import rclpy

EXECUTABLE, PARAMETERS = [str(Path(p).resolve()) for p in sys.argv[1:3]]
del sys.argv[1:3]


class StopFilterRuntime(unittest.TestCase):
    def test_standalone_filtering(self):
        environment = dict(os.environ)
        enabled = environment["AUTOWARE_TEST_AGNOCAST"] == "1"
        environment["ENABLE_AGNOCAST"] = "1" if enabled else "0"
        environment["ROS_DOMAIN_ID"] = "77"
        if enabled:
            environment["LD_PRELOAD"] = environment["AGNOCAST_TEST_HEAPHOOK"]
        else:
            environment.pop("LD_PRELOAD", None)
        os.environ["ROS_DOMAIN_ID"] = environment["ROS_DOMAIN_ID"]
        rclpy.init()
        node = rclpy.create_node("stop_filter_runtime_peer")
        received = []
        flags = []
        subscription = node.create_subscription(Odometry, "output/odom", received.append, 10)
        flag_subscription = node.create_subscription(BoolStamped, "debug/stop_flag", flags.append, 10)
        publisher = node.create_publisher(Odometry, "input/odom", 10)
        try:
            with tempfile.TemporaryFile(mode="w+") as log:
                process = subprocess.Popen(
                    [EXECUTABLE, "--ros-args", "--params-file", PARAMETERS],
                    env=environment, stdout=log, stderr=subprocess.STDOUT,
                )
                try:
                    report = Odometry()
                    report.header.frame_id = "odom"
                    report.header.stamp.sec = 42
                    report.child_frame_id = "base_link"
                    report.pose.pose.position.x = 12.0
                    report.twist.twist.linear.x = 0.05
                    report.twist.twist.angular.z = 0.01
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and not (received and flags):
                        if process.poll() is not None:
                            break
                        publisher.publish(report)
                        rclpy.spin_once(node, timeout_sec=0.1)
                    if not (received and flags):
                        log.seek(0)
                        self.fail("Missing filter outputs; node log:\n" + log.read())
                    output = received[-1]
                    self.assertEqual(output.header.frame_id, "odom")
                    self.assertEqual(output.child_frame_id, "base_link")
                    self.assertEqual(output.pose.pose.position.x, 12.0)
                    self.assertEqual(output.twist.twist.linear.x, 0.0)
                    self.assertEqual(output.twist.twist.angular.z, 0.0)
                    self.assertTrue(flags[-1].data)
                    self.assertEqual(flags[-1].stamp.sec, 42)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail("Stop filter failed to stop on SIGINT")
                log.seek(0)
                output_log = log.read()
                self.assertEqual(process.returncode, 0, output_log)
                mode = "agnocast" if enabled else "rclcpp"
                self.assertIn(f"Starting node with {mode} mode", output_log)
        finally:
            node.destroy_subscription(subscription)
            node.destroy_subscription(flag_subscription)
            node.destroy_node()
            rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
