#!/usr/bin/env bash
set -e
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
p=src/crop_pipeline_benchmark
args=()
out="$p/results"
if [[ ${1:-} == two-edge ]]; then
  source build/two/install/local_setup.bash
  args=(--two-edge)
  out="$p/two-edge-results"
fi
mkdir -p "$out"
for round in 1 2 3; do
  case $round in
    1) cases="cyclone-cpu zenoh-cuda zenoh-shmem zenoh-cpu";;
    2) cases="zenoh-shmem zenoh-cpu cyclone-cpu zenoh-cuda";;
    3) cases="zenoh-cpu zenoh-shmem zenoh-cuda cyclone-cpu";;
  esac
  for name in $cases; do
    rmw=rmw_zenoh_cpp
    if [[ $name == cyclone-cpu ]]; then rmw=rmw_cyclonedds_cpp; fi
    python3 "$p/run_case.py" "$rmw" "${name#*-}" "$out/$name-$round.csv" "${args[@]}"
  done
done
python3 "$p/summarize.py" "$out"
