# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Validate package-qualified aliases through the standard ROS executable APIs."""
from pathlib import Path
import subprocess
import unittest

from ament_index_python.packages import get_package_prefix
from ros2pkg.api import get_executable_paths
from ros2run.api import get_executable_path


class ExecutableIndexTest(unittest.TestCase):
    def test_package_discovery_and_execution(self):
        prefix = Path(get_package_prefix('runtime_fixture')).resolve()
        legacy = prefix / 'bin' / 'echo_argument'
        qualified = prefix / 'lib' / 'runtime_fixture' / 'echo_argument'
        self.assertTrue(legacy.samefile(qualified))
        self.assertIn(qualified, [Path(path).absolute() for path in get_executable_paths(package_name='runtime_fixture')])
        selected = get_executable_path(package_name='runtime_fixture', executable_name='echo_argument')
        self.assertEqual(Path(selected).absolute(), qualified)
        result = subprocess.run([selected, 'argument with spaces'], text=True, capture_output=True, check=True)
        self.assertEqual(result.stdout, 'argument with spaces\n')


if __name__ == '__main__':
    unittest.main()
