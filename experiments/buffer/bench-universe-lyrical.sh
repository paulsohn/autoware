#!/usr/bin/env bash
set -e
w=build/agnocast-lyrical
source "$w/install/local_setup.bash"
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
p=src/universe_crop_benchmark
out=${1:?supply a new results directory}
mkdir "$out"
for round in 1 2 3; do
  modes=(cyclone agnocast)
  if (( round == 2 )); then modes=(agnocast cyclone); fi
  for mode in "${modes[@]}"; do
    python3 "$p/run_case.py" "$mode" "$out/$mode-$round.csv" --resources "$out/$mode-$round-resources.csv"
  done
done
python3 src/crop_pipeline_benchmark/summarize.py "$out" cyclone agnocast
