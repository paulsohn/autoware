# Traffic-light fine-detector models

Imported from https://huggingface.co/AutowareFoundation/traffic_light_fine_detector,
tag v3.0, immutable revision 3f0616ffda0c19fc84fc455f988421c0df970e5d.
The upstream README declares Apache-2.0. Retain its training/model explanation,
all three static batch 1/4/6 ONNX inputs, labels and deployment metadata.
Each ONNX is 35,794,092 bytes; SHA256SUMS matches immutable upstream LFS records.

This repository owns autoware_traffic_light_fine_detector_models, :models and
its own :ament_index. Resolve original filenames inside its share directory.
Colcon users need this augmented repository and --symlink-install, then source
the install environment; the original HF revision lacks ament build metadata.
Keep LFS materialized and mirror objects to the containing repository's own
remote. Update current hashes with weight edits while retaining immutable import
provenance. Precision/profile selection and mutable engines belong to callers,
not this input repository.
