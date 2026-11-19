#!/usr/bin/env bash
set -e
source build/two/install/local_setup.bash
p=src/crop_pipeline_benchmark
out=$p/two-edge-results
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
cc -shared -fPIC -I/usr/local/cuda/include "$p/trace_copy.c" src/buffer_pointcloud_probe/trace_vmm.c -ldl -o /tmp/native-trace.so
for name in zenoh-cuda zenoh-shmem cyclone-cpu zenoh-cpu; do
  rmw=rmw_zenoh_cpp
  [[ $name != cyclone-cpu ]] || rmw=rmw_cyclonedds_cpp
  prefix=
  [[ $name != zenoh-cuda ]] || prefix=/tmp/native-trace.so:/usr/local/cuda/lib64/libcudart.so.13:/usr/lib/x86_64-linux-gnu/libcuda.so.1
  LD_PRELOAD="$prefix" python3 "$p/run_case.py" "$rmw" "${name#*-}" "/tmp/$name.csv" --two-edge --profile "$out/$name-resources.csv" 2>"/ws/build/$name-profile.log" || { cat "/ws/build/$name-profile.log"; exit 1; }
  if [[ $name == zenoh-cuda ]]; then
    grep -E '^(copy|VMM) ' "/ws/build/$name-profile.log" | sort | uniq -c > "$out/cuda-trace.txt"
  fi
done
