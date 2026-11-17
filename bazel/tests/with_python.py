# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0

"""Run a native embedded-Python test with Bazel's interpreter and wheel paths."""

import os
from pathlib import Path
import subprocess
import sys

from python.runfiles import runfiles


def main():
    command = [str(Path(sys.argv[1]).resolve()), *sys.argv[2:]]
    env = dict(os.environ)
    env["PYTHONHOME"] = sys.base_prefix
    python_paths = [str(Path(__file__).parent / "embedded_python"), *sys.path]
    env["PYTHONPATH"] = os.pathsep.join(str(Path(p).resolve()) for p in python_paths)
    env["MPLBACKEND"] = "Agg"
    output = Path(env["TEST_UNDECLARED_OUTPUTS_DIR"])
    env["MPLCONFIGDIR"] = str(Path(env["TEST_TMPDIR"]) / "matplotlib")
    for name in ("AMENT_PREFIX_PATH", "LD_LIBRARY_PATH"):
        if name in env:
            env[name] = os.pathsep.join(
                str(Path(p).resolve()) for p in env[name].split(os.pathsep) if p
            )
    files = runfiles.Create()
    runtime_dirs = [
        str(Path(files.Rlocation(path)).resolve().parent)
        for path in (
            "python_libstdcxx_linux_x86_64/lib/libstdc++.so.6.0.30",
            "python_libgcc_linux_x86_64/lib/libgcc_s.so.1",
        )
    ]
    env["LD_LIBRARY_PATH"] = os.pathsep.join(
        runtime_dirs + ([env["LD_LIBRARY_PATH"]] if env.get("LD_LIBRARY_PATH") else [])
    )
    env["GTEST_OUTPUT"] = "xml:" + env["XML_OUTPUT_FILE"]
    return subprocess.call(command, env=env, cwd=output)


if __name__ == "__main__":
    sys.exit(main())
