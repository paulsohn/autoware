#!/usr/bin/env bash
set -eu
out=${1:?fresh output directory}
bash experiments/rmw-agnocast/standalone/check-universe-o3.sh perception
mkdir "$out"
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)" BUFFER_RUNTIME_ONLY=1 UNIVERSE_RELEASE_CONTROLS=1
for round in 1 2; do
  modes=(native standalone cyclone zenoh)
  if [[ $round == 2 ]]; then modes=(zenoh cyclone standalone native); fi
  for mode in "${modes[@]}"; do
    isolate=()
    if [[ $mode == standalone ]]; then
      isolate=(bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws)
    fi
    status=0
    experiments/buffer/run-lyrical.sh "${isolate[@]}" timeout -k 12 150 bash experiments/rmw-agnocast/run-sustained.sh "$mode" "$out/$mode-$round" >"$out/$mode-$round.log" 2>&1 || status=$?
    echo "$mode-$round $status" >> "$out/status.txt"
  done
done
! grep -v ' 0$' "$out/status.txt"
