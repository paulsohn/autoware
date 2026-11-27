# camera_streampetr model inputs

Imported from https://huggingface.co/AutowareFoundation/camera_streampetr, tag v1.0,
immutable revision 90f37d64186686ebc458509d69d3d4ca16d185c7. The model card declares Apache-2.0.
SHA256SUMS records the immutable upstream LFS payload hashes. Preserve model
parameters, class remapping and deployment metadata with the weights.

This repository owns autoware_camera_streampetr_models, its :models/:ament_index targets,
package.xml and minimal colcon/ament_cmake support. Materialize LFS objects before
building and publish them with the parent repository. Colcon users need this
augmented repository, --symlink-install and a sourced install environment.
Upstream HF refs do not contain package metadata. Resolve inputs through this
package's share. Concrete precision/profile and writable engine-cache policy
belong to consumers. Keep indexed weights immutable.
