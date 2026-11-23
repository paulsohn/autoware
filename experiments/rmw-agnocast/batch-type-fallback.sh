#!/usr/bin/env bash
# From the provisioned colcon pilot worktree.
set -eu
out=${1:?fresh directory}
mkdir "$out"
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
cases=(no-writer no-backend ambiguous)
for direction in a2r r2a; do
 for kind in poses image; do cases+=("$direction-$kind-auto-1"); done
 cases+=("$direction-sensorimage-incompatible-1" "$direction-sensorimage-auto-1")
 for spec in typed-1 auto-1 auto-2 typed-2; do cases+=("$direction-path-$spec"); done
 for kind in cloud largecloud; do
  for spec in auto-1 forced-1 forced-2 auto-2; do cases+=("$direction-$kind-$spec"); done
 done
done
rc=0
for name in "${cases[@]}"; do
 status=0
 BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout -k 5 40 bash -ec '
 source build/agnocast-lyrical/install/local_setup.bash
 source build/rmw-agnocast/install/local_setup.bash
 source build/rmw-agnocast/load-install/local_setup.bash
 export CYCLONEDDS_URI="<CycloneDDS><Domain><General><Interfaces><NetworkInterface name=\"lo\"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address=\"127.0.0.1\"/></Peers></Discovery></Domain></CycloneDDS>"
 python3 experiments/rmw-agnocast/type-fallback.py "$@"' bash "$name" "$out/$name" >"$out/$name.txt" 2>&1 || status=$?
 echo "$name $status" | tee -a "$out/status.txt"
 if [[ $status == 75 ]]; then exit 75; fi
 if [[ $status != 0 ]]; then rc=1; fi
done
exit "$rc"
