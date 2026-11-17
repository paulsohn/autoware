# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Supply the original pytest suite's sibling-source and generator contracts."""
import os
from pathlib import Path
import shutil
import sys
import tempfile
import xml.etree.ElementTree as ET

import pytest


if __name__ == '__main__':
    lint, specs, generator = [Path(value).absolute() for value in sys.argv[1:]]
    os.environ['INTERFACE_MANIFEST_GENERATOR'] = str(generator)
    with tempfile.TemporaryDirectory(dir=os.environ['TEST_TMPDIR']) as directory:
        common = Path(directory)
        for manifest in [lint, specs]:
            shutil.copytree(manifest.parent, common / ET.parse(manifest).findtext("name"))
        package = common / ET.parse(lint).findtext("name")
        sys.path.insert(0, str(package))
        os.chdir(package)
        # The RCR copyright adapter otherwise redirects cwd checks to the root target.
        os.environ.pop("TEST_TARGET", None)
        os.environ.pop("BAZEL_TARGET", None)
        sys.exit(pytest.main(['test', '-ra', '--junitxml=' + os.environ['XML_OUTPUT_FILE']]))
