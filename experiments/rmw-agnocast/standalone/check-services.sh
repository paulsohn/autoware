#!/usr/bin/env bash
set -eu
probe=${1:?service check executable}
name="/api_test_${BASHPID}_${RANDOM}"
"$probe" server "$name" &
server=$!
trap 'kill -INT "$server" 2>/dev/null || true; wait "$server" || true' EXIT
timeout -k 2 12 "$probe" first "$name" &
first=$!
timeout -k 2 12 "$probe" second "$name" &
second=$!
status=0
wait "$first" || status=1
wait "$second" || status=1
exit "$status"
