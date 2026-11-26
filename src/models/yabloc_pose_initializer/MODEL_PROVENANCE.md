# YabLoc pose initializer models

Imported from https://huggingface.co/AutowareFoundation/yabloc_pose_initializer,
tag v1.0, immutable revision b24e397472836bc41b11a4c47e52ff6adac30a74.
The upstream README declares Apache-2.0 and documents Intel Open Model Zoo /
PINTO origins. Retain its README, deployment metadata, ONNX and TensorFlow files.
SHA256SUMS records both materialized payloads, matching upstream Git LFS records.
The launch default selects saved_model/model_float32.onnx: the source already
supports its NHWC boundary and CPU OpenCV DNN execution. Packaging the original
TensorFlow file does not qualify that legacy importer on the selected SDK.

This repository owns the autoware_yabloc_pose_initializer_models Bazel module
and ament package. :models exposes original files; :ament_index exposes the
same saved_model/ hierarchy under share/autoware_yabloc_pose_initializer_models.
Resolve resources through the index, without HOME or workspace path fallbacks.
Colcon users must obtain this augmented package, build with --symlink-install
and source install/setup.bash. The unmodified upstream revision lacks package.xml.

Install Git LFS before checkout and materialize both weights before building.
Keep the original nested .gitattributes; mirror LFS objects to the containing
repository's own remote. Do not redirect the whole repository's LFS endpoint.
Update SHA256SUMS together with edited weights and metadata; retain the immutable
import provenance separately. No engine compilation or GPU policy lives here.
