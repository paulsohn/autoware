# PTV3 model inputs

Imported from https://huggingface.co/AutowareFoundation/ptv3,
tag v0.1.1, immutable revision d62e52800ecb7de41fa9cc530df2d5d18624cf2b.
The model card declares Apache-2.0. Preserve the encoder and detection/segmentation head ONNX payloads,
ML parameters, class-remapper parameters, README and deployment metadata.
SHA256SUMS records the immutable upstream LFS payload hash.

This repository owns autoware_ptv3_models, :models and :ament_index,
plus package.xml/minimal colcon/ament_cmake support. Resolve inputs through its
share directory. Colcon users need this augmented repository, --symlink-install
and the sourced install environment; the upstream HF ref lacks package metadata.
Preserve .gitattributes, materialize LFS objects and publish them to the containing
repository's remote. Keep immutable provenance separate from later model edits.
Concrete precision/profile and writable engine/cache policy belong to product
consumers; never generate engines inside indexed immutable resources.
