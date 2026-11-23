#!/usr/bin/env bash
set -eu
out=${1:?fresh directory relative to worktree}
bash experiments/rmw-agnocast/standalone/check-universe-o3.sh path
mkdir "$out"
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)" BUFFER_RUNTIME_ONLY=1
for round in 1 2 3 4; do
  case $round in
    1) modes=(native-cie standalone-cie cyclone zenoh);;
    2) modes=(standalone-cie zenoh native-cie cyclone);;
    3) modes=(zenoh cyclone standalone-cie native-cie);;
    4) modes=(cyclone native-cie zenoh standalone-cie);;
  esac
  if ((round % 2)); then modes+=(default-cie); else modes=(default-cie "${modes[@]}"); fi
  rates=(10 50)
  if ((round % 2 == 0)); then rates=(50 10); fi
  for hz in "${rates[@]}"; do
    for mode in "${modes[@]}"; do
      isolate=()
      if [[ $mode == standalone-cie || $mode == default-cie ]]; then
        isolate=(bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws)
      fi
      prefix="$out/$mode-${hz}hz-$round"
      experiments/buffer/run-lyrical.sh "${isolate[@]}" timeout -k 4 40 bash -ec '
        source build/migration-guide/install/local_setup.bash
        source build/standalone/o3-install/local_setup.bash
        source build/standalone/release-path/install/local_setup.bash
        source build/standalone/release-cie/install/local_setup.bash
        export PATH_BENCH_HZ="$2" ROS_DOMAIN_ID=173 AGNOCAST_BRIDGE_MODE=OFF AGNOCAST_NO_DISCOVERY_AGENT=1
        hook=
        export ROS_DISABLE_LOANED_MESSAGES=1
        export CYCLONEDDS_URI="<CycloneDDS><Domain><General><Interfaces><NetworkInterface name=\"lo\"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address=\"127.0.0.1\"/></Peers></Discovery></Domain></CycloneDDS>"
        case $1 in
          native-cie|standalone-cie|default-cie)
            hook=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so
            export ROS_DISABLE_LOANED_MESSAGES=0 RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
            if [[ $1 != native-cie ]]; then
              source build/standalone/poll-install/local_setup.bash
              export RMW_IMPLEMENTATION=rmw_agnocast
              unset AGNOCAST_RMW_POLL_TOPIC AGNOCAST_RMW_READY_ROUTE AGNOCAST_RMW_REUSE_WAIT_FD
              if [[ $1 == standalone-cie ]]; then
                export AGNOCAST_RMW_POLL_TOPIC=/path_latency AGNOCAST_RMW_READY_ROUTE=1 AGNOCAST_RMW_REUSE_WAIT_FD=drain
              else
                set -- standalone-cie "${@:2}"
              fi
              test ! -e /opt/ros/lyrical/lib/libddsc.so
            fi;;
          cyclone) export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp;;
          zenoh)
            export RMW_IMPLEMENTATION=rmw_zenoh_cpp
            /opt/ros/lyrical/lib/rmw_zenoh_cpp/rmw_zenohd &
            router=$!
            trap "kill -INT $router; wait $router" EXIT
            sleep .5
            kill -0 "$router";;
        esac
        python3 experiments/rmw-agnocast/path-case.py /ws/build/standalone/release-cie/probe/path_benchmark "$hook" "$1" "$3"
      ' bash "$mode" "$hz" "$prefix" >"$prefix.log" 2>&1 || { tail -20 "$prefix.log"; exit 1; }
    done
  done
done
