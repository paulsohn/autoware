# PointPainting model inputs

Imported from https://huggingface.co/AutowareFoundation/image_projection_based_fusion,
tag v5.0, immutable revision 33119d71afd53fbc3994dcffc71e6247b76cdd5a.
The model card declares Apache-2.0. Preserve the 20,140,082-byte backbone/head,
7,621-byte voxel encoder, ML parameters, class remapper and deployment metadata.
SHA256SUMS records the immutable upstream LFS payload hashes.

This repository owns autoware_image_projection_based_fusion_models and its
:models/:ament_index targets, package.xml and minimal colcon/ament_cmake support.
Materialize LFS objects before building and publish them with the parent repository.
Colcon users need this augmented repository, --symlink-install and a sourced
install environment. Upstream HF refs do not contain package metadata.
Resolve inputs through this package's share. Concrete precision/profile and
writable engine-cache policy belong to consumers. Keep indexed weights immutable.
