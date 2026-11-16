#!/usr/bin/env bash
# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
binary="$1"
if [[ "$2" == present ]]; then
  export GEOGRAPHICLIB_GEOID_PATH="$(dirname "$(realpath "$3")")"
  filter='-*ThrowsWhenDatasetMissing'
else
  export GEOGRAPHICLIB_GEOID_PATH="$TEST_TMPDIR/absent-geoid"
  mkdir -p "$GEOGRAPHICLIB_GEOID_PATH"
  filter='GeographyUtils.*ThrowsWhenDatasetMissing'
fi
exec "$binary" --gtest_filter="$filter" --gtest_output="xml:$XML_OUTPUT_FILE"
