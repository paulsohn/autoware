#!/usr/bin/env bash
set -e
out=${1:?fresh output directory}
mkdir "$out"
export SYNC_LARGE=1 BUFFER_RUNTIME_ONLY=1
batches=(large repeat)
case ${2:-} in --loan-input|--loan-output|--native) batches+=(pair3 pair4 ref1 ref2);; esac
for batch in "${batches[@]}"; do
  modes=(cyclone cdr host fixed zenoh)
  if [[ $batch == repeat ]]; then modes=(zenoh fixed host cdr cyclone); fi
  if [[ ${2:-} == --loan-input ]]; then
    case $batch in
      large) modes+=(byte-copy byte-view);;
      repeat) modes=(byte-view byte-copy "${modes[@]}");;
      pair3) modes=(byte-view byte-copy);;
      pair4) modes=(byte-copy byte-view);;
      ref1) modes=(cyclone zenoh);;
      ref2) modes=(zenoh cyclone);;
    esac
  fi
  if [[ ${2:-} == --loan-output ]]; then
    case $batch in
      large) modes=(byte-view output-private output-shared);;
      repeat) modes=(output-shared output-private byte-view);;
      pair3) modes=(output-shared byte-view);;
      pair4) modes=(byte-view output-shared);;
      ref1) modes=(cyclone zenoh);;
      ref2) modes=(zenoh cyclone);;
    esac
  fi
  if [[ ${2:-} == --native ]]; then
    case $batch in
      large|pair4) modes=(native byte-view);;
      repeat|pair3) modes=(byte-view native);;
      ref1) modes=(cyclone zenoh);;
      ref2) modes=(zenoh cyclone);;
    esac
  fi
  for mode in "${modes[@]}"; do
    log="$out/$batch-$mode.log"
    status=0
    settings=()
    if [[ $batch == ref* ]]; then settings=(SYNC_BYTE_INPUT=1); fi
    env "${settings[@]}" experiments/buffer/run-lyrical.sh timeout -k 4 45 bash -c '
      TIMEFORMAT="graph_user_s=%U graph_sys_s=%S"
      time bash experiments/rmw-agnocast/run-sync.sh "$1"
    ' bash "$mode" > "$log" 2>&1 || status=$?
    echo "exit_status=$status" >> "$log"
    if ((status)); then tail -n 12 "$log"; exit "$status"; fi
  done
done
python3 experiments/rmw-agnocast/summarize-sync.py "$out" "${@:2}"
