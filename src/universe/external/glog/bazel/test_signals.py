# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Supply the build layout expected by the original signal-handler shell driver."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from python.runfiles import runfiles

resolver = runfiles.Create()
binary, driver = [str(Path(resolver.Rlocation(arg)).resolve()) for arg in sys.argv[1:]]
with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    (work / ".libs").mkdir()
    (work / ".libs/signalhandler_unittest").symlink_to(binary)
    result = subprocess.run(["/bin/sh", driver], cwd=work, env=os.environ,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, timeout=45)
    print(result.stdout)
    if result.returncode or "PASS (" in result.stdout:
        raise RuntimeError("The required signal tests failed or were skipped")
    assert result.stdout.strip().endswith("PASS"), result.stdout
