#!/usr/bin/env bash
# Run inside run-lyrical.sh with BUFFER_RUNTIME_ONLY=1.
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/rmw-agnocast/install/local_setup.bash
source build/rmw-agnocast/load-install/local_setup.bash
source build/universe-sync/install/local_setup.bash
export ENABLE_AGNOCAST=0 AGNOCAST_BRIDGE_MODE=OFF AGNOCAST_NO_DISCOVERY_AGENT=1
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
scratch=$(mktemp -d)
nodes=()
cleanup() {
  local original_status=$?
  for pid in "${nodes[@]}"; do kill -INT "$pid" 2>/dev/null || true; done
  local rc=$original_status status
  for pid in "${nodes[@]}"; do
    status=0
    wait "$pid" || status=$?
    if [[ $status != 0 && $status != 130 ]]; then rc=$status; fi
  done
  rm -f "$scratch/ready"
  rmdir "$scratch"
  exit "$rc"
}
trap cleanup EXIT
child=()
case ${1:?cyclone, cdr, host, fixed, byte-copy, byte-view, output-private, output-shared or zenoh} in
  cyclone) export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp;;
  zenoh)
    export RMW_IMPLEMENTATION=rmw_zenoh_cpp
    /opt/ros/lyrical/lib/rmw_zenoh_cpp/rmw_zenohd & nodes+=("$!")
    sleep 1;;
  cdr|host|fixed|byte-copy|byte-view|output-private|output-shared)
    export RMW_IMPLEMENTATION=rmw_agnocast
    export AGNOCAST_RMW_TOPIC=/sync/left:/sync/right:/sync/top:/output
    if [[ $1 == cdr ]]; then export AGNOCAST_RMW_BUFFER_CDR=1; else export AGNOCAST_RMW_HOST_CLOUD=1; fi
    child=(env LD_PRELOAD="$(ros2 pkg prefix agnocastlib)/lib/libagnocast_heaphook.so");;
  *) exit 2;;
esac
bin=build/universe-sync/install/universe_sync_benchmark/lib/universe_sync_benchmark
config=src/universe/autoware_universe/sensing/autoware_pointcloud_preprocessor/config/concatenate_and_time_sync_node.param.yaml
"${child[@]}" /opt/ros/lyrical/lib/tf2_ros/static_transform_publisher --x 4 --frame-id base_link --child-frame-id sync_lidar &
nodes+=("$!")
sync=("${child[@]}" "$bin/concatenate_and_time_sync_node")
if [[ $1 == fixed || $1 == byte-* || $1 == output-* ]]; then
  export SYNC_READY_FILE="$scratch/ready"
  runner="$(ros2 pkg prefix agnocast_rmw_loan_probe)/lib/agnocast_rmw_loan_probe/crop_wait_runner"
  input=()
  if [[ $1 == byte-* || $1 == output-* ]]; then input+=(SYNC_BYTE_INPUT=1); fi
  if [[ $1 == byte-view || $1 == output-* ]]; then input+=(SYNC_VIEW_INPUT=1 AGNOCAST_RMW_HOST_VIEW=1); fi
  if [[ $1 == output-* ]]; then input+=(SYNC_LOAN_OUTPUT=1 AGNOCAST_RMW_HOST_LOAN=1); fi
  if [[ $1 == output-shared ]]; then input+=(SYNC_SHARED_OUTPUT=1); fi
  if [[ -n ${SYNC_TRACE_DIR:-} ]]; then input+=(BENCH_HOP_WRITER="$SYNC_TRACE_DIR/writer.csv"); fi
  sync=("${child[@]}" "${input[@]}" AGNOCAST_RMW_DIRECT_WAIT=1 AGNOCAST_FIXED_PROBE=1 AGNOCAST_RMW_READY_FD=0
    BENCH_CSV="$SYNC_READY_FILE" "$runner" libuniverse_sync.so
    autoware::pointcloud_preprocessor::PointCloudConcatenateDataSynchronizerComponent)
fi
"${sync[@]}" --ros-args --params-file "$config" \
  -p 'input_topics:=[/sync/left,/sync/right,/sync/top]' -p is_motion_compensated:=false \
  -p publish_synchronized_pointcloud:=false -p timeout_sec:=0.04 &
nodes+=("$!")
trace=()
if [[ -n ${SYNC_TRACE_DIR:-} ]]; then trace=(env BENCH_HOP_READER="$SYNC_TRACE_DIR/reader.csv"); fi
"${trace[@]}" "${child[@]}" "$bin/sync_fixture"
for pid in "${nodes[@]}"; do kill -0 "$pid"; done
