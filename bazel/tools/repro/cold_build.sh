#!/bin/bash
# Cold build of every Autoware package + the runtime image, then record output digests (docs/bazel/PLAN.md, P2 check).
# Usage: cold_build.sh <output_base> <result_dir> [extra bazel build flags...]
# Bypasses the disk cache, the remote cache and (by using a fresh output base) the action cache; keeps the repository
# cache (sha256-pinned downloads only). Compare two runs with compare_digests.sh.
# Progress: tail -f <result_dir>/progress.log (one "[done / total] ..." line per minute).
set -euo pipefail
output_base=$1 result=$2
shift 2
cd "$(dirname "$0")/../../.."
mkdir -p "$result"
targets=$(mktemp)
{ cat bazel/autoware_packages.txt; echo //images:runtime; } > "$targets"
bz=(bazel --output_base="$output_base")
start=$(date +%s)
"${bz[@]}" build --disk_cache= --remote_cache= --experimental_convenience_symlinks=ignore \
  --curses=no --show_progress_rate_limit=60 \
  --target_pattern_file="$targets" "$@" 2>&1 | tee "$result/progress.log" | tail -n 30 > "$result/build.log"
echo "elapsed_s=$(( $(date +%s) - start ))" >> "$result/build.log"
bin=$("${bz[@]}" info --experimental_convenience_symlinks=ignore bazel-bin 2>/dev/null)
# The runtime image's digest covers every file installed in it.
# (grep, not python: the dev image has no python3.)
grep -o 'sha256:[0-9a-f]\{64\}' "$bin/images/runtime/index.json" | head -n 1 > "$result/image_digest.txt"
# Every regular output file (not runfiles trees, which are symlinks), for finding the action that differs.
(cd "$bin/.." && find . -path '*.runfiles' -prune -o -type f -print0 | sort -z | xargs -0 sha256sum) > "$result/outputs.sha256"
rm -f "$targets"
cat "$result/image_digest.txt"
