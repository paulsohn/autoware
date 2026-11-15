#!/usr/bin/env bash
# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

runfiles_dir="${RUNFILES_DIR:-${TEST_SRCDIR:-$0.runfiles}}"
hook="$runfiles_dir/_main/src/core/agnocast/agnocast_heaphook/libagnocast_heaphook.so"
test -r "$hook"
# Apply the hook only to the test process: preloading the shell would initialize
# Agnocast before the test's shared-library closure is available.
export LD_PRELOAD="$hook"
exec "$@"
