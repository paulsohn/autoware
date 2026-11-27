#!/usr/bin/env bash
# Inside the guarded Lyrical userspace. One directory per rate and policy.
set -e
source /opt/ros/lyrical/setup.bash
w=$(realpath "${1:?rehearsal workspace}")
source "$w/install/local_setup.bash"
export RMW_IMPLEMENTATION=rmw_agnocast ROS_DOMAIN_ID=185 ROS_DISABLE_LOANED_MESSAGES=1
export CC=clang-21 CXX=clang++-21 XML_CATALOG_FILES="$w/schema/catalog.xml"
cmake -S experiments/rmw-agnocast/standalone -B "$w/contract-probes" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$w/contract-probes" --target cloud_reader_pressure --parallel 1
mkdir "${2:?fresh output directory}"
cd "$2"
for hz in 50 10; do
  for policy in last all; do
    mkdir "$hz-$policy"
    (
      cd "$hz-$policy"
      children=()
      trap 'for pid in "${children[@]}"; do kill "$pid" 2>/dev/null || true; done' EXIT
      for role in fast slow writer; do
        LD_PRELOAD="$w/libagnocast_heaphook.so" "$w/contract-probes/cloud_reader_pressure" "$role" "$hz" "$policy" >"$role.csv" 2>"$role.log" &
        children+=("$!")
        echo "$!" > "$role.pid"
      done
      for pid in "${children[@]}"; do wait "$pid"; done
      # The released Agnocast cleanup daemon unlinks pools asynchronously.
      for attempt in {0..300}; do
        remaining=0
        for pid in "${children[@]}"; do
          if [[ -e "/dev/shm/agnocast@$pid" ]]; then remaining=$((remaining+1)); fi
        done
        if ((remaining == 0)); then break; fi
        sleep .1
      done
      echo "$attempt,$remaining" > cleanup.csv
      test "$remaining" = 0
      trap - EXIT
    )
  done
done
