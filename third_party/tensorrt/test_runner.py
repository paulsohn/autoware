# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run a native test with declared TensorRT builder and GNU runtime libraries."""
import os
from pathlib import Path
import sys

from python.runfiles import runfiles

r = runfiles.Create()
paths = [r.Rlocation(value) for value in sys.argv[1:5]]
if not all(paths):
    raise RuntimeError('TensorRT test executable/runtime runfiles are missing')
env = os.environ.copy()
directories = [str(Path(path).parent) for path in paths[1:]]
env['LD_LIBRARY_PATH'] = ':'.join(directories + [env.get('LD_LIBRARY_PATH', '')])
env['GTEST_OUTPUT'] = 'xml:' + env['XML_OUTPUT_FILE']
os.execve(paths[0], [paths[0], *sys.argv[5:]], env)
