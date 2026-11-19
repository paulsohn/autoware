#!/usr/bin/env bash
set -e
w=build/rmw-agnocast
mode=${1:?use stock or patched}
source build/agnocast-lyrical/install/local_setup.bash
if [[ $mode == patched ]]; then source "$w/install/local_setup.bash"; fi
source "$w/msg-install/local_setup.bash"
source "$w/$mode-install/local_setup.bash"
probe=/ws/$w/$mode-install/agnocast_rmw_loan_probe/lib/agnocast_rmw_loan_probe/loan_probe
ldd "$probe" | grep 'librclcpp.so'
export ROS_DISABLE_LOANED_MESSAGES=0 AGNOCAST_BRIDGE_MODE=OFF AGNOCAST_NO_DISCOVERY_AGENT=1
export NATIVE_PROBE_TOPIC=/loan_probe_${RANDOM}_$$
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
hook=/ws/build/agnocast-lyrical/install/agnocastlib/lib/libagnocast_heaphook.so
cd "$(mktemp -d)"
LD_PRELOAD="$hook" "$probe" reader "$mode" &
reader=$!
trap 'kill "$reader" 2>/dev/null || true' EXIT
LD_PRELOAD="$hook" "$probe" writer "$mode"
wait "$reader"
trap - EXIT
