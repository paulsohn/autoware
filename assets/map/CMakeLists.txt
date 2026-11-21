cmake_minimum_required(VERSION 3.24)
project(autoware_map_asset NONE)

find_package(ament_cmake REQUIRED)
include(FetchContent)

FetchContent_Declare(
  map_data
  URL https://autoware-files.s3.us-west-2.amazonaws.com/maps/demos/sample-map-planning.zip
  URL_HASH SHA256=5536fce7bb8db7688fdf94ec004118b898637ad0d5b6175108b10989dd6e93b9
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(map_data)

install(
  FILES
    "${map_data_SOURCE_DIR}/lanelet2_map.osm"
    "${map_data_SOURCE_DIR}/pointcloud_map.pcd"
    "${map_data_SOURCE_DIR}/map_projector_info.yaml"
    "${map_data_SOURCE_DIR}/map_config.yaml"
  DESTINATION share/${PROJECT_NAME}
)
ament_package()
