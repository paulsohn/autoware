#!/usr/bin/env bash
# Inside the guarded Lyrical root, with the perception overlay built.
set -e
source build/perception-replay/install/local_setup.bash
export ROS_LOG_DIR=/tmp/cluster-roslog
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
pkg=src/universe/autoware_universe/perception/autoware_euclidean_cluster
build/perception-replay/install/universe_detection_replay/lib/universe_detection_replay/replay_cluster \
  --ros-args --params-file "$pkg/config/euclidean_cluster.param.yaml" &
cluster=$!
trap 'kill -INT "$cluster" 2>/dev/null || true; wait "$cluster" || true' EXIT
r=experiments/rmw-agnocast
python3 "$r/replay-clusters.py" "$r/pipeline-isolation-results" "$r/pipeline-busy-results"
kill -INT "$cluster"
wait "$cluster"
trap - EXIT
