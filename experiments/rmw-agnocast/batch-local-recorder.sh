#!/usr/bin/env bash
# Run on the provisioned laptop from the colcon worktree.
set -eu
out=${1:?fresh output directory}
mkdir "$out"
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
cases=(unrouted direct-running running direct-stalled stalled
       stalled direct-stalled running direct-running
       running direct-running stalled direct-stalled
       direct-stalled-depth32 stalled-depth32 stalled-depth32 direct-stalled-depth32
       direct-stalled-both32 stalled-both32 stalled-both32 direct-stalled-both32)
failed=0
for i in "${!cases[@]}"; do
  mode=${cases[i]}
  path="$out/$i-$mode"
  rc=0
  BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout -k 5 80 \
    bash experiments/rmw-agnocast/run-live-recorder.sh "$mode" "$path" >"$path.txt" 2>&1 || rc=$?
  echo "$i $mode $rc" | tee -a "$out/status.txt"
  if [[ $rc == 75 ]]; then exit "$rc"; fi
  if [[ $rc != 0 ]]; then failed=1; fi
done
exit "$failed"
