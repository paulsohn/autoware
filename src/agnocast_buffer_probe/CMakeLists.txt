cmake_minimum_required(VERSION 3.16)
project(agnocast_buffer_probe)
find_package(ament_cmake REQUIRED)
foreach(pkg IN ITEMS agnocastlib rclcpp rosidl_buffer rosidl_buffer_backend pluginlib shmem_buffer
    std_msgs sensor_msgs rosidl_typesupport_cpp)
  find_package(${pkg} REQUIRED)
endforeach()
add_library(agnocast_buffer_probe_plugin SHARED backend.cpp reservations.cpp)
target_compile_features(agnocast_buffer_probe_plugin PUBLIC cxx_std_20)
target_link_libraries(agnocast_buffer_probe_plugin PUBLIC ${agnocastlib_TARGETS}
  rclcpp::rclcpp rosidl_buffer::rosidl_buffer rosidl_buffer_backend::rosidl_buffer_backend
  pluginlib::pluginlib ${std_msgs_TARGETS} rosidl_typesupport_cpp::rosidl_typesupport_cpp)
pluginlib_export_plugin_description_file(rosidl_buffer_backend plugin.xml)
add_executable(probe probe.cpp)
target_link_libraries(probe PRIVATE agnocast_buffer_probe_plugin ${sensor_msgs_TARGETS} shmem_buffer::shmem_buffer)
install(TARGETS agnocast_buffer_probe_plugin DESTINATION lib)
install(TARGETS probe DESTINATION lib/${PROJECT_NAME})
ament_package()
