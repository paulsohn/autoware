# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Isolate a solver generator's working directory and publish its declared outputs."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    generator, source, renderer, libstdcxx, libgcc, *outputs = sys.argv[1:]
    generator, source, renderer = [str(Path(p).resolve()) for p in (generator, source, renderer)]
    destinations = [(path, Path(output).resolve()) for path, output in (item.split('=', 1) for item in outputs)]
    env = dict(os.environ, ACADOS_SOURCE_DIR=source, TERA_PATH=renderer, MPLBACKEND='Agg',
               LD_LIBRARY_PATH=os.pathsep.join(str(Path(p).resolve()) for p in [libstdcxx, libgcc]))
    with tempfile.TemporaryDirectory() as work:
        env['MPLCONFIGDIR'] = str(Path(work) / 'matplotlib')
        subprocess.run([generator], cwd=work, env=env, check=True)
        for path, destination in destinations:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(Path(work) / path, destination)


if __name__ == '__main__':
    main()
