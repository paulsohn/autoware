# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Declared interfaces retain their canonical ROS package and type namespaces."""
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

    def test_dependency_repository_package_identity(self):
        descriptors = MANIFEST.read_text().splitlines()
        for interface in ['msg/GridMap', 'msg/GridMapInfo', 'srv/GetGridMap',
                          'srv/GetGridMapInfo', 'srv/SetGridMap', 'srv/ProcessFile']:
            self.assertIn('grid_map_msgs/' + interface, descriptors)
            self.assertNotIn('grid_map/' + interface, descriptors)


if __name__ == '__main__':
    unittest.main()
