#!/usr/bin/env bash
# Run from the repository root on the Lyrical laptop fixture.
set -euo pipefail
out=${1:?new directory under build}
examples=${2:?path to ros2/examples checkout}
schema=${3:?directory with package schemas}
rmw_repo=${4:?path to ros2/rmw_implementation checkout at 3.1.6}
here=experiments/rmw-agnocast/rehearsal
test -c /dev/agnocast
test "$(cat /sys/module/agnocast/version)" = 2.4.1
test -f "${REHEARSAL_HOOK:?released heap hook library}"
test -d "${LYRICAL_ROOT:?exported Lyrical filesystem}/opt/ros/lyrical"
docker --context default image inspect "${REHEARSAL_IMAGE:-local/autoware-migration:awf-buffer-lyrical-zenoh}" >/dev/null
bash "$here/stage.sh" "$out" "$examples" "$schema"
install -m 0644 "$REHEARSAL_HOOK" "$out/libagnocast_heaphook.so"
env_cmd=(experiments/buffer/run-lyrical.sh env -i HOME=/tmp PATH=/usr/bin:/bin)
"${env_cmd[@]}" bash "$here/build.sh" "$out" 2>&1 | tee "$out/build.log"
experiments/buffer/run-limited.sh python3 "$here/runtime.py" "$out" "$out/runtime" 2>&1 | tee "$out/runtime.log"
"${env_cmd[@]}" REHEARSAL_TEST_DEBS="${REHEARSAL_TEST_DEBS:-}" REHEARSAL_HOOK="/ws/$out/libagnocast_heaphook.so" bash "$here/conformance.sh" "$out" "$rmw_repo" 2>&1 | tee "$out/conformance.log"
python3 "$here/compare.py" "$here/baseline.csv" "$out/conformance-results/cases.csv"
