# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Resolve actual launcher model defaults with the model's own indexed resources."""
from pathlib import Path
import sys
import unittest
import xml.etree.ElementTree as ET

from ament_index_python.packages import get_package_share_directory
from launch import LaunchContext
from launch.frontend.parse_substitution import parse_substitution
from launch.utilities import perform_substitutions
import launch_ros.substitutions  # Register ROS frontend substitutions.
from python.runfiles import runfiles

PACKAGE = "autoware_tensorrt_yolox_models"
r = runfiles.Create()
INPUTS = [Path(r.Rlocation(p)) for arg in sys.argv[1:] for p in arg.split()]
sys.argv[1:] = []


class LaunchModels(unittest.TestCase):
    def test_indexed_model_and_metadata_defaults(self):
        share = Path(get_package_share_directory(PACKAGE))
        count = 0
        for path in INPUTS:
            if path.suffix != ".xml" or path.name == "package.xml":
                continue
            for element in ET.parse(path).iter():
                for expression in element.attrib.values():
                    if "$(find-pkg-share " + PACKAGE + ")" in expression:
                        resolved = Path(perform_substitutions(LaunchContext(), parse_substitution(expression)))
                        self.assertEqual(resolved.parent, share)
                        self.assertTrue(resolved.is_file(), expression)
                        count += 1
        self.assertGreater(count, 0)

    def test_launcher_runtime_dependencies(self):
        manifests = [p for p in INPUTS if p.name == "package.xml"]
        self.assertTrue(manifests)
        for path in manifests:
            dependencies = [e.text for e in ET.parse(path).findall("exec_depend")]
            self.assertIn(PACKAGE, dependencies, str(path))


if __name__ == "__main__":
    unittest.main()
