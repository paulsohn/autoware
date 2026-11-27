# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Supply indexed traffic models and writable caches to unchanged native suites."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from ament_index_python.packages import get_package_share_directory

source = Path(get_package_share_directory('autoware_traffic_light_classifier_models'))
with tempfile.TemporaryDirectory(dir=os.environ['TEST_TMPDIR']) as temporary:
    for path in source.iterdir():
        if path.suffix in ['.onnx', '.txt', '.yaml']:
            shutil.copyfile(path, Path(temporary) / path.name)
    environment = dict(os.environ, TLC_TEST_DATA_DIR=temporary)
    result = subprocess.run(sys.argv[1:], env=environment)
    if result.returncode:
        sys.exit(result.returncode)
    xml = ET.parse(os.environ['XML_OUTPUT_FILE'])
    if xml.findall('.//skipped') or not xml.findall('.//testcase'):
        raise RuntimeError('Traffic classifier GPU gate requires executed original cases with no skips')
