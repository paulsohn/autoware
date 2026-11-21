# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Render the original tutorial sensor kit without a workspace/HOME lookup."""
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

from ament_index_python.packages import get_package_share_directory
import xacro


class TestSensors(unittest.TestCase):
    def test_indexed_and_relocated_sensors(self):
        sources = {name: Path(get_package_share_directory(name)) for name in
                   ['sample_sensor_kit_description', 'velodyne_description']}
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            markers = prefix / 'share/ament_index/resource_index/packages'
            markers.mkdir(parents=True)
            for name, source in sources.items():
                shutil.copytree(source, prefix / 'share' / name)
                (markers / name).touch()
            with patch.dict(os.environ, {'AMENT_PREFIX_PATH': directory}):
                source = Path(get_package_share_directory('sample_sensor_kit_description'))
                robot = ET.fromstring(xacro.process_file(str(source / 'urdf/sensors.xacro')).toxml())
                links = {x.attrib['name'] for x in robot.findall('link')}
                self.assertTrue({'sensor_kit_base_link', 'velodyne_top_base_link', 'velodyne_left_base_link',
                                 'velodyne_right_base_link', 'velodyne_rear_base_link', 'tamagawa/imu_link'} <= links)
                joint = robot.find("joint[@name='sensor_kit_base_link_joint']")
                self.assertEqual(joint.find('parent').attrib['link'], 'base_link')
                self.assertEqual([float(x) for x in joint.find('origin').attrib['xyz'].split()], [0.9, 0, 2])
                for mesh in robot.iter('mesh'):
                    uri = mesh.attrib['filename']
                    self.assertTrue(uri.startswith('package://'), uri)
                    package, relative = uri[len('package://'):].split('/', 1)
                    self.assertTrue((Path(get_package_share_directory(package)) / relative).is_file(), uri)


if __name__ == '__main__':
    unittest.main()
