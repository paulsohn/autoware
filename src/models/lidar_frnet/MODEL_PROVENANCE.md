# FRNet sensor models

Imported from https://huggingface.co/AutowareFoundation/lidar_frnet,
tag v2.0, immutable revision a53a1c11b0b28d31fd50a03abef5db0e830a5b3c.
The model card declares Apache-2.0. Preserve both frnet_ot128.onnx and
frnet_qt128.onnx, their ML parameters, README and deployment metadata. Both
weights are 42,394,660 bytes; SHA256SUMS records immutable upstream LFS hashes.

This repository owns autoware_lidar_frnet_models, :models and :ament_index,
plus package.xml/minimal colcon/ament_cmake support. Resolve weights and metadata
through this package's share directory. Colcon users need the augmented model
repository, --symlink-install and the sourced install environment; the upstream
HF ref lacks package metadata. Preserve .gitattributes, materialize LFS objects,
and publish them to the containing repository's remote. Keep immutable import
provenance separate from later model edits. Concrete precision/profile and
writable engine/cache decisions belong to product consumers. Never generate
engines inside indexed immutable resources.
