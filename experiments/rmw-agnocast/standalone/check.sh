#!/usr/bin/env bash
# Run inside a sourced, isolated Lyrical shell with an unusable/absent Cyclone
# RMW, the selected standalone install and the version-matched runtime hook.
set -eu
probe=${1:?path to topic_check}
test "$RMW_IMPLEMENTATION" = rmw_agnocast
test ! -e /opt/ros/lyrical/lib/librmw_cyclonedds_cpp.so
unset AGNOCAST_RMW_PUBLISH_TOPICS AGNOCAST_RMW_SUBSCRIBE_TOPICS AGNOCAST_RMW_TOPIC
unset AGNOCAST_BRIDGE_MODE AGNOCAST_NO_DISCOVERY_AGENT
library="$(ros2 pkg prefix rmw_agnocast)/lib/librmw_agnocast.so"
deps=$(ldd "$library")
if grep -Ei 'librmw_(cyclonedds|fastrtps|zenoh)|libddsc\.so|not found' <<< "$deps"; then exit 1; fi
for mode in ordinary loan; do
  topic="/standalone_${BASHPID}_${RANDOM}/${mode}"
  if [[ $mode == loan ]]; then
    export CHECK_NATIVE_LOAN=1 ROS_DISABLE_LOANED_MESSAGES=0
  else
    unset CHECK_NATIVE_LOAN
    export ROS_DISABLE_LOANED_MESSAGES=1
  fi
  timeout -k 2 12 "$probe" reader "$topic" &
  reader=$!
  if ! timeout -k 2 12 "$probe" writer "$topic"; then
    wait "$reader" || true
    exit 1
  fi
  wait "$reader"
done
