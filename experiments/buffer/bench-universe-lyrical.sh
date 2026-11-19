#!/usr/bin/env bash
set -e
w=build/agnocast-lyrical
source "$w/install/local_setup.bash"
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
p=src/universe_crop_benchmark
out=${1:?supply a new results directory}
shift
cie=()
if [[ ${1:-} == --compare-cie ]]; then cie=(--ros-cie); shift; fi
compare_rmw=0
if [[ ${1:-} == --compare-rmw ]]; then
  compare_rmw=1
  source build/rmw-agnocast/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  shift
fi
if (($#)); then echo "unexpected argument: $1" >&2; exit 2; fi
mkdir "$out"
for round in 1 2 3; do
  case "$round" in
    1) modes=(cyclone zenoh-shmem agnocast zenoh-cuda zenoh) ;;
    2) modes=(agnocast zenoh zenoh-cuda cyclone zenoh-shmem) ;;
    3) modes=(zenoh-cuda zenoh-shmem zenoh agnocast cyclone) ;;
  esac
  if ((compare_rmw)); then
    pos=$((2 * round - 2))
    modes=("${modes[@]:0:pos}" rmw-agnocast "${modes[@]:pos}")
  fi
  i=0
  for mode in "${modes[@]}"; do
    backend=cpu
    if [[ $mode == zenoh-* ]]; then backend=${mode#zenoh-}; fi
    variants=(default)
    if ((${#cie[@]})) && [[ $mode != agnocast ]]; then
      if (((round + i) % 2)); then variants=(cie default); else variants=(default cie); fi
    fi
    for variant in "${variants[@]}"; do
      options=()
      label=$mode
      if [[ $variant == cie ]]; then options=("${cie[@]}"); label=$mode-cie; fi
      transport=${mode%%-*}
      if [[ $mode == rmw-agnocast ]]; then transport=$mode; fi
      python3 "$p/run_case.py" "$transport" --buffer "$backend" "${options[@]}" "$out/$label-$round.csv" --resources "$out/$label-$round-resources.csv"
    done
    i=$((i + 1))
  done
done
labels=(cyclone agnocast zenoh zenoh-shmem zenoh-cuda)
if ((compare_rmw)); then labels+=(rmw-agnocast); fi
if ((${#cie[@]})); then
  labels+=(cyclone-cie zenoh-cie zenoh-shmem-cie zenoh-cuda-cie)
  if ((compare_rmw)); then labels+=(rmw-agnocast-cie); fi
fi
python3 src/crop_pipeline_benchmark/summarize.py "$out" "${labels[@]}"
