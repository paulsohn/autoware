#!/usr/bin/env bash
# Inside the guarded Lyrical runtime.
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/standalone/o3-install/local_setup.bash
source build/standalone/release-path/install/local_setup.bash
source build/standalone/poll-install/local_setup.bash
source build/manual-gateway/install/local_setup.bash
export AGNOCAST_GATEWAY_EXECUTABLE="$(ros2 pkg prefix agnocast_cyclone_gateway)/lib/agnocast_cyclone_gateway/topic_gateway"
if [[ -n ${TEST_GATEWAY_EXECUTABLE:-} ]]; then export AGNOCAST_GATEWAY_EXECUTABLE="$TEST_GATEWAY_EXECUTABLE"; fi
export TEST_HEAPHOOK=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so
export ROS_DOMAIN_ID=175 RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
if [[ ${2:-} == --handoff ]]; then
  out=$1
  shift 2
  exec python3 experiments/rmw-agnocast/manual-gateway/handoff.py "$out" "$@"
fi
exec python3 experiments/rmw-agnocast/manual-gateway/ownership.py "$1"
