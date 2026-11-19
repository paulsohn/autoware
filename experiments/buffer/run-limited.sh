#!/usr/bin/env bash
set -eu
unit="aw-buffer-$$"
available() { awk '/MemAvailable:/ {print int($2/1024)}' /proc/meminfo; }
minimum=$(available)
if ((minimum < 3072)); then echo "Host has only $minimum MiB available" >&2; exit 75; fi
systemd-run --user --scope --quiet --collect --unit="$unit" -p MemoryMax=1200M -p MemorySwapMax=0 "$@" &
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
