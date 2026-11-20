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
variant=
baseline=rmw
case ${2:-} in
  --helper-free) baseline=direct; variant=helper-free;;
  --handoff) variant=traced;;
  --direct) variant=direct;;
  --native-stages) ;;
  --fixed) export AGNOCAST_PROBE_IDLE_DDS=1;;
  --dispatch) rounds+=(5);;
  --rendezvous) baseline=direct; variant=rendezvous;;
  "") ;;
  *) exit 2;;
esac
if [[ -n $variant ]]; then rounds+=(5 6); fi
for round in "${rounds[@]}"; do
  modes=(native rmw)
  if ((round % 2 == 0)); then modes=(rmw native); fi
  if [[ -n $variant ]]; then
    case $round in
      1) modes=(native "$baseline" "$variant");;
      2) modes=("$baseline" "$variant" native);;
      3) modes=("$variant" native "$baseline");;
      4) modes=("$variant" "$baseline" native);;
      5) modes=("$baseline" native "$variant");;
      6) modes=(native "$variant" "$baseline");;
    esac
  fi
  if [[ ${2:-} == --native-stages ]]; then
    case $round in
      1) modes=(native native-traced direct-stages direct);;
      2) modes=(native-traced direct native direct-stages);;
      3) modes=(direct direct-stages native-traced native);;
      4) modes=(direct-stages native direct native-traced);;
    esac
  fi
  if [[ ${2:-} == --dispatch ]]; then
    treatments=(native direct single rendezvous single-traced)
    offset=$((round - 1))
    modes=("${treatments[@]:offset}" "${treatments[@]:0:offset}")
  fi
  if [[ ${2:-} == --fixed ]]; then
    case $round in
      1) modes=(native direct fixed helper-free);;
      2) modes=(direct helper-free native fixed);;
      3) modes=(helper-free fixed direct native);;
      4) modes=(fixed native helper-free direct);;
    esac
  fi
  for mode in "${modes[@]}"; do
    export RMW_IMPLEMENTATION=rmw_agnocast
    if [[ $mode == native || $mode == native-traced ]]; then export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp; fi
    python3 experiments/rmw-agnocast/path-case.py "$probe" "$hook" "$mode" "$out/$mode-$round"
  done
done
