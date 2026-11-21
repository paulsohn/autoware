# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess
import unittest


class RegisteredExecutables(unittest.TestCase):
    def test_plain_resource(self):
        self.assertEqual(Path('share/registration_fixture/payload.txt').read_text().strip(), 'declared payload')

    def test_bootstrap_and_runfiles(self):
        for name in ['entry.py', 'renamed_entry.py']:
            binary = Path('lib/registration_fixture', name).resolve()
            with self.subTest(name=name):
                self.assertTrue(binary.is_file(), binary)
                result = subprocess.run([str(binary)], text=True, capture_output=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), 'declared module: declared payload')


if __name__ == '__main__':
    unittest.main()
