#!/usr/bin/env bash
# Device-free, network-only control. The Zenoh router and stock nodes use container loopback.
set -eu
distro=$(cat build/agnocast-lyrical/universe-root)
if [[ ${1:-loopback} == routed ]]; then
  peer=$(docker run --rm -d --pull=never --network awpilot0 --ipc private --memory=512m --memory-swap=512m \
    -v "$distro/opt/ros/lyrical:/opt/ros/lyrical:ro" --entrypoint bash local/autoware-migration:awf-buffer-lyrical-zenoh \
    -ec 'source /opt/ros/lyrical/setup.bash; exec /opt/ros/lyrical/lib/rmw_zenoh_cpp/rmw_zenohd')
  trap 'docker stop -t 1 "$peer" >/dev/null' EXIT
  ip=$(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "$peer")
  export LYRICAL_ROOT="$distro" ZENOH_CONFIG_OVERRIDE="connect/endpoints=[\"tcp/$ip:7447\"];listen/endpoints=[\"tcp/0.0.0.0:0\"]"
  experiments/buffer/run-lyrical.sh env -u LD_PRELOAD ROS_DOMAIN_ID=173 RMW_IMPLEMENTATION=rmw_zenoh_cpp \
    python3 experiments/rmw-agnocast/islands/zenoh-retirement.py
  exit
fi
docker run --rm --pull=never --network none --ipc private --memory=512m --memory-swap=512m \
  -v "$distro/opt/ros/lyrical:/opt/ros/lyrical:ro" \
  -v "$PWD/experiments/rmw-agnocast/islands/zenoh-retirement.py:/probe.py:ro" \
  --entrypoint bash local/autoware-migration:awf-buffer-lyrical-zenoh -ec '
    source /opt/ros/lyrical/setup.bash
    test ! -e /dev/agnocast
    export ROS_DOMAIN_ID=173 RMW_IMPLEMENTATION=rmw_zenoh_cpp
    /opt/ros/lyrical/lib/rmw_zenoh_cpp/rmw_zenohd >/tmp/router.log 2>&1 &
    router=$!
    trap "kill $router" EXIT
    sleep 0.5
    python3 /probe.py'
