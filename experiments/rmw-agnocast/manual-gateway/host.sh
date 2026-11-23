#!/usr/bin/env bash
# Commands inside a guarded Lyrical runtime, from the provisioned colcon worktree.
set -e
op=$1
shift
# ros2 run's prefix supplies the exact supervisor PID for fault injection.
if [[ $op == capture-pid ]]; then
  echo "$$" > "$1"
  shift
  exec "$@"
fi
export ROS_DOMAIN_ID=173
if [[ $op == sustained ]]; then
  export UNIVERSE_RELEASE_CONTROLS=1
  exec bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws bash experiments/rmw-agnocast/run-sustained.sh standalone "$1"
fi
if [[ $op == graph ]]; then
  export SYNC_ROUTE_PILOT=1 SYNC_RECORDER_LIVE=1 SYNC_SHORT_WAIT=1 SYNC_FIXTURE_DIRECTORY="$1"
  export SYNC_STANDALONE_OWNERSHIP=crop-view SYNC_TYPED_CROP_CONTROL=1
  export SYNC_CROP_COMPONENT_PREFIX=/ws/build/agnocast-lyrical/install/universe_crop_benchmark
  export SYNC_CIE_EXECUTABLE=/ws/build/universe-pipeline/obj/universe_sync_benchmark/sync_standalone_cie
  TIMEFORMAT="graph_cpu %U %S"
  time bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws bash experiments/rmw-agnocast/standalone/run-universe.sh
  exit
fi
source build/agnocast-lyrical/install/local_setup.bash
source build/standalone/o3-install/local_setup.bash
source build/standalone/release-path/install/local_setup.bash
source build/standalone/poll-install/local_setup.bash
source build/manual-gateway/install/local_setup.bash
hook=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so
if [[ $op == gateway ]]; then
  pidfile=$1
  shift
  export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp CYCLONEDDS_URI="${GATEWAY_CYCLONE_PROFILE:-file:///ws/build/lyrical-peer-ready/host.xml}"
  exec ros2 run --prefix "bash $0 capture-pid $pidfile" agnocast_cyclone_gateway topic_gateway "$@" --heaphook "$hook"
fi
if [[ $op == ingress-reader ]]; then
  export RMW_IMPLEMENTATION=rmw_agnocast LD_PRELOAD="$hook"
  exec python3 experiments/rmw-agnocast/manual-gateway/poses-reader.py
fi
if [[ $op == verify ]]; then
  exec python3 experiments/rmw-agnocast/manual-gateway/verify.py "$@"
fi
exit 2
