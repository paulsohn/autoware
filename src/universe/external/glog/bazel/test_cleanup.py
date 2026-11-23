# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run the original CMake cleanup checks around the original executables."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from python.runfiles import runfiles

resolver = runfiles.Create()
binaries = [str(Path(resolver.Rlocation(arg)).resolve()) for arg in sys.argv[1:]]
sys.argv = sys.argv[:1]


class CleanupDrivers(unittest.TestCase):
    def check_cleanup(self, index, pattern, delay):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "test_subdir").mkdir()
            env = dict(os.environ, GOOGLE_LOG_DIR=directory + "/")
            env.pop("GTEST_OUTPUT", None)
            for _ in range(3):
                subprocess.run([Path(binaries[index]).name.removeprefix("test_") + "_unittest",
                                "--log_dir=" + directory + "/"], executable=binaries[index], cwd=work,
                               env=env, check=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=30)
                time.sleep(delay)
            self.assertEqual(len(list(work.glob(pattern))), 1)

    def test_immediate(self):
        self.check_cleanup(0, "*.foobar", 1)

    def test_absolute_prefix(self):
        self.check_cleanup(1, "test_cleanup_*.barfoo", 1)

    def test_relative_prefix(self):
        self.check_cleanup(2, "test_subdir/test_cleanup_*.relativefoo", 2)


if __name__ == "__main__":
    unittest.main()
