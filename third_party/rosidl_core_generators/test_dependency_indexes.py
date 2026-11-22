# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""An interface index must forward the resources of its declared dependencies."""
from pathlib import Path
import unittest

from ament_index_python.packages import get_package_share_directory


class DependencyIndexesTest(unittest.TestCase):
    def test_nested_interface_resources(self):
        for package, filename in [('nav_msgs', 'Odometry'), ('geometry_msgs', 'Pose'), ('std_msgs', 'Header')]:
            with self.subTest(package=package):
                share = Path(get_package_share_directory(package))
                self.assertTrue((share / 'package.xml').is_file())
                self.assertTrue((share / 'msg' / (filename + '.msg')).is_file())


if __name__ == '__main__':
    unittest.main()
