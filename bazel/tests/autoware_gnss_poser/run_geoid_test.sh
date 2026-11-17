#!/usr/bin/env bash
# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
export GEOGRAPHICLIB_GEOID_PATH="$(dirname "$(realpath "$2")")"
exec "$1" --gtest_output="xml:$XML_OUTPUT_FILE"
