#!/usr/bin/env bash
# Inside the existing Lyrical memory guard.
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/rmw-agnocast/install/local_setup.bash
source build/rmw-agnocast/load-install/local_setup.bash
source build/universe-sync/install/local_setup.bash
source build/universe-pipeline/install/local_setup.bash
if [[ $1 == native ]]; then source build/universe-pipeline-native/install/local_setup.bash; fi
source build/perception-replay/install/local_setup.bash
source build/tracker-replay/install/local_setup.bash
export SYNC_SUSTAINED=1 SYNC_AUDIT=1 SYNC_SHORT_WAIT=1 ENABLE_AGNOCAST=0
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
if [[ ${UNIVERSE_RELEASE_CONTROLS:-0} == 1 ]]; then
  export LD_LIBRARY_PATH="/ws/build/standalone/o3-install/agnocastlib/lib:/ws/build/standalone/release-path/install/rclcpp/lib:$LD_LIBRARY_PATH"
  if [[ $1 != native ]]; then
    export SYNC_TYPED_CROP_CONTROL=1 SYNC_CROP_COMPONENT_PREFIX=/ws/build/agnocast-lyrical/install/universe_crop_benchmark
  fi
  if [[ $1 == standalone ]]; then
    source build/standalone/release-cie/install/local_setup.bash
    source build/standalone/poll-install/local_setup.bash
    test ! -e /opt/ros/lyrical/lib/libddsc.so
    export RMW_IMPLEMENTATION=rmw_agnocast
    export LD_PRELOAD=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so
    export SYNC_STANDALONE_OWNERSHIP=crop-view
    export SYNC_CIE_EXECUTABLE=/ws/build/universe-pipeline/obj/universe_sync_benchmark/sync_standalone_cie
  fi
fi
if [[ $1 == zenoh ]]; then export RMW_IMPLEMENTATION=rmw_zenoh_cpp; fi
cluster_prefix=$(ros2 pkg prefix universe_detection_replay)
sync_prefix=$(ros2 pkg prefix universe_sync_benchmark)
TRACKER_PREFIX=$(ros2 pkg prefix autoware_multi_object_tracker)
export TRACKER_PREFIX LD_LIBRARY_PATH="$cluster_prefix/lib:$sync_prefix/lib:$LD_LIBRARY_PATH"
python3 experiments/rmw-agnocast/sustained.py "$@"
