# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Relocate the real native launcher, including automatic/symlink discovery."""
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

ARCHIVE = Path(sys.argv.pop()).absolute()


class BootstrapTest(unittest.TestCase):
    def test_runfiles_paths(self):
        with tempfile.TemporaryDirectory(prefix='python bootstrap ') as directory:
            root = Path(directory)
            with tarfile.open(ARCHIVE) as archive:
                archive.extractall(root, filter='data')
            (root / 'link one').symlink_to(root / 'hello')
            (root / 'link two').symlink_to(root / 'link one')
            for name, explicit in [('hello', True), ('hello', False), ('link two', False)]:
                with self.subTest(entrypoint=name, explicit_runfiles=explicit):
                    env = {'PATH': '/usr/bin:/bin', 'HOME': str(root)}
                    if explicit:
                        env['RUNFILES_DIR'] = str(root / 'hello.runfiles')
                    result = subprocess.run([str(root / name), 'argument with spaces'], cwd='/',
                                            env=env, text=True, capture_output=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout.strip(), 'argument with spaces')


if __name__ == '__main__':
    unittest.main()
