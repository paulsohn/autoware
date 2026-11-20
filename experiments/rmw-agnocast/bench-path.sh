#!/usr/bin/env bash
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/rmw-agnocast/install/local_setup.bash
source build/rmw-agnocast/msg-install/local_setup.bash
source build/rmw-agnocast/load-install/local_setup.bash
probe=/ws/build/rmw-agnocast/load-install/agnocast_rmw_loan_probe/lib/agnocast_rmw_loan_probe/path_benchmark
hook=/ws/build/agnocast-lyrical/install/agnocastlib/lib/libagnocast_heaphook.so
export AGNOCAST_BRIDGE_MODE=OFF AGNOCAST_NO_DISCOVERY_AGENT=1 ROS_DISABLE_LOANED_MESSAGES=0 AGNOCAST_RMW_TOPIC=/path_latency
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
out=$(realpath -m "${1:?fresh result directory}")
mkdir "$out"
rounds=(1 2 3 4)
if [[ ${2:-} == --handoff ]]; then rounds+=(5 6); fi
for round in "${rounds[@]}"; do
  modes=(native rmw)
  if ((round % 2 == 0)); then modes=(rmw native); fi
  if [[ ${2:-} == --handoff ]]; then
    case $round in
      1) modes=(native rmw traced);;
      2) modes=(rmw traced native);;
      3) modes=(traced native rmw);;
      4) modes=(traced rmw native);;
      5) modes=(rmw native traced);;
      6) modes=(native traced rmw);;
    esac
  fi
  for mode in "${modes[@]}"; do
    export RMW_IMPLEMENTATION=rmw_agnocast
    if [[ $mode == native ]]; then export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp; fi
    python3 experiments/rmw-agnocast/path-case.py "$probe" "$hook" "$mode" "$out/$mode-$round"
  done
done
