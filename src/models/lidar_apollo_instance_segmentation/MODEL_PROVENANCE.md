# Apollo LiDAR instance-segmentation models

Imported from https://huggingface.co/AutowareFoundation/lidar_apollo_instance_segmentation,
tag v1.0, immutable revision 0b4e73bcfecfa9660c3d2782e2888aca5c2fdc2b.
Its README declares Apache-2.0. Preserve all three ONNX inputs (HDL-64, VLP-16,
VLS-128), README and deployment metadata. SHA256SUMS records immutable upstream
LFS payload hashes; the 64/16-channel inputs are 11,906,335 bytes each and the
128-channel input is 11,906,144 bytes.

This repository owns autoware_lidar_apollo_instance_segmentation_models,
:models and :ament_index, plus minimal colcon/ament_cmake support. Resolve inputs
through this package's share directory. Colcon users need the augmented model
repository, a --symlink-install build and its sourced install environment; the
original HF revision lacks package metadata. Preserve .gitattributes, materialize
LFS payloads, and publish their objects to the containing repository's own remote.
Keep immutable import provenance separate from later model edits. Product model/
precision selection and writable engine/cache policy belong to consumers.
