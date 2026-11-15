#!/usr/bin/env bash
# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
runfiles_dir="${TEST_SRCDIR:-${RUNFILES_DIR:-$0.runfiles}}"
source "$runfiles_dir/bazel_tools/tools/bash/runfiles/runfiles.bash"
prefix="$runfiles_dir/_main"
export AMENT_PREFIX_PATH="$prefix"
export LD_LIBRARY_PATH="$prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export AGNOCAST_BRIDGE_PLUGINS_PATH="$prefix/lib"
export AGNOCAST_BRIDGE_MODE=on
export AGNOCAST_TEST_HEAPHOOK="$(rlocation agnocast_heaphook/libagnocast_heaphook.so '')"
if [[ "${AGNOCAST_TEST_NO_PRELOAD:-0}" != 1 ]]; then
  export LD_PRELOAD="$AGNOCAST_TEST_HEAPHOOK"
fi
exec "$@"
