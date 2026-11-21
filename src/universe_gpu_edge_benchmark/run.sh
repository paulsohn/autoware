#!/usr/bin/env bash
# Inside run-lyrical.sh with BUFFER_RUNTIME_ONLY=1.
set -eo pipefail
source build/gpu-edge/install/local_setup.bash
export RMW_IMPLEMENTATION=rmw_zenoh_cpp
mode=${1:?blackboard, composed, split or mixed}
edge=$(ros2 pkg prefix universe_gpu_edge_benchmark)/lib/universe_gpu_edge_benchmark/edge
router=$(ros2 pkg prefix rmw_zenoh_cpp)/lib/rmw_zenoh_cpp/rmw_zenohd
children=()
cleanup() {
  for pid in "${children[@]}"; do kill "$pid" 2>/dev/null || true; done
  wait || true
}
trap cleanup EXIT
"$router" &
children+=("$!")
sleep 1
kill -0 "${children[0]}"
case $mode in
  blackboard|intra|composed) "$edge" "$mode" 1 ;;
  split|mixed)
    "$edge" sub 1 &
    children+=("$!")
    peers=1
    if [[ $mode == mixed ]]; then
      "$edge" cpu 2 &
      children+=("$!")
      peers=2
    fi
    "$edge" pub "$peers"
    for pid in "${children[@]:1}"; do wait "$pid"; done ;;
  *) exit 2 ;;
esac
