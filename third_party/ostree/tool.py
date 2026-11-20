# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run the pinned OSTree SDK using its own loader and libraries."""
from pathlib import Path
import subprocess
import tarfile


def extract_sdk(archive, destination):
    with tarfile.open(archive) as source:
        source.extractall(destination, filter='data')
    sdk = Path(destination).resolve()
    libraries = sdk / 'usr/lib/x86_64-linux-gnu'
    result = subprocess.run([str(libraries / 'ld-linux-x86-64.so.2'), '--inhibit-cache',
                             '--library-path', str(libraries), '--list', str(sdk / 'usr/bin/ostree')],
                            env=environment(sdk), capture_output=True, text=True, check=True)
    for line in result.stdout.splitlines():
        if '=>' in line:
            resolved = line.split('=>', 1)[1].split('(', 1)[0].strip()
            if not Path(resolved).resolve().is_relative_to(sdk):
                raise RuntimeError('OSTree dependency escaped the declared SDK: ' + line)
    return sdk


def command(sdk, *args):
    libraries = sdk / 'usr/lib/x86_64-linux-gnu'
    return [str(libraries / 'ld-linux-x86-64.so.2'), '--inhibit-cache', '--library-path',
            str(libraries), str(sdk / 'usr/bin/ostree'), *map(str, args)]


def environment(sdk):
    return {'PATH': '/usr/bin:/bin', 'HOME': str(sdk / 'home'), 'LC_ALL': 'C',
            'GIO_USE_VFS': 'local', 'GIO_MODULE_DIR': str(sdk / 'no-modules'), 'GIO_EXTRA_MODULES': ''}


def run(sdk, *args, **kwargs):
    try:
        return subprocess.run(command(sdk, *args), env=environment(sdk), check=True,
                              text=True, capture_output=True, **kwargs)
    except subprocess.CalledProcessError as error:
        error.add_note(error.stderr)
        raise
