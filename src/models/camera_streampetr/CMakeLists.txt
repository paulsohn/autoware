cmake_minimum_required(VERSION 3.16)
project(autoware_camera_streampetr_models NONE)
find_package(ament_cmake REQUIRED)
install(FILES
  README.md
  deploy_metadata.yaml
  ml_package_camera_streampetr.param.yaml
  simplify_extract_img_feat.onnx
  simplify_position_embedding.onnx
  simplify_pts_head_memory.onnx
  SHA256SUMS
  MODEL_PROVENANCE.md
  DESTINATION share/${PROJECT_NAME}
)
ament_package()
