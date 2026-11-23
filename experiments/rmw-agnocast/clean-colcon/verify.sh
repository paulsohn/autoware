#!/usr/bin/env bash
set -e
source build/rmw-agnocast/install/local_setup.bash
source build/reproduction/install/local_setup.bash
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
export ENABLE_AGNOCAST=0
case "$1" in
  provenance)
    for name in rclcpp agnocastlib rmw_agnocast agnocast_rmw_loan_probe autoware_agnocast_wrapper universe_crop_benchmark universe_sync_benchmark; do
       prefix=$(ros2 pkg prefix "$name")
       case "$name:$prefix" in rclcpp:/ws/build/rmw-agnocast/install/rclcpp|*:/ws/build/reproduction/install/"$name") ;; *) exit 1;; esac
       echo "$name $prefix"
    done
    for lib in build/reproduction/install/{agnocastlib/lib/libagnocast_heaphook,rmw_agnocast/lib/librmw_agnocast,universe_sync_benchmark/lib/libuniverse_sync}.so; do
      result=$(ldd "$lib")
      if [[ "$result" == *"not found"* ]]; then echo "$result"; exit 1; fi
      echo "$lib"
      echo "$result" | grep -E 'lib(agnocast|rclcpp|rmw_agnocast|rmw_cyclonedds_cpp)\.so'
    done
    ;;
  local|healthy|ingress|egress)
    python3 experiments/rmw-agnocast/dual-route.py "$1" "$2"
    ;;
  sustained)
    source build/perception-replay/install/local_setup.bash
    export TRACKER_PREFIX=$(ros2 pkg prefix autoware_multi_object_tracker)
    export SYNC_SUSTAINED=1 SYNC_AUDIT=1 SYNC_SHORT_WAIT=1
    python3 build/reproduction/pressure.py "$2-pressure.csv" &
    monitor=$!
    trap 'kill "$monitor" 2>/dev/null || true; wait "$monitor" 2>/dev/null || true' EXIT
    python3 experiments/rmw-agnocast/sustained.py stock "$2"
    kill -0 "$monitor"
    ;;
esac
