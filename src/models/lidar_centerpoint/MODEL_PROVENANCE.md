# CenterPoint model bundles

Imported from https://huggingface.co/AutowareFoundation/lidar_centerpoint,
tag v4.1, immutable revision 494c8171def40bd36cc2feb323e0a5acbfab132b.
The model card declares Apache-2.0. Preserve base, short_range, sigma and tiny
bundles, each with two ONNX inputs, ml_package.param.yaml and class-remapper
metadata. SHA256SUMS records all eight immutable upstream LFS payload hashes;
verify sizes against that revision's HF metadata before import/update.

This repository owns autoware_lidar_centerpoint_models, :models and :ament_index,
plus minimal colcon/ament_cmake support. Resolve bundles through the package's
share directory. Colcon users need this augmented repository, --symlink-install
and the sourced install environment; the upstream HF ref lacks package metadata.
Preserve .gitattributes, materialize LFS payloads, and publish their objects to
the containing repository's remote. Keep immutable import provenance separate
from subsequent model edits. Concrete model/precision/cache choices belong to
product consumers, not the model provider. Runtime engines must not be written
into immutable indexed resources.
