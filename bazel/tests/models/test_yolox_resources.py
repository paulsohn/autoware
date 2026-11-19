# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check actual model bytes and a standalone relocated ament resource bundle."""
import hashlib
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

from ament_index_python.packages import get_package_share_directory

PACKAGE = "autoware_tensorrt_yolox_models"


class ModelResources(unittest.TestCase):
    def verify(self, share):
        rows = (share / "SHA256SUMS").read_text().splitlines()
        self.assertEqual(len(rows), 5)
        for row in rows:
            digest, name = row.split()
            with (share / name).open("rb") as stream:
                self.assertEqual(hashlib.file_digest(stream, "sha256").hexdigest(), digest, name)
        self.assertTrue((share / "label.txt").is_file())
        self.assertTrue((share / "semseg_color_map.csv").is_file())

    def test_indexed_materialized_models(self):
        self.verify(Path(get_package_share_directory(PACKAGE)))

    def test_relocated_bundle_without_workspace_lookup(self):
        original = Path(get_package_share_directory(PACKAGE))
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary)
            share = prefix / "share" / PACKAGE
            shutil.copytree(original, share)
            index = prefix / "share/ament_index/resource_index/packages"
            index.mkdir(parents=True)
            (index / PACKAGE).touch()
            with patch.dict(os.environ, {"AMENT_PREFIX_PATH": str(prefix)}):
                resolved = Path(get_package_share_directory(PACKAGE))
                self.assertEqual(resolved, share)
                self.verify(resolved)


if __name__ == "__main__":
    unittest.main()
