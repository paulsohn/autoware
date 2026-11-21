#!/usr/bin/env python3
# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path

from fixture_lib import VALUE

print(VALUE + ': ' + Path(__file__).with_name('payload.txt').read_text().strip())
