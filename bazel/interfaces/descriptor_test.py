# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Nested source directories must retain the generated ROS msg namespace."""
from pathlib import Path
import sys
import unittest

MANIFEST = Path(sys.argv.pop(1))


class TestDescriptors(unittest.TestCase):
    def test_nested_message_namespace(self):
        descriptors = MANIFEST.read_text().splitlines()
        self.assertIn('tier4_perception_msgs/msg/DetectedObjectWithFeature', descriptors)
        self.assertIn('tier4_perception_msgs/msg/TrafficLight', descriptors)
        self.assertNotIn('tier4_perception_msgs/object_recognition/DetectedObjectWithFeature', descriptors)
        self.assertNotIn('tier4_perception_msgs/traffic_light/TrafficLight', descriptors)


if __name__ == '__main__':
    unittest.main()
