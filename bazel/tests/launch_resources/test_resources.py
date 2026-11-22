# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Resolve original config/launch resources through owned, relocatable indexes."""
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

from ament_index_python.packages import get_package_share_directory
import yaml

PACKAGE, FIXTURE = [sys.argv.pop(1) for _ in range(2)]


class Resources(unittest.TestCase):
    def test_original_payload(self):
        share = Path(get_package_share_directory(PACKAGE))
        self.assertEqual(ET.parse(share / 'package.xml').getroot().findtext('name'), PACKAGE)
        path = share / FIXTURE
        self.assertTrue(path.is_file(), path)
        if path.suffix == '.xml':
            root = ET.parse(path).getroot()
            self.assertEqual(root.tag, 'launch')
            self.assertIsNotNone(root.find("arg[@name='vehicle_id']"))
        else:
            config = yaml.safe_load(path.read_text())
            self.assertIsInstance(config, dict)
            if PACKAGE in ('autoware_planning_config', 'autoware_control_config'):
                key, expected = ('behavior_path_planner_type', 'behavior_path_planner') if PACKAGE == 'autoware_planning_config' else (
                    'trajectory_follower_mode', 'trajectory_follower_node')
                arguments = {item['arg']['name']: item['arg']['default'] for item in config['launch'] if 'arg' in item}
                self.assertEqual(arguments[key], expected)
            else:
                self.assertIn('ros__parameters', config['/**'])

    def test_relocated_index(self):
        source = Path(get_package_share_directory(PACKAGE))
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            destination = prefix / 'share' / PACKAGE
            shutil.copytree(source, destination)
            marker = prefix / 'share/ament_index/resource_index/packages' / PACKAGE
            marker.parent.mkdir(parents=True)
            marker.touch()
            with patch.dict(os.environ, {'AMENT_PREFIX_PATH': directory}):
                self.assertEqual(Path(get_package_share_directory(PACKAGE)), destination)
                self.assertTrue((destination / FIXTURE).is_file())


if __name__ == '__main__':
    unittest.main()
