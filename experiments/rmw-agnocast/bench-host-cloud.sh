#!/usr/bin/env bash
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/rmw-agnocast/install/local_setup.bash
source build/rmw-agnocast/load-install/local_setup.bash
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
out=${1:?fresh results directory}
mkdir "$out"
control=host
variant=fixed
if [[ ${2:-} == --view ]]; then variant=view; fi
if [[ ${2:-} == --loan ]]; then variant=loan; control=view; fi
for round in 1 2 3; do
  case $round in
    1) modes=(agnocast "$variant" "$control");;
    2) modes=("$variant" "$control" agnocast);;
    3) modes=("$control" agnocast "$variant");;
  esac
  if [[ ${3:-} == --reverse ]]; then modes=("${modes[2]}" "${modes[1]}" "${modes[0]}"); fi
  for mode in "${modes[@]}"; do
    opts=()
    transport=$mode
    if [[ $mode == host ]]; then transport=fixed; opts+=(--host-cloud); fi
    if [[ $mode == host && $variant == view ]]; then opts+=(--typed-input); fi
    if [[ $mode == loan ]]; then transport=fixed; opts+=(--host-loan); fi
    if [[ $mode == view ]]; then transport=fixed; opts+=(--host-view); fi
    python3 src/universe_crop_benchmark/run_case.py "$transport" "$out/$mode-$round.csv" \
      "${opts[@]}" --resources "$out/$mode-$round-resources.csv"
  done
done
python3 src/crop_pipeline_benchmark/summarize.py "$out" agnocast "$variant" "$control"
