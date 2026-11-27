cmake_minimum_required(VERSION 3.16)
project(autoware_bevfusion_models NONE)
find_package(ament_cmake REQUIRED)
install(FILES
  README.md
  bevfusion_camera_lidar.onnx
  bevfusion_image_backbone.onnx
  bevfusion_lidar.onnx
  deploy_metadata.yaml
  detection_class_remapper.param.yaml
  ml_package_bevfusion_camera_lidar.param.yaml
  ml_package_bevfusion_lidar.param.yaml
  SHA256SUMS
  MODEL_PROVENANCE.md
  DESTINATION share/${PROJECT_NAME}
)
ament_package()
