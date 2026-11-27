cmake_minimum_required(VERSION 3.16)
project(autoware_image_projection_based_fusion_models NONE)
find_package(ament_cmake REQUIRED)
install(FILES
  README.md
  deploy_metadata.yaml
  detection_class_remapper.param.yaml
  pointpainting_ml_package.param.yaml
  pts_backbone_neck_head_pointpainting.onnx
  pts_voxel_encoder_pointpainting.onnx
  SHA256SUMS
  MODEL_PROVENANCE.md
  DESTINATION share/${PROJECT_NAME}
)
ament_package()
