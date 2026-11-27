# Calibration-status classifier model

Imported from https://huggingface.co/AutowareFoundation/calibration_status_classifier,
tag v2.0, immutable revision ea54ddcf78063f65f54dd091d0ed1bc27d89ebd2.
The upstream README declares Apache-2.0. Preserve its model explanation and
original ONNX, deployment and preprocessing metadata. The 44,825,085-byte ONNX
matches the upstream Git LFS SHA-256 in SHA256SUMS. Keep LFS materialized and
mirror objects to the containing repository's own remote when pushing.

This repository owns autoware_calibration_status_classifier_models and its
:models/:ament_index targets. Resolve files from that package's share directory.
Colcon users need this augmented repository, its package.xml and a
--symlink-install build; the original upstream revision has no ament package.
Do not add engine precision, optimization or product cache choices here.
Mutable engines belong in caller-selected writable caches, separate from these
indexed input files. Update current hashes with model edits while retaining
this immutable import record.
