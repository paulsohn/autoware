#!/usr/bin/env bash
# Run inside the Lyrical runtime guard after the tracker and cluster builds.
set -e
source build/agnocast-lyrical/install/local_setup.bash
source build/perception-replay/install/local_setup.bash
source build/tracker-replay/install/local_setup.bash
export TRACKER_PREFIX=$(ros2 pkg prefix autoware_multi_object_tracker)
export ROS_LOG_DIR=/tmp/tracker-replay-roslog
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
cluster_dir=src/universe/autoware_universe/perception/autoware_euclidean_cluster
build/perception-replay/install/universe_detection_replay/lib/universe_detection_replay/replay_cluster \
  --ros-args --params-file "$cluster_dir/config/euclidean_cluster.param.yaml" &
cluster=$!
/opt/ros/lyrical/lib/tf2_ros/static_transform_publisher --frame-id map --child-frame-id base_link &
tf=$!
trap 'kill -INT "$cluster" "$tf" 2>/dev/null || true; wait "$cluster"; wait "$tf"' EXIT
r=experiments/rmw-agnocast
if [[ ${1:-} == coverage ]]; then
  shift
  python3 "$r/route-perception.py" "$@"
  exit
fi
if (($# == 0)); then set -- "$r"/pipeline-{isolation,busy}-results/*.log; fi
python3 "$r/replay-tracks.py" "$@"
