# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""The fixture runner must reject native suites that execute no useful cases."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from python.runfiles import runfiles

RUNNER = runfiles.Create().Rlocation(sys.argv.pop(1))


class FixtureRejection(unittest.TestCase):
    def assert_rejected(self, xml):
        with tempfile.TemporaryDirectory() as temporary:
            env = dict(os.environ, TEST_TMPDIR=temporary, XML_OUTPUT_FILE=temporary + "/native.xml")
            command = ('import os; from pathlib import Path; '
                       'Path(os.environ["XML_OUTPUT_FILE"]).write_text(' + repr(xml) + ')')
            result = subprocess.run([RUNNER, sys.executable, "-c", command], env=env,
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("requires executed native cases with no skips", result.stderr)

    def test_skipped_native_suite(self):
        self.assert_rejected("<testsuites><testsuite><testcase><skipped/></testcase></testsuite></testsuites>")

    def test_empty_native_suite(self):
        self.assert_rejected("<testsuites/>")


if __name__ == "__main__":
    unittest.main()
