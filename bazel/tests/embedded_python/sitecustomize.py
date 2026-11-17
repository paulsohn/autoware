# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0

"""Keep wheel extension exception runtimes separate from the native LLVM ABI."""

import os
import sys

sys.setdlopenflags(sys.getdlopenflags() | os.RTLD_DEEPBIND)
