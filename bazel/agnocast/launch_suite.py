# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Apply the heap hook to launched processes after Python's bootstrap execs."""

import os
from pathlib import Path

os.environ['LD_PRELOAD'] = os.environ['AGNOCAST_TEST_HEAPHOOK']
__file__ = os.environ['AGNOCAST_LAUNCH_SOURCE']
exec(compile(Path(__file__).read_text(), __file__, 'exec'))
