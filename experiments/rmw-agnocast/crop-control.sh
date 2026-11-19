#!/usr/bin/env bash
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/rmw-agnocast/install/local_setup.bash
source build/rmw-agnocast/load-install/local_setup.bash
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
out=${1:?fresh results directory}
mkdir "$out"
if [[ ${2:-} == --codec ]]; then
  AGNOCAST_RMW_TRACE_CODEC=1 python3 src/universe_crop_benchmark/run_case.py rmw-agnocast "$out/frames.csv" 2>"$out/codec.log"
  exit 0
fi
for round in 1 2 3; do
  cases=(rmw-agnocast rmw-delegated)
  if [[ $round == 2 ]]; then cases=(rmw-delegated rmw-agnocast); fi
  for mode in "${cases[@]}"; do
    python3 src/universe_crop_benchmark/run_case.py "$mode" "$out/$mode-$round.csv" --resources "$out/$mode-$round-resources.csv"
  done
done
python3 src/crop_pipeline_benchmark/summarize.py "$out" rmw-agnocast rmw-delegated
if [[ -n ${TRACE_BIN:-} ]]; then
  for mode in rmw-agnocast rmw-delegated; do
    mkdir "$out/trace-$mode"
    "$TRACE_BIN" -ff -s 1 -e trace=execve,ioctl,sendto,sendmsg,sendmmsg -o "$out/trace-$mode/thread" \
      python3 src/universe_crop_benchmark/run_case.py "$mode" "$out/trace-$mode/frames.csv"
  done
fi
