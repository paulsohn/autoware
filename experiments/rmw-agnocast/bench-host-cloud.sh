#!/usr/bin/env bash
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/rmw-agnocast/install/local_setup.bash
source build/rmw-agnocast/load-install/local_setup.bash
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
if [[ ${1:-} == --check-cold ]]; then
  exec python3 experiments/rmw-agnocast/check-cold-diagnostic.py
fi
out=${1:?fresh results directory}
mkdir "$out"
baseline=agnocast
control=host
variant=fixed
if [[ ${2:-} == --view ]]; then variant=view; fi
if [[ ${2:-} == --loan ]]; then variant=loan; control=view; fi
if [[ ${2:-} == --scope ]]; then variant=scope; control=loan; fi
if [[ ${2:-} == --wire ]]; then variant=v2; control=legacy; fi
if [[ ${2:-} == --bridge ]]; then baseline=off; variant=idle; control=active; fi
if [[ ${2:-} == --references ]]; then variant=cyclone; control=zenoh; fi
if [[ ${2:-} == --profile-bridge ]]; then
  for round in 1 2 3; do
    AGNOCAST_BRIDGE_PROFILE="$out/timing-$round.csv" python3 src/universe_crop_benchmark/run_case.py fixed \
      "$out/active-$round.csv" --payload-scope --bridge active --resources "$out/active-$round-resources.csv"
  done
  exit 0
fi
for round in 1 2 3; do
  case $round in
    1) modes=("$baseline" "$variant" "$control");;
    2) modes=("$variant" "$control" "$baseline");;
    3) modes=("$control" "$baseline" "$variant");;
  esac
  if [[ ${3:-} == --reverse ]]; then modes=("${modes[2]}" "${modes[1]}" "${modes[0]}"); fi
  for mode in "${modes[@]}"; do
    opts=()
    transport=$mode
    if [[ $mode == host ]]; then transport=fixed; opts+=(--host-cloud); fi
    if [[ $mode == host && $variant == view ]]; then opts+=(--typed-input); fi
    if [[ $mode == off || $mode == idle || $mode == active ]]; then
      transport=fixed
      opts+=(--payload-scope)
      if [[ $mode != off ]]; then opts+=(--bridge "$mode"); fi
    fi
    wire_env=()
    if [[ $mode == legacy ]]; then wire_env+=(AGNOCAST_RMW_LEGACY_WIRE=1); fi
    if [[ $mode == scope || $mode == v2 || $mode == legacy ]]; then transport=fixed; opts+=(--payload-scope); fi
    if [[ $mode == loan ]]; then transport=fixed; opts+=(--host-loan); fi
    if [[ $mode == view ]]; then transport=fixed; opts+=(--host-view); fi
    env -u AGNOCAST_RMW_LEGACY_WIRE "${wire_env[@]}" python3 src/universe_crop_benchmark/run_case.py "$transport" "$out/$mode-$round.csv" \
      "${opts[@]}" --resources "$out/$mode-$round-resources.csv"
  done
done
python3 src/crop_pipeline_benchmark/summarize.py "$out" "$baseline" "$variant" "$control"
