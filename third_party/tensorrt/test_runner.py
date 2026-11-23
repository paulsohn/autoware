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
# Vendor RTTI and internal exception handling must bind GNU's C++ ABI first.
env['LD_PRELOAD'] = ':'.join(filter(None, [paths[2], env.get('LD_PRELOAD', '')]))
env['GTEST_OUTPUT'] = 'xml:' + env['XML_OUTPUT_FILE']
arguments = sys.argv[5:]
command = [paths[0], *arguments]
if arguments[:1] == ['--run-under']:
    wrapper = r.Rlocation(arguments[1])
    if not wrapper:
        raise RuntimeError('TensorRT test fixture runner is missing')
    command = [wrapper, paths[0], *arguments[2:]]
os.execve(command[0], command, env)
