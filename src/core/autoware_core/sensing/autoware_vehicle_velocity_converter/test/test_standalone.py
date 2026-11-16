# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check the standalone converter with ordinary ROS peers in either backend."""

import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from autoware_vehicle_msgs.msg import VelocityReport
from geometry_msgs.msg import TwistWithCovarianceStamped
import rclpy

EXECUTABLE, PARAMETERS = [str(Path(p).resolve()) for p in sys.argv[1:3]]
del sys.argv[1:3]


class ConverterRuntime(unittest.TestCase):
    def test_standalone_conversion(self):
        environment = dict(os.environ)
        enabled = environment["AUTOWARE_TEST_AGNOCAST"] == "1"
        environment["ENABLE_AGNOCAST"] = "1" if enabled else "0"
        environment["ROS_DOMAIN_ID"] = "76"
        if enabled:
            environment["LD_PRELOAD"] = environment["AGNOCAST_TEST_HEAPHOOK"]
        else:
            environment.pop("LD_PRELOAD", None)
        os.environ["ROS_DOMAIN_ID"] = environment["ROS_DOMAIN_ID"]
        rclpy.init()
        node = rclpy.create_node("converter_runtime_peer")
        received = []
        subscription = node.create_subscription(
            TwistWithCovarianceStamped, "twist_with_covariance", received.append, 10
        )
        publisher = node.create_publisher(VelocityReport, "velocity_status", 10)
        try:
            with tempfile.TemporaryFile(mode="w+") as log:
                process = subprocess.Popen(
                    [EXECUTABLE, "--ros-args", "--params-file", PARAMETERS,
                     "-p", "speed_scale_factor:=1.5"],
                    env=environment, stdout=log, stderr=subprocess.STDOUT,
                )
                try:
                    report = VelocityReport()
                    report.header.frame_id = "base_link"
                    report.longitudinal_velocity = 2.0
                    report.lateral_velocity = 0.125
                    report.heading_rate = 0.25
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and not received:
                        if process.poll() is not None:
                            break
                        publisher.publish(report)
                        rclpy.spin_once(node, timeout_sec=0.1)
                    if not received:
                        log.seek(0)
                        self.fail("No converted message; node log:\n" + log.read())
                    output = received[-1]
                    self.assertEqual(output.header.frame_id, "base_link")
                    self.assertEqual(output.twist.twist.linear.x, 3.0)
                    self.assertEqual(output.twist.twist.linear.y, 0.125)
                    self.assertEqual(output.twist.twist.angular.z, 0.25)
                    self.assertTrue(math.isclose(output.twist.covariance[0], 0.2 ** 2))
                    self.assertTrue(math.isclose(output.twist.covariance[35], 0.1 ** 2))
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail("Converter failed to stop on SIGINT")
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
