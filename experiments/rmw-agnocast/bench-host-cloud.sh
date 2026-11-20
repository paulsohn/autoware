#!/usr/bin/env bash
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/rmw-agnocast/install/local_setup.bash
source build/rmw-agnocast/load-install/local_setup.bash
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
out=${1:?fresh results directory}
mkdir "$out"
for round in 1 2 3; do
  case $round in
    1) modes=(agnocast fixed host);;
    2) modes=(fixed host agnocast);;
    3) modes=(host agnocast fixed);;
  esac
  for mode in "${modes[@]}"; do
    opts=()
    transport=$mode
    if [[ $mode == host ]]; then transport=fixed; opts+=(--host-cloud); fi
    python3 src/universe_crop_benchmark/run_case.py "$transport" "$out/$mode-$round.csv" \
      "${opts[@]}" --resources "$out/$mode-$round-resources.csv"
  done
done
python3 src/crop_pipeline_benchmark/summarize.py "$out" agnocast fixed host
