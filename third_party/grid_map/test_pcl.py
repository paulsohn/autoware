# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Supply the original PCL suite's indexed, writable test_data directory."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

executable = str(Path(sys.argv[1]).resolve())
fixtures = Path(sys.argv[2]).resolve().parent
env = os.environ.copy()
for name, default in [('LD_LIBRARY_PATH', 'lib'), ('AMENT_PREFIX_PATH', str(Path.cwd()))]:
    env[name] = os.pathsep.join(str(Path(p).resolve()) for p in env.get(name, default).split(os.pathsep) if p)
command = [executable]
if os.environ.get('XML_OUTPUT_FILE'):
    command += ['--gtest_output=xml:' + str(Path(os.environ['XML_OUTPUT_FILE']).resolve())]
with tempfile.TemporaryDirectory(prefix='grid-map-pcl-') as directory:
    prefix = Path(directory)
    marker = prefix / 'share/ament_index/resource_index/packages/grid_map_pcl'
    marker.parent.mkdir(parents=True)
    marker.touch()
    shutil.copytree(fixtures, prefix / 'share/grid_map_pcl/test_data')
    env['AMENT_PREFIX_PATH'] = directory + os.pathsep + env['AMENT_PREFIX_PATH']
    sys.exit(subprocess.call(command, env=env, cwd=directory))
