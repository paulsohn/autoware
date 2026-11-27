# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Give unchanged original GPU suites declared data and writable private caches."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from ament_index_python.packages import get_package_share_directory

PACKAGE = 'autoware_calibration_status_classifier'
source = Path(get_package_share_directory(PACKAGE))
models = Path(get_package_share_directory(PACKAGE + '_models'))
with tempfile.TemporaryDirectory(dir=os.environ['TEST_TMPDIR']) as temporary:
    root = Path(temporary)
    model_dir = root / 'autoware_data/ml_models/calibration_status_classifier'
    model_dir.mkdir(parents=True)
    shutil.copyfile(models / 'calibration_status_classifier.onnx', model_dir / 'calibration_status_classifier.onnx')
    share = root / 'share' / PACKAGE
    share.mkdir(parents=True)
    shutil.copytree(source / 'data', share / 'data')
    index = root / 'share/ament_index/resource_index/packages'
    index.mkdir(parents=True)
    (index / PACKAGE).touch()
    environment = dict(os.environ, HOME=str(root), AMENT_PREFIX_PATH=str(root) + ':' + os.environ.get('AMENT_PREFIX_PATH', str(Path.cwd())))
    result = subprocess.run(sys.argv[1:], env=environment)
    if result.returncode:
        sys.exit(result.returncode)
    xml = ET.parse(os.environ['XML_OUTPUT_FILE'])
    if xml.findall('.//skipped') or not xml.findall('.//testcase'):
        raise RuntimeError('Calibration GPU gate requires executed original cases without skips')
