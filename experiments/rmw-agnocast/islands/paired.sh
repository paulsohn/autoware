#!/usr/bin/env bash
# Run from the colcon checkout with access to the default Docker Engine.
set -eu
prefix=${1:?pass a fresh output directory prefix}
payloads=(0 1)
if [[ ${4:-both} == cloud ]]; then payloads=(1); fi
for cloud in "${payloads[@]}"; do
  for pair in $(seq "${2:-1}" "${3:-5}"); do
    order=(0 1)
    if (( pair % 2 == 0 )); then order=(1 0); fi
    for dynamic in "${order[@]}"; do
      ISLAND_CLOUD=$cloud ISLAND_PROFILE=1 ISLAND_DISCOVERY=$dynamic \
        experiments/buffer/run-limited.sh python3 experiments/rmw-agnocast/islands/control.py \
        "$prefix-$cloud-$pair-$dynamic"
    done
  done
done
