# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run a native program with declared TensorRT builder and GNU runtime libraries."""
import os
from pathlib import Path
import sys

from python.runfiles import runfiles

_BAKED_RUNTIME = None
r = runfiles.Create()
runtime = _BAKED_RUNTIME if _BAKED_RUNTIME is not None else sys.argv[1:5]
paths = [r.Rlocation(value) for value in runtime]
if not all(paths):
    raise RuntimeError('TensorRT executable/runtime runfiles are missing')
env = os.environ.copy()
directories = [str(Path(path).parent) for path in paths[1:]]
env['LD_LIBRARY_PATH'] = ':'.join(directories + [env.get('LD_LIBRARY_PATH', '')])
# Vendor RTTI and internal exception handling must bind GNU's C++ ABI first.
env['LD_PRELOAD'] = ':'.join(filter(None, [paths[2], env.get('LD_PRELOAD', '')]))
if 'XML_OUTPUT_FILE' in env:
    env['GTEST_OUTPUT'] = 'xml:' + env['XML_OUTPUT_FILE']
arguments = sys.argv[1:] if _BAKED_RUNTIME is not None else sys.argv[5:]
command = [paths[0], *arguments]
if arguments[:1] == ['--run-under']:
    wrapper = r.Rlocation(arguments[1])
    if not wrapper:
        raise RuntimeError('TensorRT test fixture runner is missing')
    command = [wrapper, paths[0], *arguments[2:]]
os.execve(command[0], command, env)
