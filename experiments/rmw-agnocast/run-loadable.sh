#!/usr/bin/env bash
set -e
w=/ws/build/rmw-agnocast
source /ws/build/agnocast-lyrical/install/local_setup.bash
source "$w/install/local_setup.bash"
source "$w/msg-install/local_setup.bash"
source "$w/load-install/local_setup.bash"
probe="$w/load-install/agnocast_rmw_loan_probe/lib/agnocast_rmw_loan_probe/loadable_probe"
export RMW_IMPLEMENTATION=rmw_agnocast AGNOCAST_RMW_TOPIC=/trajectory_loan_probe
export AGNOCAST_BRIDGE_MODE=OFF AGNOCAST_NO_DISCOVERY_AGENT=1
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
hook=/ws/build/agnocast-lyrical/install/agnocastlib/lib/libagnocast_heaphook.so

if [[ ${1:-} == bridge ]]; then
  for direction in a2r r2a; do
    for active in off on; do
      cd "$(mktemp -d)"
      reader_env=(env RMW_IMPLEMENTATION=rmw_cyclonedds_cpp)
      writer_env=(env LD_PRELOAD="$hook")
      if [[ $direction == r2a ]]; then
        reader_env=(env LD_PRELOAD="$hook")
        writer_env=(env RMW_IMPLEMENTATION=rmw_cyclonedds_cpp)
      fi
      pids=()
      trap 'for pid in "${pids[@]}"; do kill -INT "$pid" 2>/dev/null || true; done' EXIT
      if [[ $active == on ]]; then
        RMW_IMPLEMENTATION=rmw_cyclonedds_cpp LD_PRELOAD="$hook" "$(dirname "$probe")/manual_bridge" "$direction" &
        pids+=("$!")
        for ((i=0;i<80;i++)); do
          [[ -f bridge_ready ]] && break
          sleep 0.1
        done
        [[ -f bridge_ready ]]
      fi
      "${reader_env[@]}" "$probe" reader boundary "$active" &
      reader=$!
      pids+=("$reader")
      "${writer_env[@]}" "$probe" writer boundary "$active"
      wait "$reader"
      if [[ $active == on ]]; then
        kill -INT "${pids[0]}"
        wait "${pids[0]}"
      fi
      trap - EXIT
      echo "$direction $active passed"
    done
  done
  exit 0
fi

if [[ ${1:-} == host-cloud || ${1:-} == host-view || ${1:-} == host-loan ]]; then
  cd "$(mktemp -d)"
  export AGNOCAST_RMW_HOST_CLOUD=1
  if [[ $1 == host-loan ]]; then export AGNOCAST_RMW_HOST_LOAN=1; fi
  if [[ $1 != host-cloud ]]; then export AGNOCAST_RMW_HOST_VIEW=1 AGNOCAST_VIEW_ADDRESS_FILE=view-address; fi
  LD_PRELOAD="$hook" "$probe" reader host-cloud &
  reader=$!
  trap 'kill "$reader" 2>/dev/null || true' EXIT
  LD_PRELOAD="$hook" "$probe" writer host-cloud
  wait "$reader"
  trap - EXIT
  exit 0
fi

if [[ ${1:-} == contract ]]; then
  cd "$(mktemp -d)"
  ROS_DISABLE_LOANED_MESSAGES=0 LD_PRELOAD="$hook" "$probe" writer contract &
  writer=$!
  trap 'kill "$writer" 2>/dev/null || true' EXIT
  status=0
  ROS_DISABLE_LOANED_MESSAGES=0 LD_PRELOAD="$hook" "$probe" reader contract || status=$?
  [[ $status == 137 && -f kill_ready ]]
  touch kill_confirmed
  wait "$writer"
  trap - EXIT
  exit 0
fi

if [[ ${1:-} == pressure ]]; then
  cd "$(mktemp -d)"
  pids=()
  trap 'for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done' EXIT
  for role in fast slow writer; do
    ROS_DISABLE_LOANED_MESSAGES=0 LD_PRELOAD="$hook" "$probe" "$role" pressure &
    pids+=("$!")
  done
  wait "${pids[1]}"
  for ((i=0; i<120; i++)); do
    [[ -f restart_now ]] && break
    sleep 0.1
  done
  [[ -f restart_now ]]
  ROS_DISABLE_LOANED_MESSAGES=0 LD_PRELOAD="$hook" "$probe" restart pressure
  wait "${pids[0]}"
  wait "${pids[2]}"
  trap - EXIT
  echo 'Pressure passed: fast 1..11, slow 1/8/9, restarted 11; retained 1 through teardown'
  exit 0
fi

for mode in loan copy; do
 cd "$(mktemp -d)"
 disabled=0
 if [[ $mode == copy ]]; then disabled=1; fi
 ROS_DISABLE_LOANED_MESSAGES=$disabled LD_PRELOAD="$hook" "$probe" reader "$mode" "${1:-trajectory}" &
 reader=$!
 trap 'kill "$reader" 2>/dev/null || true' EXIT
 ROS_DISABLE_LOANED_MESSAGES=0 LD_PRELOAD="$hook" "$probe" writer "$mode" "${1:-trajectory}"
 wait "$reader"
 trap - EXIT
done
