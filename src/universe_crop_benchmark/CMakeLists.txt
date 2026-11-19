cmake_minimum_required(VERSION 3.16)
project(universe_crop_benchmark)
find_package(ament_cmake REQUIRED)
set(deps rclcpp rclcpp_components agnocastlib autoware_agnocast_wrapper managed_transform_buffer
  sensor_msgs geometry_msgs pcl_ros pcl_conversions pcl_msgs point_cloud_msg_wrapper
  autoware_internal_debug_msgs autoware_internal_msgs)
foreach(pkg IN LISTS deps)
  find_package(${pkg} REQUIRED)
endforeach()
set(aw "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(pre "${aw}/universe/autoware_universe/sensing/autoware_pointcloud_preprocessor")
add_library(universe_crop SHARED "${pre}/src/filter.cpp" "${pre}/src/utility/memory.cpp"
  "${pre}/src/crop_box_filter/crop_box_filter_node.cpp")
target_include_directories(universe_crop PRIVATE "${pre}/include"
  "${aw}/core/autoware_core/common/autoware_point_types/include")
foreach(pkg IN ITEMS autoware_utils autoware_utils_debug autoware_utils_diagnostics autoware_utils_system)
  target_include_directories(universe_crop PRIVATE "${aw}/core/autoware_utils/${pkg}/include")
endforeach()
ament_target_dependencies(universe_crop ${deps})
autoware_agnocast_wrapper_register_node(universe_crop
  PLUGIN "autoware::pointcloud_preprocessor::CropBoxFilterComponent" EXECUTABLE crop
  AGNOCAST_EXECUTOR AgnocastOnlyCallbackIsolatedExecutor ROS2_EXECUTOR SingleThreadedExecutor)
add_library(universe_endpoint SHARED endpoint.cpp)
ament_target_dependencies(universe_endpoint rclcpp rclcpp_components sensor_msgs agnocastlib autoware_agnocast_wrapper)
autoware_agnocast_wrapper_register_node(universe_endpoint
  PLUGIN "benchmark::Endpoint" EXECUTABLE endpoint
  AGNOCAST_EXECUTOR AgnocastOnlySingleThreadedExecutor ROS2_EXECUTOR SingleThreadedExecutor)
install(TARGETS universe_crop universe_endpoint DESTINATION lib)
ament_package()
