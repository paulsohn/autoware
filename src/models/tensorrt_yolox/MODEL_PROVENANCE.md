# YOLOX model package

Imported from https://huggingface.co/AutowareFoundation/tensorrt_yolox,
tag v1.0, immutable revision 424d6b9cb94c939ae9ce6a22380e46c13f76b1da.
The upstream README declares Apache-2.0. Original weights/metadata are unchanged.
SHA256SUMS records materialized ONNX payloads, not Git LFS pointer hashes.

This repository owns the autoware_tensorrt_yolox_models Bazel module and ament
package. Its indexed share directory contains the model and metadata filenames
unchanged. Depend on :ament_index for portable runtime lookup or :models for
explicit weight/metadata files; keep both within this repository boundary.

Git LFS must be installed before checkout; materialize the weights before Bazel
or colcon builds. The containing Git repository mirrors the LFS objects to its
own remote so fresh checkouts and locally edited weights use normal Git LFS
storage. Do not redirect the containing repository's LFS URL to Hugging Face.
When editing a weight, regenerate SHA256SUMS, review metadata together, and push
the LFS objects with the Git commit. Engine/cache outputs remain ignored.

Colcon users obtain this package, run colcon build --symlink-install and source
the resulting environment. A raw upstream checkout lacks this package metadata.
Launchers use $(find-pkg-share autoware_tensorrt_yolox_models), with no HOME path
fallback. Refer to the integration journal for validation and subtree updates.
