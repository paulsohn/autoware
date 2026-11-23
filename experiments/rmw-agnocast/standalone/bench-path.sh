#!/usr/bin/env bash
# Run from the experiment worktree on the provisioned Lyrical laptop.
set -eu
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)" BUFFER_RUNTIME_ONLY=1
out=${1:?fresh directory relative to worktree}
mkdir "$out"
rounds=4
case ${2:-} in
  --compare-builds|--compare-release|--compare-events) rounds=6;;
  ""|--compare-handoff) ;;
  *) echo "Unknown comparison: $2" >&2; exit 2;;
esac
for ((round=1;round<=rounds;round++)); do
  if [[ ${2:-} == --compare-handoff ]]; then
    case $round in
      1) modes=(native o2 inline handoff);;
      2) modes=(o2 handoff native inline);;
      3) modes=(handoff inline o2 native);;
      4) modes=(inline native handoff o2);;
    esac
  elif [[ ${2:-} == --compare-release || ${2:-} == --compare-events ]]; then
    case $round in
      1) modes=(native o2 o3);;
      2) modes=(o2 o3 native);;
      3) modes=(o3 native o2);;
      4) modes=(o3 o2 native);;
      5) modes=(native o3 o2);;
      6) modes=(o2 native o3);;
    esac
    if [[ ${2:-} == --compare-events ]]; then modes=("${modes[@]/o3/events}"); fi
  elif [[ $rounds == 4 ]]; then
    modes=(rmw native)
    if ((round % 2 == 0)); then modes=(native rmw); fi
  else
    modes=(qualified native o2)
    if ((round % 2 == 0)); then modes=(o2 native qualified); fi
    if [[ $round == 5 ]]; then modes=(qualified-trace o2-trace); fi
    if [[ $round == 6 ]]; then modes=(o2-trace qualified-trace); fi
  fi
  for mode in "${modes[@]}"; do
    isolate=()
    if [[ $mode != native ]]; then
      isolate=(bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws)
    fi
    experiments/buffer/run-lyrical.sh "${isolate[@]}" bash -ec '
      source build/migration-guide/install/local_setup.bash
      if [[ $1 != native ]]; then
        build_kind=${1%-trace}
        if [[ $1 == inline || $1 == handoff || $1 == events ]]; then build_kind=o2; fi
        if [[ $build_kind == rmw ]]; then build_kind=qualified; fi
        source "build/standalone/$build_kind-install/local_setup.bash"
        test ! -e /opt/ros/lyrical/lib/librmw_cyclonedds_cpp.so
        test ! -e /opt/ros/lyrical/lib/libddsc.so
        export RMW_IMPLEMENTATION=rmw_agnocast
        test "$(ros2 pkg prefix rmw_agnocast)" = "/ws/build/standalone/$build_kind-install/rmw_agnocast"
      else
        source build/agnocast-lyrical/install/local_setup.bash
        source build/rmw-agnocast/install/local_setup.bash
        export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
        export AGNOCAST_BRIDGE_MODE=OFF AGNOCAST_NO_DISCOVERY_AGENT=1
        export CYCLONEDDS_URI="<CycloneDDS><Domain><General><Interfaces><NetworkInterface name=\"lo\"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address=\"127.0.0.1\"/></Peers></Discovery></Domain></CycloneDDS>"
      fi
      export ROS_DOMAIN_ID=173 ROS_DISABLE_LOANED_MESSAGES=0
      probe=/ws/build/rmw-agnocast/load-install/agnocast_rmw_loan_probe/lib/agnocast_rmw_loan_probe/path_benchmark
      hook=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so
      if [[ $4 == --compare-handoff || $4 == --compare-events ]]; then
        probe=/ws/build/rmw-agnocast/load-obj/agnocast_rmw_loan_probe/path_benchmark
      fi
      arm=rmw
      if [[ $1 == inline || $1 == handoff || $1 == events ]]; then arm=$1; fi
      if [[ $1 == native ]]; then arm=native; fi
      if [[ $1 == *-trace ]]; then
        export PATH_TIMING_FILE="/ws/$2/$1-$3.stages"
        hook="$hook:/ws/build/standalone/path-timing.so"
      fi
      python3 experiments/rmw-agnocast/path-case.py "$probe" "$hook" "$arm" "$2/$1-$3"
    ' bash "$mode" "$out" "$round" "${2:-}" >"$out/$mode-$round.log" 2>&1 || { tail -15 "$out/$mode-$round.log"; exit 1; }
  done
done
