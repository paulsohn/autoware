#!/usr/bin/env bash
# Called on the host at the colcon worktree root.
set -eu
out=${1:?fresh directory}
mkdir "$out"
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)" BUFFER_RUNTIME_ONLY=1 UNIVERSE_RELEASE_CONTROLS=1
guard=(experiments/buffer/run-lyrical.sh bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws)
for op in --ownership --check --check-nested; do
  "${guard[@]}" bash -ec 'source build/perception-replay/install/local_setup.bash; exec bash experiments/rmw-agnocast/standalone/run-universe.sh "$1"' bash "$op" >"$out/$op.log" 2>&1
done
"${guard[@]}" bash experiments/rmw-agnocast/standalone/bench-functional-final.sh >"$out/functional.log" 2>&1
for round in 1 2; do
  "${guard[@]}" timeout -k 12 150 bash experiments/rmw-agnocast/run-sustained.sh standalone "$out/standalone-$round" >"$out/standalone-$round.log" 2>&1
done
