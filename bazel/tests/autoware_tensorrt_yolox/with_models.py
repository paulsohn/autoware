# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Provide writable engine-cache fixtures to unchanged native YOLOX suites."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from ament_index_python.packages import get_package_prefix, get_package_share_directory

source = Path(get_package_share_directory("autoware_tensorrt_yolox_models"))
prefix = get_package_prefix("autoware_tensorrt_yolox")
files = [
    "yolox_s_car_ped_tl_detector_960_960_batch_1.onnx",
    "yolox-sPlus-opt-pseudoV2-T4-960x960-T4-seg16cls.onnx",
    "car_ped_tl_detector_labels.txt", "label.txt", "semseg_color_map.csv",
]
with tempfile.TemporaryDirectory(dir=os.environ["TEST_TMPDIR"]) as temporary:
    for name in files:
        path = source / name
        with path.open("rb") as stream:
            if stream.read(40).startswith(b"version https://git-lfs"):
                raise RuntimeError("Unmaterialized model: " + name)
        shutil.copyfile(path, Path(temporary) / name)
    env = dict(os.environ, AMENT_PREFIX_PATH=prefix, YOLOX_TEST_DATA_DIR=temporary)
    result = subprocess.run(sys.argv[1:], env=env)
    if result.returncode:
        sys.exit(result.returncode)
    xml = ET.parse(os.environ["XML_OUTPUT_FILE"])
    if xml.findall(".//skipped") or not xml.findall(".//testcase"):
        raise RuntimeError("YOLOX gate requires executed native cases with no skips")
