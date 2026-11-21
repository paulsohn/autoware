cmake_minimum_required(VERSION 3.16)
project(universe_sync_benchmark)
find_package(ament_cmake REQUIRED)
set(deps rclcpp_components rclcpp sensor_msgs nav_msgs geometry_msgs autoware_internal_debug_msgs
  autoware_sensing_msgs diagnostic_updater managed_transform_buffer point_cloud_msg_wrapper
  message_filters pcl_conversions pcl_ros)
set(aw "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(pre "${aw}/universe/autoware_universe/sensing/autoware_pointcloud_preprocessor")
set(sync "${pre}/src/concatenate_data")
add_library(universe_sync SHARED
  "${sync}/concatenate_and_time_sync_node.cpp" "${sync}/cloud_collector.cpp"
  "${sync}/collector_matcher.cpp" "${sync}/matching_policy.cpp"
  "${sync}/combine_cloud_handler.cpp" "${sync}/combine_cloud_handler_base.cpp"
  "${sync}/concatenation_diagnostics.cpp" "${pre}/src/utility/memory.cpp")
target_include_directories(universe_sync PRIVATE "${pre}/include"
  "${aw}/core/autoware_core/common/autoware_point_types/include")
foreach(pkg IN ITEMS autoware_utils autoware_utils_debug autoware_utils_diagnostics autoware_utils_system)
  target_include_directories(universe_sync PRIVATE "${aw}/core/autoware_utils/${pkg}/include")
endforeach()
foreach(pkg IN LISTS deps)
  find_package(${pkg} REQUIRED)
  target_include_directories(universe_sync SYSTEM PRIVATE ${${pkg}_INCLUDE_DIRS})
  if(${pkg}_TARGETS)
    target_link_libraries(universe_sync ${${pkg}_TARGETS})
  else()
    target_link_libraries(universe_sync ${${pkg}_LIBRARIES})
  endif()
endforeach()
rclcpp_components_register_node(universe_sync
  PLUGIN "autoware::pointcloud_preprocessor::PointCloudConcatenateDataSynchronizerComponent"
  EXECUTABLE concatenate_and_time_sync_node)
install(TARGETS universe_sync DESTINATION lib)
add_executable(sync_fixture fixture.cpp)
target_link_libraries(sync_fixture ${rclcpp_TARGETS} ${sensor_msgs_TARGETS} ${autoware_sensing_msgs_TARGETS})
install(TARGETS sync_fixture DESTINATION lib/${PROJECT_NAME})
ament_package()
