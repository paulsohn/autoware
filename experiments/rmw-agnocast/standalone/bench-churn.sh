#!/usr/bin/env bash
set -eu
out=${1:?fresh output directory}
mkdir "$out"
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)" BUFFER_RUNTIME_ONLY=1
bash experiments/rmw-agnocast/standalone/check-universe-o3.sh lidar
for round in 1 2; do
  modes=(standalone cyclone zenoh)
  if [[ $round == 2 ]]; then modes=(zenoh cyclone standalone); fi
  for mode in "${modes[@]}"; do
    isolate=()
    if [[ $mode == standalone ]]; then
      isolate=(bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws)
    fi
    experiments/buffer/run-lyrical.sh "${isolate[@]}" timeout -k 8 65 bash -ec '
      source build/agnocast-lyrical/install/local_setup.bash
      source build/rmw-agnocast/load-install/local_setup.bash
      source build/universe-sync/install/local_setup.bash
      source build/universe-pipeline/install/local_setup.bash
      source build/standalone/o3-install/local_setup.bash
      source build/standalone/release-path/install/local_setup.bash
      export ROS_DOMAIN_ID=173 ENABLE_AGNOCAST=0
      export CYCLONEDDS_URI="<CycloneDDS><Domain><General><Interfaces><NetworkInterface name=\"lo\"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address=\"127.0.0.1\"/></Peers></Discovery></Domain></CycloneDDS>"
      case "$1" in
        standalone)
          source build/standalone/poll-install/local_setup.bash
          export RMW_IMPLEMENTATION=rmw_agnocast LD_PRELOAD=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so;;
        cyclone) export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp;;
        zenoh) export RMW_IMPLEMENTATION=rmw_zenoh_cpp;;
      esac
      exec python3 experiments/rmw-agnocast/dynamic-graph.py "$1" "$2"
    ' bash "$mode" "$out/$mode-$round" >"$out/$mode-$round.log" 2>&1 || { tail -20 "$out/$mode-$round.log"; exit 1; }
  done
done
