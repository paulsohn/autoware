# Traffic-light classifier models

Imported from https://huggingface.co/AutowareFoundation/traffic_light_classifier,
tag v4.0, immutable revision 527c4905afd8f1bfcc03253529a105f7fa5a6d36.
Its README declares Apache-2.0. Preserve all seven ONNX inputs (car/pedestrian
batch 1/4/6 and the lamp recognizer), labels, recognizer parameters and deployment
metadata. SHA256SUMS matches the immutable upstream LFS payload hashes and sizes.

The repository owns autoware_traffic_light_classifier_models, :models and its
own :ament_index. Resolve files in that package's share directory. Colcon users
need this augmented repository and a --symlink-install build, then source its
install environment; the original HF revision lacks ament metadata. Keep LFS
materialized, preserve .gitattributes, push objects to the containing repository's
own remote and update hashes with edited weights. Retain immutable import
provenance separately. Product precision/profile and writable engine policy do
not belong to this input repository.
