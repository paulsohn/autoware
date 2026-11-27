cmake_minimum_required(VERSION 3.16)
project(autoware_tensorrt_bevdet_models NONE)
find_package(ament_cmake REQUIRED)
install(FILES
  README.md
  bevdet_one_lt_d.onnx
  deploy_metadata.yaml
  SHA256SUMS
  MODEL_PROVENANCE.md
  DESTINATION share/${PROJECT_NAME}
)
ament_package()
