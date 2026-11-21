# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Render the tutorial vehicle and resolve its meshes from an isolated prefix."""
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

from ament_index_python.packages import get_package_share_directory
import xacro


class TestVehicle(unittest.TestCase):
    def test_indexed_and_relocated_body(self):
        original = Path(get_package_share_directory('sample_vehicle_description'))
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            copied = prefix / 'share/sample_vehicle_description'
            shutil.copytree(original, copied)
            marker = prefix / 'share/ament_index/resource_index/packages/sample_vehicle_description'
            marker.parent.mkdir(parents=True)
            marker.touch()
            with patch.dict(os.environ, {'AMENT_PREFIX_PATH': directory}):
                self.assertEqual(Path(get_package_share_directory('sample_vehicle_description')), copied)
                robot = ET.fromstring(xacro.process_file(str(copied / 'urdf/vehicle.xacro')).toxml())
                self.assertEqual(robot.find('link').attrib['name'], 'base_link')
                offset = [float(x) for x in robot.find('link/visual/origin').attrib['xyz'].split()]
                self.assertEqual(offset, [2.79 / 2, 0, 0])
                for mesh in robot.iter('mesh'):
                    uri = mesh.attrib['filename']
                    package, relative = uri.removeprefix('package://').split('/', 1)
                    asset = Path(get_package_share_directory(package)) / relative
                    self.assertTrue(asset.is_file(), uri)
                    dae = ET.parse(asset)
                    for texture in dae.findall('.//{http://www.collada.org/2005/11/COLLADASchema}image/{http://www.collada.org/2005/11/COLLADASchema}init_from'):
                        self.assertTrue((asset.parent / texture.text).is_file(), texture.text)
                self.assertTrue((copied / 'config/simulator_model.param.yaml').is_file())


if __name__ == '__main__':
    unittest.main()
