#!/usr/bin/env bash
set -eu
unit="aw-buffer-$$"
available() { awk '/MemAvailable:/ {print int($2/1024)}' /proc/meminfo; }
limit=1200
start=3072
if [[ ${BUFFER_RUNTIME_ONLY:-0} == 1 ]]; then limit=512; start=2816; fi
minimum=$(available)
if ((minimum < start)); then echo "Host has only $minimum MiB available" >&2; exit 75; fi
systemd-run --user --scope --quiet --collect --unit="$unit" -p MemoryMax="${limit}M" -p MemorySwapMax=0 env --default-signal=INT --default-signal=QUIT "$@" &
job=$!
while kill -0 "$job" 2>/dev/null; do
  current=$(available)
  if ((current < minimum)); then minimum=$current; fi
  if ((current < 2048)); then
    echo "Stopping $unit at $current MiB available" >&2
    systemctl --user stop "$unit.scope"
    exit 75
  fi
  sleep .25
done
status=0
wait "$job" || status=$?
echo "Minimum host MemAvailable: $minimum MiB"
exit "$status"
