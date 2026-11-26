# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Validate materialized model bytes and relocatable ament lookup."""
import hashlib
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch
from ament_index_python.packages import get_package_share_directory

PACKAGE = "autoware_yabloc_pose_initializer_models"

class ModelResources(unittest.TestCase):
    def verify(self, share):
        rows = (share / "SHA256SUMS").read_text().splitlines()
        self.assertEqual(len(rows), 2)
        for row in rows:
            digest, name = row.split()
            data = (share / name).read_bytes()
            self.assertFalse(data.startswith(b"version https://git-lfs.github.com/spec/"))
            self.assertEqual(hashlib.sha256(data).hexdigest(), digest, name)
        self.assertTrue((share / "deploy_metadata.yaml").is_file())

    def test_indexed_payloads(self):
        self.verify(Path(get_package_share_directory(PACKAGE)))

    def test_relocated_prefix(self):
        source = Path(get_package_share_directory(PACKAGE))
        with tempfile.TemporaryDirectory() as tmp:
            prefix = Path(tmp)
            share = prefix / "share" / PACKAGE
            shutil.copytree(source, share)
            index = prefix / "share/ament_index/resource_index/packages"
            index.mkdir(parents=True)
            (index / PACKAGE).touch()
            with patch.dict(os.environ, {"AMENT_PREFIX_PATH": str(prefix)}):
                resolved = Path(get_package_share_directory(PACKAGE))
                self.assertEqual(resolved, share)
                self.verify(resolved)

if __name__ == "__main__":
    unittest.main()
