#!/usr/bin/env bash
set -e
source /opt/ros/jazzy/setup.bash
export ENABLE_AGNOCAST=1 RMW_IMPLEMENTATION=rmw_cyclonedds_cpp ROS_DOMAIN_ID=194
export MAKEFLAGS=-j1 CMAKE_BUILD_PARALLEL_LEVEL=1 CC=clang-18 CXX=clang++-18
w=build/universe-jazzy
opts=(--build-base "$w/obj" --install-base "$w/install" --executor sequential
  --cmake-args -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG')
colcon --log-base "$w/log" build --base-paths \
  src/core/agnocast/src/{agnocast_cie_config_msgs,agnocast_cie_thread_configurator,agnocastlib} \
  src/core/autoware_internal_msgs/{autoware_internal_debug_msgs,autoware_internal_msgs} \
  src/core/autoware_cmake src/core/autoware_utils/autoware_utils_rclcpp \
  src/core/autoware_core/common/autoware_agnocast_wrapper \
  src/universe/external/managed_transform_buffer/managed_transform_buffer "${opts[@]}"
source "$w/install/local_setup.bash"
p=src/universe_crop_benchmark
colcon --log-base "$w/log" build --base-paths "$p" "${opts[@]}"
source "$w/install/local_setup.bash"
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
mkdir -p "$p/results"
for round in 1 2 3; do
  cases="cyclone agnocast"
  if ((round == 2)); then cases="agnocast cyclone"; fi
  for mode in $cases; do
    python3 "$p/run_case.py" "$mode" "$p/results/$mode-$round.csv"
  done
done
python3 src/crop_pipeline_benchmark/summarize.py "$p/results" cyclone agnocast
