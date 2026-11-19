#!/usr/bin/env bash
set -e
w=/ws/build/rmw-agnocast
deps="$w/test-deps/opt/ros/lyrical"
export AMENT_PREFIX_PATH="$deps:$AMENT_PREFIX_PATH"
export CMAKE_PREFIX_PATH="$deps:$deps/opt/mimick_vendor:$w/test-deps/usr:$CMAKE_PREFIX_PATH"
export LD_LIBRARY_PATH="$w/obj/rclcpp:$deps/lib:$deps/opt/mimick_vendor/lib:$w/test-deps/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"
export XML_CATALOG_FILES=/ws/build/agnocast-lyrical/catalog.xml
export RMW_IMPLEMENTATIONS=rmw_cyclonedds_cpp RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export CYCLONEDDS_URI='<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces></General></Domain></CycloneDDS>'
obj="$w/obj/rclcpp"
cmake -S src/rclcpp/rclcpp -B "$obj" -DBUILD_TESTING=ON
cmake --build "$obj" --target test_loaned_message test_subscription test_executor --parallel 1
ctest --test-dir "$obj" -R '^(test_loaned_message|test_subscription|test_executor)$' --output-on-failure --no-tests=error
