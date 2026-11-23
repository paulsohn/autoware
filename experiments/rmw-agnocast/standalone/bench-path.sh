#!/usr/bin/env bash
# Run from the experiment worktree on the provisioned Lyrical laptop.
set -eu
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)" BUFFER_RUNTIME_ONLY=1
out=${1:?fresh directory relative to worktree}
mkdir "$out"
rounds=4
case ${2:-} in
  --compare-builds|--compare-release|--compare-events|--compare-poll) rounds=6;;
  ""|--compare-handoff|--compare-cie|--replay-cie|--trace-cie) ;;
  *) echo "Unknown comparison: $2" >&2; exit 2;;
esac
release_baseline=0
case ${2:-} in
  ""|--compare-handoff|--compare-events|--compare-cie|--compare-poll|--replay-cie|--trace-cie)
    release_baseline=1
    for flags in \
      build/standalone/o3-obj/agnocastlib/CMakeFiles/agnocast.dir/flags.make \
      build/standalone/o3-obj/rmw_agnocast/CMakeFiles/rmw_agnocast.dir/flags.make \
      build/standalone/release-path/obj/rclcpp/CMakeFiles/rclcpp.dir/flags.make \
      build/standalone/release-path/probe/CMakeFiles/path_benchmark.dir/flags.make; do
      grep -Eq '^CXX_FLAGS = .* -O3( |$)' "$flags"
    done;;
esac
if [[ ${2:-} == --replay-cie || ${2:-} == --trace-cie ]]; then
  for flags in \
    build/standalone/poll-obj/rmw_agnocast/CMakeFiles/rmw_agnocast.dir/flags.make \
    build/standalone/release-cie/obj/callback_isolated_executor/CMakeFiles/callback_isolated_executor.dir/flags.make \
    build/standalone/release-cie/probe/CMakeFiles/path_benchmark.dir/flags.make; do
    awk '/^CXX_FLAGS =/ { for (i=3; i<=NF; ++i) if ($i ~ /^-O/) opt=$i } END { exit opt != "-O3" }' "$flags"
  done
fi
for ((round=1;round<=rounds;round++)); do
  if [[ ${2:-} == --trace-cie ]]; then
    modes=(native standalone-cie poll-cie)
    if ((round % 2 == 0)); then modes=(poll-cie standalone-cie native); fi
    if ((round == 2 || round == 3)); then
      modes=("${modes[@]/%/-instrumented}")
    fi
  elif [[ ${2:-} == --replay-cie ]]; then
    modes=(native standalone-cie)
    if ((round % 2 == 0)); then modes=(standalone-cie native); fi
  elif [[ ${2:-} == --compare-cie ]]; then
    case $round in
      1) modes=(native o3 standalone-cie bundled-cie);;
      2) modes=(o3 bundled-cie native standalone-cie);;
      3) modes=(bundled-cie standalone-cie o3 native);;
      4) modes=(standalone-cie native bundled-cie o3);;
    esac
  elif [[ ${2:-} == --compare-handoff ]]; then
    case $round in
      1) modes=(native o2 inline handoff);;
      2) modes=(o2 handoff native inline);;
      3) modes=(handoff inline o2 native);;
      4) modes=(inline native handoff o2);;
    esac
  elif [[ ${2:-} == --compare-release || ${2:-} == --compare-events || ${2:-} == --compare-poll ]]; then
    case $round in
      1) modes=(native o2 o3);;
      2) modes=(o2 o3 native);;
      3) modes=(o3 native o2);;
      4) modes=(o3 o2 native);;
      5) modes=(native o3 o2);;
      6) modes=(o2 native o3);;
    esac
    if [[ ${2:-} == --compare-poll ]]; then modes=("${modes[@]/o3/poll}"); fi
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
  if ((release_baseline)); then
    modes=("${modes[@]/o2/o3}")
  fi
  for mode in "${modes[@]}"; do
    tag=$mode
    unset PATH_TRACE_PREFIX
    if [[ $mode == *-instrumented ]]; then
      export PATH_TRACE_PREFIX="/ws/$out/$tag-$round"
      mode=${mode%-instrumented}
    fi
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
        if [[ $5 == 1 ]]; then build_kind=o3; fi
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
      if [[ $5 == 1 ]]; then
        source build/standalone/o3-install/local_setup.bash
        source build/standalone/release-path/install/local_setup.bash
        probe=/ws/build/standalone/release-path/probe/path_benchmark
        if [[ $4 == --compare-cie || $4 == --replay-cie || $4 == --trace-cie ]]; then
          source build/standalone/release-cie/install/local_setup.bash
          probe=/ws/build/standalone/release-cie/probe/path_benchmark
        fi
        libs=$(ldd "$probe")
        grep -F "libagnocast.so => /ws/build/standalone/o3-install/agnocastlib/lib/libagnocast.so" <<< "$libs"
        grep -F "librclcpp.so => /ws/build/standalone/release-path/install/rclcpp/lib/librclcpp.so" <<< "$libs"
      fi
      unset AGNOCAST_RMW_POLL_TOPIC
      if [[ ( $4 == --compare-poll || $4 == --replay-cie || $4 == --trace-cie ) && $1 != native ]]; then
        source build/standalone/poll-install/local_setup.bash
        test "$(ros2 pkg prefix rmw_agnocast)" = /ws/build/standalone/poll-install/rmw_agnocast
        if [[ $1 == poll || $1 == poll-cie ]]; then export AGNOCAST_RMW_POLL_TOPIC=/path_latency; fi
      fi
      arm=rmw
      if [[ $1 == inline || $1 == handoff || $1 == events ]]; then arm=$1; fi
      if [[ $1 == *-cie ]]; then arm=$1; fi
      if [[ $1 == poll-cie ]]; then arm=standalone-cie; fi
      if [[ $1 == native ]]; then arm=native; fi
      if [[ $1 == *-trace ]]; then
        export PATH_TIMING_FILE="/ws/$2/$1-$3.stages"
        hook="$hook:/ws/build/standalone/path-timing.so"
      fi
      python3 experiments/rmw-agnocast/path-case.py "$probe" "$hook" "$arm" "$2/$6-$3"
    ' bash "$mode" "$out" "$round" "${2:-}" "$release_baseline" "$tag" >"$out/$tag-$round.log" 2>&1 || { tail -15 "$out/$tag-$round.log"; exit 1; }
  done
done
