#!/usr/bin/env bash
# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

runfiles_dir="${TEST_SRCDIR:-${RUNFILES_DIR:-$0.runfiles}}"
source "$runfiles_dir/bazel_tools/tools/bash/runfiles/runfiles.bash"
# The main module's canonical name is empty in the repository mapping.
hook="$(rlocation agnocast_heaphook/libagnocast_heaphook.so '')"
test -r "$hook"
# Apply the hook only to the test process: preloading the shell would initialize
# Agnocast before the test's shared-library closure is available.
export LD_PRELOAD="$hook"
exec "$@"
