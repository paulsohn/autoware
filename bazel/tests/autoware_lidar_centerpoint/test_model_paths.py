# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Resolve the real CenterPoint bundle defaults and owning runtime manifests."""
from pathlib import Path
import sys
import unittest
import xml.etree.ElementTree as E

from ament_index_python.packages import get_package_share_directory
from launch import LaunchContext
from launch.frontend.parse_substitution import parse_substitution
from launch.utilities import perform_substitutions
import launch_ros.substitutions
from python.runfiles import runfiles

PACKAGE = 'autoware_lidar_centerpoint_models'
resolver = runfiles.Create()
INPUTS = [Path(resolver.Rlocation(arg)) for arg in sys.argv[1:]]
sys.argv[1:] = []


class Paths(unittest.TestCase):
    def test_original_model_defaults(self):
        share = Path(get_package_share_directory(PACKAGE))
        for path, name, expected in [(INPUTS[0], 'model_path', share / 'base'),
                                      (INPUTS[1], 'centerpoint_model_path', share)]:
            expression = next(e.attrib['default'] for e in E.parse(path).findall('arg')
                              if e.attrib.get('name') == name)
            resolved = Path(perform_substitutions(LaunchContext(), parse_substitution(expression)))
            self.assertEqual(resolved, expected)
            self.assertTrue(resolved.is_dir())
            self.assertNotIn('data_path', expression)
            self.assertNotIn('HOME', expression)
        for bundle in ['base', 'short_range', 'sigma', 'tiny']:
            for name in ['pts_voxel_encoder.onnx', 'pts_backbone_neck_head.onnx',
                         'ml_package.param.yaml', 'detection_class_remapper.param.yaml']:
                self.assertTrue((share / bundle / name).is_file())

    def test_owning_manifests(self):
        for path in INPUTS[2:]:
            self.assertIn(PACKAGE, [e.text for e in E.parse(path).findall('exec_depend')])


if __name__ == '__main__':
    unittest.main()
