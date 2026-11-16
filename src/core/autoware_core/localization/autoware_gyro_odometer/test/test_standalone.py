# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check gyro-odometer fusion and diagnostics with ordinary ROS peers in either backend."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus
from geometry_msgs.msg import TwistStamped, TwistWithCovarianceStamped
from sensor_msgs.msg import Imu
import rclpy

EXECUTABLE, PARAMETERS = [str(Path(p).resolve()) for p in sys.argv[1:3]]
del sys.argv[1:3]


class GyroOdometerRuntime(unittest.TestCase):
    def test_standalone_fusion(self):
        environment = dict(os.environ)
        enabled = environment["AUTOWARE_TEST_AGNOCAST"] == "1"
        environment["ENABLE_AGNOCAST"] = "1" if enabled else "0"
        environment["ROS_DOMAIN_ID"] = "79"
        if enabled:
            environment["LD_PRELOAD"] = environment["AGNOCAST_TEST_HEAPHOOK"]
        else:
            environment.pop("LD_PRELOAD", None)
        os.environ["ROS_DOMAIN_ID"] = environment["ROS_DOMAIN_ID"]
        rclpy.init()
        node = rclpy.create_node("gyro_runtime_peer")
        outputs = {name: [] for name in ["twist_raw", "twist", "twist_with_covariance_raw", "twist_with_covariance"]}
        subscriptions = []
        for name in outputs:
            message_type = TwistWithCovarianceStamped if "covariance" in name else TwistStamped
            subscriptions.append(node.create_subscription(
                message_type, name, lambda msg, key=name: outputs[key].append(msg), 10
            ))
        diagnostics = []
        subscriptions.append(node.create_subscription(DiagnosticArray, "/diagnostics", diagnostics.append, 10))
        vehicle_publisher = node.create_publisher(TwistWithCovarianceStamped, "vehicle/twist_with_covariance", 10)
        imu_publisher = node.create_publisher(Imu, "imu", 10)

        def healthy():
            return any("gyro_odometer_status" in status.name and status.level == DiagnosticStatus.OK
                       for message in diagnostics for status in message.status)
        try:
            with tempfile.TemporaryFile(mode="w+") as log:
                process = subprocess.Popen(
                    [EXECUTABLE, "--ros-args", "--params-file", PARAMETERS, "-p", "message_timeout_sec:=10.0"],
                    env=environment, stdout=log, stderr=subprocess.STDOUT,
                )
                try:
                    vehicle = TwistWithCovarianceStamped()
                    vehicle.header.frame_id = "base_link"
                    vehicle.twist.twist.linear.x = 1.0
                    vehicle.twist.covariance[0] = 4.0
                    imu = Imu()
                    imu.header.frame_id = "base_link"
                    imu.angular_velocity.x = 0.1
                    imu.angular_velocity.y = 0.2
                    imu.angular_velocity.z = 0.3
                    imu.angular_velocity_covariance = [0.01, 0.0, 0.0, 0.0, 0.02, 0.0, 0.0, 0.0, 0.03]
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and not (all(outputs.values()) and healthy()):
                        if process.poll() is not None:
                            break
                        stamp = node.get_clock().now().to_msg()
                        vehicle.header.stamp = stamp
                        imu.header.stamp = stamp
                        vehicle_publisher.publish(vehicle)
                        imu_publisher.publish(imu)
                        rclpy.spin_once(node, timeout_sec=0.05)
                    if not all(outputs.values()) or not healthy():
                        log.seek(0)
                        self.fail("Missing fusion/healthy diagnostics; node log:\n" + log.read())
                    for name, messages in outputs.items():
                        output = messages[-1]
                        twist = output.twist.twist if "covariance" in name else output.twist
                        self.assertEqual(output.header.frame_id, "base_link")
                        self.assertAlmostEqual(twist.linear.x, 1.0)
                        self.assertAlmostEqual(twist.angular.z, 0.3)
                        if "covariance" in name:
                            self.assertAlmostEqual(output.twist.covariance[0], 4.0)
                            self.assertAlmostEqual(output.twist.covariance[35], 0.03)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail("Gyro odometer failed to stop on SIGINT")
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
