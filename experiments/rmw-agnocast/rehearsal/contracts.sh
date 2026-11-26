#!/usr/bin/env bash
# Run inside the guarded Lyrical userspace with the built rehearsal mounted under /ws.
set -e
source /opt/ros/lyrical/setup.bash
out=$(realpath "${1:?supply a built rehearsal workspace}")
source "$out/install/local_setup.bash"
export RMW_IMPLEMENTATION=rmw_agnocast CC=clang-21 CXX=clang++-21
export XML_CATALOG_FILES="$out/schema/catalog.xml"
cmake -S experiments/rmw-agnocast/standalone -B "$out/contract-probes" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$out/contract-probes" --target qos_contract_check deadline_check --parallel 1
export ROS_DOMAIN_ID=183
bash experiments/rmw-agnocast/standalone/check-qos-contract.sh \
  "$out/contract-probes/qos_contract_check" "$out/libagnocast_heaphook.so"
LD_PRELOAD="$out/libagnocast_heaphook.so" "$out/contract-probes/deadline_check" /contract_deadline
