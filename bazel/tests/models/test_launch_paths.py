# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Resolve actual launcher model defaults with the model's own indexed resources."""
from pathlib import Path
from itertools import product
import re
import sys
import unittest
import xml.etree.ElementTree as ET

from ament_index_python.packages import get_package_share_directory
from launch import LaunchContext
from launch.frontend.parse_substitution import parse_substitution
from launch.utilities import perform_substitutions
import launch_ros.substitutions  # Register ROS frontend substitutions.
from python.runfiles import runfiles

PACKAGE = sys.argv.pop(1)
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
            document = ET.parse(path)
            for element in document.iter():
                for expression in element.attrib.values():
                    if "$(find-pkg-share " + PACKAGE + ")" in expression:
                        variables = sorted(set(re.findall(r"\$\(var ([^)]+)\)", expression)))
                        defaults = []
                        for name in variables:
                            values = {e.attrib["default"] for e in document.iter("arg")
                                      if e.attrib.get("name") == name and "default" in e.attrib}
                            self.assertTrue(values, "No local defaults for " + name)
                            defaults.append(sorted(values))
                        for values in product(*defaults):
                            context = LaunchContext()
                            context.launch_configurations.update(zip(variables, values))
                            resolved = Path(perform_substitutions(context, parse_substitution(expression)))
                            self.assertTrue(resolved.is_relative_to(share), expression)
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
