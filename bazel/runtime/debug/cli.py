# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""ROS CLI and the same dependency-complete interpreter for interactive debugging."""
import os
import sys


if sys.argv[1:2] == ['--python']:
    os.execv(sys.executable, [sys.executable, *sys.argv[2:]])

from ros2cli.cli import main  # noqa: E402

sys.argv[0] = 'ros2'
sys.exit(main())
