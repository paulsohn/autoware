# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Expose the declared OSTree SDK without installing host packages."""
import subprocess
import sys
import tempfile

from third_party.ostree.tool import command, environment, extract_sdk

with tempfile.TemporaryDirectory(prefix='ostree-sdk-') as temp:
    sdk = extract_sdk(sys.argv[1], temp)
    result = subprocess.run(command(sdk, *sys.argv[2:]), env=environment(sdk))
    sys.exit(result.returncode)
