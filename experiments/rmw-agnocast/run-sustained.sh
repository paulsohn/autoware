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
export LD_LIBRARY_PATH="$(ros2 pkg prefix universe_sync_benchmark)/lib:$LD_LIBRARY_PATH"
export TRACKER_PREFIX=$(ros2 pkg prefix autoware_multi_object_tracker)
export SYNC_SUSTAINED=1 SYNC_AUDIT=1 SYNC_SHORT_WAIT=1 ENABLE_AGNOCAST=0
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
if [[ $1 == zenoh ]]; then export RMW_IMPLEMENTATION=rmw_zenoh_cpp; fi
python3 experiments/rmw-agnocast/sustained.py "$@"
