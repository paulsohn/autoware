#!/usr/bin/env bash
# Run with Docker Engine access on the provisioned colcon laptop.
set -eu
out=${1:?supply a fresh result prefix}
backend=${2:?cyclone or zenoh}
policy=${3:?native, configured or discovered}
root=$(cat build/agnocast-lyrical/universe-root)
peer=$(docker run --rm -d --pull=never --network awpilot0 --ipc=private --memory=512m --memory-swap=512m \
  -v "$root/opt/ros/lyrical:/opt/ros/lyrical:ro" --entrypoint sleep local/autoware-migration:awf-buffer-lyrical-zenoh 160)
trap 'docker stop -t 1 "$peer" >/dev/null' EXIT
ip=$(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "$peer")
export GATEWAY_BENCH_PEER="$peer"
profile() {
  python3 -c 'import sys, xml.etree.ElementTree as ET
tree=ET.parse(sys.argv[1])
tree.findall(".//Peers/Peer")[int(sys.argv[2])].set("Address",sys.argv[3])
print(ET.tostring(tree.getroot(),encoding="unicode"))' "build/lyrical-peer-ready/$1.xml" "$2" "$ip"
}
GATEWAY_CYCLONE_PROFILE=$(profile host 1)
GATEWAY_PEER_PROFILE=$(profile peer 0)
export GATEWAY_CYCLONE_PROFILE GATEWAY_PEER_PROFILE
export ZENOH_CONFIG_OVERRIDE="connect/endpoints=[\"tcp/$ip:7447\"];listen/endpoints=[\"tcp/0.0.0.0:0\"]"
case "$backend" in
  cyclone) export GATEWAY_BENCH_RMW=rmw_cyclonedds_cpp ;;
  zenoh) export GATEWAY_BENCH_RMW=rmw_zenoh_cpp
    docker exec -d "$peer" bash -ec 'source /opt/ros/lyrical/setup.bash; exec /opt/ros/lyrical/lib/rmw_zenoh_cpp/rmw_zenohd'
    ;;
  *) exit 2 ;;
esac
python3 experiments/rmw-agnocast/manual-gateway/check.py "$out-$backend-$policy" --churn "$policy" sustained-control
