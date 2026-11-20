# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Convert Ubuntu's zstd tar payload into the packaging rule's input format."""
import shutil
import sys

from compression import zstd

with zstd.open(sys.argv[1], 'rb') as source, open(sys.argv[2], 'wb') as destination:
    shutil.copyfileobj(source, destination)
