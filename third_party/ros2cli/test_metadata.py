# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check generated distribution versions and plugins against upstream setup.py."""
import ast
from importlib.metadata import distribution
from pathlib import Path
import sys
import unittest

SOURCES = list(zip(sys.argv[1::2], sys.argv[2::2]))
del sys.argv[1:]


class MetadataTest(unittest.TestCase):
    def test_dependency_metadata(self):
        self.assertTrue(SOURCES)
        for package, filename in SOURCES:
            with self.subTest(package=package):
                tree = ast.parse(Path(filename).read_text())
                setup = next(node for node in ast.walk(tree) if isinstance(node, ast.Call)
                             and isinstance(node.func, ast.Name) and node.func.id == 'setup')
                keywords = {keyword.arg: keyword.value for keyword in setup.keywords}
                metadata = distribution(package)
                self.assertEqual(metadata.metadata['Name'], package)
                self.assertEqual(metadata.version, ast.literal_eval(keywords['version']))
                expected = set()
                for group, entries in ast.literal_eval(keywords['entry_points']).items():
                    for entry in entries:
                        name, value = entry.split('=', 1)
                        expected.add((group, name.strip(), value.strip()))
                self.assertEqual({(entry.group, entry.name, entry.value) for entry in metadata.entry_points}, expected)
        print(f'Validated {len(SOURCES)} dependency-owned distribution metadata sets')


if __name__ == '__main__':
    unittest.main()
