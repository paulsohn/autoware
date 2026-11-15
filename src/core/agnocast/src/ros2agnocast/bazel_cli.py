# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run ROS CLI with the Agnocast extensions in its Bazel dependency closure."""

import sys
from ros2cli.cli import main

if __name__ == '__main__':
    sys.exit(main())
