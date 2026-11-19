#!/usr/bin/env bash
# Each recorded cell requires the requested backend at both consumers.
set -e
source build/two/install/local_setup.bash
p=src/crop_pipeline_benchmark
out=$(mktemp -d /ws/build/cyclone-followup-XXXXXX)
report=$p/two-edge-results/cyclone-matrix.tsv
printf 'transport\tbuffer\texit\tverified_frames\tdiagnostic\n' > "$report"
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
record() {
  local transport=$1 backend=$2 rc=$3 path=$4
  local frames message
  frames=$(wc -l < "$path/$backend.csv")
  if ((rc == 0)); then
    [[ $frames == 60 ]]
    message=passed
  else
    message=$(grep -m1 -E 'Operation requires CPU backend|malformed packet|output timeout|discovery timeout|unexpected output backend' "$path/$backend.log")
    [[ -n $message ]]
  fi
  printf '%s\t%s\t%s\t%s\t%s\n' "$transport" "$backend" "$rc" "$frames" "$message" >> "$report"
}
for backend in cpu shmem; do
  rc=0
  python3 "$p/run_case.py" rmw_cyclonedds_cpp "$backend" "$out/$backend.csv" --two-edge >"$out/$backend.log" 2>&1 || rc=$?
  record loopback "$backend" "$rc" "$out"
  if [[ $backend == cpu ]]; then [[ $rc == 0 ]]; fi
done
rc=0
bash "$p/run-cyclone-iox.sh" >"$out/iox.txt" 2>&1 || rc=$?
iox=$(head -n1 "$out/iox.txt")
grep -q 'RouDi is ready' "$iox/roudi.log"
for backend in cpu shmem; do
  code=$(awk -v b="$backend" '$1==b && $2=="exit" {print $3}' "$out/iox.txt")
  [[ $code =~ ^[0-9]+$ ]]
  record iceoryx "$backend" "$code" "$iox"
done
cat "$report"
