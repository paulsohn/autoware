#!/usr/bin/env bash
# Run with the rehearsal overlay sourced, without preloading into the supervisor.
set -eu
probe=$(realpath "${1:?supply qos_contract_check}")
hook=$(realpath "${2:?supply the released heap hook}")
out=$(mktemp -d)
cd "$out"
run=(env RMW_IMPLEMENTATION=rmw_agnocast "LD_PRELOAD=$hook" ROS_DISABLE_LOANED_MESSAGES=1)
"${run[@]}" "$probe" topics
"${run[@]}" "$probe" best_available
"${run[@]}" "$probe" server >server.log 2>&1 &
server=$!
"${run[@]}" "$probe" client &
client=$!
cleanup() {
  kill -CONT "$server" 2>/dev/null || true
  kill -TERM "$server" "$client" 2>/dev/null || true
  wait "$server" "$client" 2>/dev/null || true
}
trap cleanup EXIT
wait_for() {
  for ((i=0;i<800;++i)); do
    [[ -f "$1" ]] && return
    kill -0 "$client" && kill -0 "$server" || { cat server.log; return 1; }
    sleep .01
  done
  return 1
}
wait_for stop-ready
kill -STOP "$server"
wait_for expired
kill -CONT "$server"
wait_for kill-ready
kill -KILL "$server"
wait "$server" 2>/dev/null || true
wait "$client"
trap - EXIT
cd /
rm -f "$out"/{server.log,stop-ready,expired,kill-ready}
rmdir "$out"
