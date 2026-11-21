# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check that distribution archives are closed, relocatable file trees."""
import posixpath
import sys
import tarfile
import unittest

ARCHIVES = sys.argv[1:]
del sys.argv[1:]


class TestBundle(unittest.TestCase):
    def test_declared_archives(self):
        self.assertEqual(len(ARCHIVES), 2)
        for filename in ARCHIVES:
            with self.subTest(archive=filename), tarfile.open(filename) as archive:
                members = {}
                for member in archive:
                    name = posixpath.normpath(member.name)
                    self.assertFalse(name.startswith(('/', '../')), name)
                    self.assertNotEqual(name, '..')
                    self.assertNotIn(name, members, 'Duplicate archive destination: ' + name)
                    self.assertTrue(member.isfile() or member.isdir() or member.issym()
                                    or member.islnk(), name)
                    members[name] = member
                for name, member in members.items():
                    if member.issym() or member.islnk():
                        target = member.linkname
                        self.assertFalse(target.startswith('/'), (name, target))
                        if member.issym():
                            target = posixpath.join(posixpath.dirname(name), target)
                        target = posixpath.normpath(target)
                        self.assertIn(target, members, (name, target))
                entry = 'core_headless' if 'core_headless' in members else 'core_system_headless_launch_test'
                self.assertTrue(members[entry].mode & 0o111)
                self.assertEqual(archive.extractfile(entry).readline().strip(), b'#!/usr/bin/env bash')
                self.assertIn(entry + '.runfiles/_repo_mapping', members)
                self.assertIn(entry + '.runfiles/_main/share/ament_index/resource_index/packages/autoware_core', members)
                self.assertIn(entry + '.runfiles/_main/share/ament_index/resource_index/packages/autoware_map_asset', members)
                for name in ('lanelet2_map.osm', 'pointcloud_map.pcd', 'map_projector_info.yaml', 'map_config.yaml'):
                    self.assertIn(entry + '.runfiles/_main/share/autoware_map_asset/' + name, members)
                print(f'{filename}: {len(members)} entries, closed links, executable Bash bootstrap')


if __name__ == '__main__':
    unittest.main()
