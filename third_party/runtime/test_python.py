# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise imports and the Python environment through a non-Python product."""
import os
import subprocess
import sys
import unittest

import runtime_message


class RuntimePython(unittest.TestCase):
    def test_import_through_product(self):
        self.assertEqual(runtime_message.VALUE, "message reached through a filegroup")

    def test_bundled_interpreter_preserves_imports(self):
        environment = dict(os.environ)
        environment.pop('PYTHONPATH', None)
        result = subprocess.run(
            [sys.executable, '-c', 'import runtime_message; print(runtime_message.VALUE)'],
            env=environment, text=True, capture_output=True, check=True,
        )
        self.assertEqual(result.stdout.strip(), runtime_message.VALUE)


if __name__ == '__main__':
    unittest.main()
