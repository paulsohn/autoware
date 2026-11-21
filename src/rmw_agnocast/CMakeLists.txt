cmake_minimum_required(VERSION 3.16)
project(rmw_agnocast)
find_package(ament_cmake REQUIRED)
foreach(pkg IN ITEMS sensor_msgs agnocastlib rosidl_typesupport_introspection_cpp rclcpp rmw rmw_cyclonedds_cpp)
  find_package(${pkg} REQUIRED)
endforeach()
add_library(${PROJECT_NAME} SHARED rmw.cpp)
# rmw_implementation resolves unoverridden symbols through this first ELF dependency.
target_link_libraries(${PROJECT_NAME} PRIVATE "-Wl,--no-as-needed" rmw_cyclonedds_cpp::rmw_cyclonedds_cpp
  "-Wl,--as-needed" ${sensor_msgs_TARGETS} ${agnocastlib_TARGETS} ${rosidl_typesupport_introspection_cpp_TARGETS}
  rclcpp::rclcpp rmw::rmw ${CMAKE_DL_LIBS})
register_rmw_implementation("c:rosidl_typesupport_introspection_c" "cpp:rosidl_typesupport_introspection_cpp")
install(TARGETS ${PROJECT_NAME} DESTINATION lib)
install(FILES host_cloud_view.hpp DESTINATION include/rmw_agnocast)
ament_export_include_directories(include)
ament_package()
