# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the interface collection's real Python/index consumer contract."""
from pathlib import Path
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_planning_msgs.msg import Trajectory
from geometry_msgs.msg import Pose
from rosidl_runtime_py.get_interfaces import get_interface_path
from rosidl_generator_py import import_type_support


class PythonInterfacesTest(unittest.TestCase):
    def test_python_and_transitive_indexes(self):
        for package, name in [('autoware_planning_msgs', 'Trajectory'), ('geometry_msgs', 'Pose'),
                              ('yabloc_particle_filter', 'ParticleArray')]:
            with self.subTest(package=package):
                self.assertTrue(Path(get_package_share_directory(package)).is_dir())
                self.assertTrue(Path(get_interface_path(f'{package}/msg/{name}')).is_file())
                self.assertIsNotNone(import_type_support(package))
        self.assertIsInstance(Trajectory().points, list)
        self.assertIsInstance(Pose().position.x, float)


if __name__ == '__main__':
    unittest.main()
