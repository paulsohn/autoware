#!/usr/bin/env bash
# Run a command on the host GPU inside an exported Lyrical root on a systemd laptop.
set -eu
root=${LYRICAL_ROOT:?Set LYRICAL_ROOT to the exported Lyrical container filesystem}
cuda=${CUDA_HOME:-/usr/local/cuda-13.0}
ws=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
unit="aw-two-edge-$$"
args=(--ro-bind "$root" / --dev-bind /dev /dev --proc /proc --ro-bind /sys /sys
      --tmpfs /tmp --ro-bind /etc/hosts /etc/hosts --ro-bind "$(realpath "$cuda")" /usr/local/cuda-13.0
      --symlink cuda-13.0 /usr/local/cuda --bind "$ws" /ws --chdir /ws)
for lib in /usr/lib/x86_64-linux-gnu/libcuda.so* /usr/lib/x86_64-linux-gnu/libnvidia-{gpucomp,ptxjitcompiler,nvvm,ml}.so*; do
  if [[ -e "$lib" ]]; then args+=(--ro-bind "$(realpath "$lib")" "$lib"); fi
done

available() { awk '/MemAvailable:/ {print int($2/1024)}' /proc/meminfo; }
minimum=$(available)
if ((minimum < 3072)); then echo "Host has only $minimum MiB available" >&2; exit 1; fi
systemd-run --user --scope --quiet --collect --unit="$unit" -p MemoryMax=1200M -p MemorySwapMax=0 \
  bwrap "${args[@]}" bash -ec '
    export HOME=/tmp CC=/usr/bin/gcc CXX=/usr/bin/g++ MAKEFLAGS=-j1 CMAKE_BUILD_PARALLEL_LEVEL=1
    export PYTHON_CPU_COUNT=1 ENABLE_AGNOCAST=0 WRAPPER_TWO_EDGE=1 RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
    export ROS_DOMAIN_ID=${ROS_DOMAIN_ID:-194}
    source /opt/ros/lyrical/setup.bash
    if [[ -f /ws/install/local_setup.bash ]]; then source /ws/install/local_setup.bash; fi
    export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
    exec "$@"
  ' bash "$@" &
job=$!
while kill -0 "$job" 2>/dev/null; do
  current=$(available)
  if ((current < minimum)); then minimum=$current; fi
  if ((current < 2048)); then systemctl --user stop "$unit.scope"; exit 1; fi
  sleep .25
done
status=0
wait "$job" || status=$?
echo "Minimum host MemAvailable: $minimum MiB"
exit "$status"
