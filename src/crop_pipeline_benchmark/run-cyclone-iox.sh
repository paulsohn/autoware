#!/usr/bin/env bash
set -e
ulimit -c 0
source build/two/install/local_setup.bash
p=src/crop_pipeline_benchmark
out=$(mktemp -d /ws/build/cyclone-matrix-XXXXXX)
echo "$out"
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/><PubSubMessageExchange name="iox" library="psmx_iox" config="LOG_LEVEL=INFO;"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
cat >/tmp/pool.toml <<'TOML'
[general]
version = 1
[[segment]]
[[segment.mempool]]
size = 4096
count = 256
[[segment.mempool]]
size = 1048576
count = 24
TOML
if pgrep -x iox-roudi >/dev/null; then echo "An iceoryx RouDi daemon is already running" >&2; exit 2; fi
iox-roudi -c /tmp/pool.toml >"$out/roudi.log" 2>&1 &
daemon=$!
trap 'kill "$daemon" 2>/dev/null || true; wait "$daemon" || true' EXIT
sleep 1
kill -0 "$daemon" || { cat "$out/roudi.log"; exit 2; }
status=0
for backend in cpu shmem; do
  rc=0
  python3 "$p/run_case.py" rmw_cyclonedds_cpp "$backend" "$out/$backend.csv" --two-edge >"$out/$backend.log" 2>&1 || rc=$?
  if ((rc != 0)); then status=1; fi
  echo "$backend exit $rc rows $(wc -l < "$out/$backend.csv")"
  tail -4 "$out/$backend.log"
done
exit "$status"
