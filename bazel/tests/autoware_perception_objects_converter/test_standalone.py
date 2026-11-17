# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check detected-to-predicted conversion through the actual node executable."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from autoware_perception_msgs.msg import DetectedObject, DetectedObjects, ObjectClassification, PredictedObjects
import rclpy

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())


class ConverterRuntime(unittest.TestCase):
    def test_object_conversion(self):
        os.environ["ROS_DOMAIN_ID"] = "91"
        environment = dict(os.environ)
        environment.pop("LD_PRELOAD", None)
        rclpy.init()
        node = rclpy.create_node("converter_runtime_peer")
        outputs = []
        subscription = node.create_subscription(PredictedObjects, "output/predicted_objects", outputs.append, 10)
        publisher = node.create_publisher(DetectedObjects, "input/detected_objects", 10)
        try:
            with tempfile.TemporaryFile(mode="w+") as log:
                process = subprocess.Popen([EXECUTABLE], env=environment, stdout=log, stderr=subprocess.STDOUT)
                try:
                    message = DetectedObjects()
                    message.header.frame_id = "map"
                    message.header.stamp.sec = 42
                    for i in range(2):
                        obj = DetectedObject()
                        obj.existence_probability = 0.9
                        obj.classification = [ObjectClassification(label=ObjectClassification.CAR, probability=0.75)]
                        obj.shape.type = obj.shape.BOUNDING_BOX
                        obj.shape.dimensions.x, obj.shape.dimensions.y, obj.shape.dimensions.z = 4.0, 2.0, 1.5
                        obj.kinematics.pose_with_covariance.pose.position.x = 12.5 + i
                        obj.kinematics.pose_with_covariance.pose.orientation.w = 1.0
                        obj.kinematics.pose_with_covariance.covariance[0] = 0.25
                        obj.kinematics.has_twist = i == 0
                        obj.kinematics.twist_with_covariance.twist.linear.x = 9.25
                        message.objects.append(obj)
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and not outputs:
                        if process.poll() is not None:
                            break
                        publisher.publish(message)
                        rclpy.spin_once(node, timeout_sec=0.1)
                    if not outputs:
                        log.seek(0)
                        self.fail("Missing predicted objects; node log:\n" + log.read())
                    output = outputs[-1]
                    self.assertEqual(output.header, message.header)
                    self.assertEqual(len(output.objects), 2)
                    ids = []
                    for i, (predicted, detected) in enumerate(zip(output.objects, message.objects)):
                        self.assertAlmostEqual(predicted.existence_probability, detected.existence_probability)
                        self.assertEqual(predicted.classification, detected.classification)
                        self.assertEqual(predicted.shape, detected.shape)
                        self.assertEqual(predicted.kinematics.initial_pose_with_covariance, detected.kinematics.pose_with_covariance)
                        self.assertEqual(predicted.kinematics.initial_twist_with_covariance.twist.linear.x, 9.25 if i == 0 else 0.0)
                        self.assertEqual(len(predicted.kinematics.predicted_paths), 0)
                        ids.append(bytes(predicted.object_id.uuid))
                    self.assertTrue(all(identifier != bytes(16) for identifier in ids))
                    self.assertNotEqual(ids[0], ids[1])
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
                self.assertEqual(process.returncode, 0, log.read())
        finally:
            node.destroy_subscription(subscription)
            node.destroy_node()
            rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
