# bevfusion model inputs

Imported from https://huggingface.co/AutowareFoundation/bevfusion, tag v2.0,
immutable revision e1bf164b909d3c6c2642012f9b5ee4d827753220. The model card declares Apache-2.0.
SHA256SUMS records the immutable upstream LFS payload hashes. Preserve model
parameters, class remapping and deployment metadata with the weights.

This repository owns autoware_bevfusion_models, its :models/:ament_index targets,
package.xml and minimal colcon/ament_cmake support. Materialize LFS objects before
building and publish them with the parent repository. Colcon users need this
augmented repository, --symlink-install and a sourced install environment.
Upstream HF refs do not contain package metadata. Resolve inputs through this
package's share. Concrete precision/profile and writable engine-cache policy
belong to consumers. Keep indexed weights immutable.
