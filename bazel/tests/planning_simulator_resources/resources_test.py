# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check official map formats and indexed lookup after relocation."""
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

from ament_index_python.packages import get_package_share_directory
import yaml

PACKAGE = 'autoware_map_asset'
FILES = ['lanelet2_map.osm', 'map_config.yaml', 'map_projector_info.yaml', 'pointcloud_map.pcd']


class TestPlanningMap(unittest.TestCase):
    def test_map_payloads(self):
        source = Path(get_package_share_directory(PACKAGE))
        projector = yaml.safe_load((source / 'map_projector_info.yaml').read_text())
        self.assertEqual(projector, {'projector_type': 'MGRS', 'vertical_datum': 'WGS84', 'mgrs_grid': '54SVE'})
        osm = ET.parse(source / 'lanelet2_map.osm').getroot()
        self.assertEqual(len(osm.findall('node')), 4499)
        self.assertEqual(len(osm.findall('relation')), 241)
        with (source / 'pointcloud_map.pcd').open('rb') as stream:
            header = {}
            while True:
                line = stream.readline().decode().strip()
                self.assertTrue(line, 'Incomplete PCD header')
                if not line.startswith('#'):
                    key, *values = line.split()
                    header[key] = values
                    if key == 'DATA':
                        break
            self.assertEqual(header['DATA'], ['binary'])
            self.assertEqual(int(header['POINTS'][0]), 1757841)
            self.assertEqual(header['FIELDS'], ['x', 'y', 'z', 'rgb'])
            # The official archive retains 186 zero padding bytes after the declared points.
            stream.seek(1757841 * 16, os.SEEK_CUR)
            self.assertEqual(stream.read(), bytes(186))

    def test_relocated_lookup(self):
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
                self.assertTrue(all((destination / name).is_file() for name in FILES))


if __name__ == '__main__':
    unittest.main()
