cmake_minimum_required(VERSION 3.16)
project(rmw_agnocast_discovery_msgs)

find_package(ament_cmake REQUIRED)
find_package(rosgraph_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/DiscoveryEndpoint.msg"
  "msg/IslandSnapshot.msg"
  DEPENDENCIES rosgraph_msgs
)
ament_export_dependencies(rosidl_default_runtime)
ament_package()
