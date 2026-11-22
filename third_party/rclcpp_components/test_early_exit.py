# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the actual native component-container help and argument-error exits."""
from pathlib import Path
import subprocess
import sys
import unittest

BINARY = Path(sys.argv.pop()).absolute()


class EarlyExitTest(unittest.TestCase):
    def test_help_and_invalid_executor(self):
        for arguments, status in [(['--help'], 0), (['--executor-type', 'invalid'], 1), (['--executor-type'], 1)]:
            with self.subTest(arguments=arguments):
                result = subprocess.run([str(BINARY), *arguments], text=True, capture_output=True, timeout=20)
                self.assertEqual(result.returncode, status, result.stdout + result.stderr)
                self.assertIn('Usage: component_container', result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
