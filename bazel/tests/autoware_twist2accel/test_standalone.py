# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check standalone acceleration estimation from both input message types."""

import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from nav_msgs.msg import Odometry
from geometry_msgs.msg import AccelWithCovarianceStamped, TwistWithCovarianceStamped
import rclpy

EXECUTABLE, PARAMETERS = [str(Path(p).resolve()) for p in sys.argv[1:3]]
del sys.argv[1:3]


class AccelerationRuntime(unittest.TestCase):
    def test_twist_input(self):
        self.check_input(False)

    def test_odometry_input(self):
        self.check_input(True)

    def check_input(self, use_odometry):
        environment = dict(os.environ)
        enabled = environment["AUTOWARE_TEST_AGNOCAST"] == "1"
        environment["ENABLE_AGNOCAST"] = "1" if enabled else "0"
        environment["ROS_DOMAIN_ID"] = "78"
        if enabled:
            environment["LD_PRELOAD"] = environment["AGNOCAST_TEST_HEAPHOOK"]
        else:
            environment.pop("LD_PRELOAD", None)
        os.environ["ROS_DOMAIN_ID"] = environment["ROS_DOMAIN_ID"]
        rclpy.init()
        node = rclpy.create_node("acceleration_runtime_peer")
        received = []
        subscription = node.create_subscription(
            AccelWithCovarianceStamped, "output/accel", received.append, 10
        )
        publisher = node.create_publisher(
            Odometry if use_odometry else TwistWithCovarianceStamped,
            "input/odom" if use_odometry else "input/twist", 10
        )
        try:
            with tempfile.TemporaryFile(mode="w+") as log:
                process = subprocess.Popen(
                    [EXECUTABLE, "--ros-args", "--params-file", PARAMETERS,
                     "-p", f"use_odom:={str(use_odometry).lower()}",
                     "-p", "accel_lowpass_gain:=0.0"],
                    env=environment, stdout=log, stderr=subprocess.STDOUT,
                )
                try:
                    report = Odometry() if use_odometry else TwistWithCovarianceStamped()
                    report.header.frame_id = "base_link"
                    sequence = 0
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and not any(math.isclose(m.accel.accel.linear.x, 2.0) for m in received):
                        if process.poll() is not None:
                            break
                        report.header.stamp.sec = sequence + 1
                        report.twist.twist.linear.x = 2.0 * sequence
                        report.twist.twist.angular.z = 0.5 * sequence
                        sequence += 1
                        publisher.publish(report)
                        rclpy.spin_once(node, timeout_sec=0.1)
                    if not received:
                        log.seek(0)
                        self.fail("No acceleration message; node log:\n" + log.read())
                    outputs = [m for m in received if math.isclose(m.accel.accel.linear.x, 2.0)]
                    self.assertTrue(outputs, "No correct finite-difference acceleration")
                    output = outputs[-1]
                    self.assertEqual(output.header.frame_id, "base_link")
                    self.assertAlmostEqual(output.accel.accel.angular.z, 0.5)
                    self.assertEqual(output.accel.covariance[0], 1.0)
                    self.assertEqual(output.accel.covariance[35], 0.05)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail("Acceleration node failed to stop on SIGINT")
                log.seek(0)
                output_log = log.read()
                self.assertEqual(process.returncode, 0, output_log)
                mode = "agnocast" if enabled else "rclcpp"
                self.assertIn(f"Starting node with {mode} mode", output_log)
        finally:
            node.destroy_subscription(subscription)
            node.destroy_node()
            rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
