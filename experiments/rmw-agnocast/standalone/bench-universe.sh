#!/usr/bin/env bash
set -eu
out=${1:?fresh output directory}
mkdir "$out"
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)" BUFFER_RUNTIME_ONLY=1 SYNC_SHORT_WAIT=1
for round in 1 2; do
  modes=(native standalone poll cyclone zenoh)
  if [[ $round == 2 ]]; then modes=(zenoh cyclone poll standalone native); fi
  for mode in "${modes[@]}"; do
    log="$out/$mode-$round.log"
    unset AGNOCAST_RMW_POLL_TOPIC
    if [[ $mode == standalone || $mode == poll ]]; then
      if [[ $mode == poll ]]; then export AGNOCAST_RMW_POLL_TOPIC=/output; fi
      command=(bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp
        --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws timeout -k 8 65
        bash experiments/rmw-agnocast/standalone/run-universe.sh)
    else
      command=(timeout -k 8 65 bash experiments/rmw-agnocast/run-pipeline.sh "$mode")
    fi
    if ! experiments/buffer/run-lyrical.sh "${command[@]}" >"$log" 2>&1; then tail -30 "$log"; exit 1; fi
    test "$(grep -c 'sample,[0-9]' "$log")" = 24
  done
done
