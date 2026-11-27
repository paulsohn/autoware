# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check declared model payloads and a relocated, isolated ament package."""
import hashlib
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

from ament_index_python.packages import get_package_share_directory

PACKAGE = sys.argv.pop(1)
COUNT = int(sys.argv.pop(1))


class IndexedModels(unittest.TestCase):
    def verify(self, share):
        rows = (share / 'SHA256SUMS').read_text().splitlines()
        self.assertEqual(len(rows), COUNT)
        for row in rows:
            digest, name = row.split()
            payload = (share / name).read_bytes()
            self.assertFalse(payload.startswith(b'version https://git-lfs.github.com/spec/'))
            self.assertEqual(hashlib.sha256(payload).hexdigest(), digest, name)
        self.assertTrue((share / 'deploy_metadata.yaml').is_file())
        self.assertTrue((share / 'MODEL_PROVENANCE.md').is_file())

    def test_materialized_indexed_payloads(self):
        self.verify(Path(get_package_share_directory(PACKAGE)))

    def test_isolated_relocated_prefix(self):
        source = Path(get_package_share_directory(PACKAGE))
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary)
            share = prefix / 'share' / PACKAGE
            shutil.copytree(source, share)
            index = prefix / 'share/ament_index/resource_index/packages'
            index.mkdir(parents=True)
            (index / PACKAGE).touch()
            with patch.dict(os.environ, {'AMENT_PREFIX_PATH': str(prefix)}):
                resolved = Path(get_package_share_directory(PACKAGE))
                self.assertEqual(resolved, share)
                self.verify(resolved)


if __name__ == '__main__':
    unittest.main()
