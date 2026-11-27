# TransFusion model inputs

Imported from https://huggingface.co/AutowareFoundation/lidar_transfusion,
tag v2.1, immutable revision 8f07eb93038bfd62e35d1f78e56a6b3fb76b12aa.
The model card declares Apache-2.0. Preserve transfusion.onnx (172,460,238 bytes),
ML parameters, class-remapper parameters, README and deployment metadata.
SHA256SUMS records the immutable upstream LFS payload hash.

This repository owns autoware_lidar_transfusion_models, :models and :ament_index,
plus package.xml/minimal colcon/ament_cmake support. Resolve inputs through its
share directory. Colcon users need this augmented repository, --symlink-install
and the sourced install environment; the upstream HF ref lacks package metadata.
Preserve .gitattributes, materialize LFS objects and publish them to the containing
repository's remote. Keep immutable provenance separate from later model edits.
Concrete precision/profile and writable engine/cache policy belong to product
consumers; never generate engines inside indexed immutable resources.
