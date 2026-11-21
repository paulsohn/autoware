# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0

"""Run the original ROS suite with its declared writable bag fixtures."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

executable = str(Path(sys.argv[1]).resolve())
fixtures = Path(sys.argv[2]).resolve().parent.parent
env = os.environ.copy()
for name, default in [('LD_LIBRARY_PATH', 'lib'), ('AMENT_PREFIX_PATH', str(Path.cwd()))]:
    env[name] = os.pathsep.join(str(Path(part).resolve())
                                for part in env.get(name, default).split(os.pathsep) if part)
command = [executable]
if os.environ.get('XML_OUTPUT_FILE'):
    command += ['--gtest_output=xml:' + str(Path(os.environ['XML_OUTPUT_FILE']).resolve())]
with tempfile.TemporaryDirectory(prefix='grid-map-ros-') as directory:
    for name in ['double_chatter', 'test_multitopic']:
        shutil.copytree(fixtures / name, Path(directory) / name)
    sys.exit(subprocess.call(command, env=env, cwd=directory))
