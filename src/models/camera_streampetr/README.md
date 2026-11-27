---
license: apache-2.0
pipeline_tag: object-detection
tags:
  - autoware
  - ros2
  - autonomous-driving
  - camera
  - multi-view
  - 3d-object-detection
  - streampetr
  - tensorrt
  - onnx
---

# StreamPETR for Autoware (`camera_streampetr`)

Camera-only multi-view 3D object detection models, used by the
[`autoware_camera_streampetr`](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_camera_streampetr)
node in [Autoware](https://github.com/autowarefoundation/autoware).

The models follow the **StreamPETR** [1] architecture, an object-centric temporal modeling approach for
efficient multi-view 3D detection, and run with TensorRT inside Autoware. This is the first camera-only 3D
object detection node in Autoware: it detects 3D objects from surround-view camera images without any LiDAR
input. The models are exported as ONNX so they can be deployed across hardware; Autoware builds the TensorRT
engines from the ONNX files on first launch.

## Model overview

| | |
| --- | --- |
| Task | 3D object detection (oriented bounding boxes) from multi-view camera images |
| Architecture | StreamPETR (image backbone + 3D position embedding + temporal propagation head) |
| Detected classes | `CAR`, `TRUCK`, `BUS`, `BICYCLE`, `PEDESTRIAN` |
| Cameras | 5 views: `CAM_FRONT`, `CAM_FRONT_LEFT`, `CAM_BACK_LEFT`, `CAM_FRONT_RIGHT`, `CAM_BACK_RIGHT` |
| Input resolution | 640 x 480 per camera (after node-side resize/crop/normalize) |
| Detection range | `[-61.2, -61.2, -10.0, 61.2, 61.2, 10.0]` m from `base_link` |
| Temporal modeling | Propagated object memory (input length 1024, output length 1280), 5400 proposals |
| Runtime | TensorRT (FP16 by default, FP32 selectable) via the `autoware_camera_streampetr` ROS 2 node |
| Format | ONNX (Autoware builds the TensorRT engines locally on first launch) |
| License | Apache-2.0 |

The network is split into three ONNX sub-models, matching how the node consumes them:

1. **Image feature extractor (backbone)**: `simplify_extract_img_feat.onnx`
2. **Position embedding**: `simplify_position_embedding.onnx`
3. **Detection head with temporal memory**: `simplify_pts_head_memory.onnx`

Pre-processing (undistortion if needed, resize, crop, normalization, ego-vehicle masking) and post-processing
(circle NMS, IoU NMS, per-class confidence thresholding, yaw normalization) run in the node, not in the ONNX
graphs.

## Files

| File | Description |
| --- | --- |
| `simplify_extract_img_feat.onnx` | Image feature extractor (backbone) |
| `simplify_position_embedding.onnx` | 3D position embedding network |
| `simplify_pts_head_memory.onnx` | Detection head with temporal memory |
| `ml_package_camera_streampetr.param.yaml` | Model parameters (classes, detection range, input resolution, camera count, proposal and memory lengths) |
| `deploy_metadata.yaml` | Deployment metadata recording the artifact version of this repository |

> **TensorRT engines are not distributed here.** TensorRT engines are specific to the GPU architecture and
> TensorRT version they are built on and are not portable, so Autoware builds them locally from the ONNX files
> on first launch (or via `build_only:=true`).

## Inputs and outputs (as used by the node)

**Inputs**

- `~/input/camera*/image` (`sensor_msgs/msg/Image` or `sensor_msgs/msg/CompressedImage`): camera images,
  compressed or uncompressed.
- `~/input/camera*/camera_info` (`sensor_msgs/msg/CameraInfo`): camera intrinsics.

The node is optimized for multi-camera systems where camera topics arrive sequentially rather than all at
once: each image is preprocessed and stored on the GPU as it arrives, and the forward pass is triggered when
the anchor camera's image arrives and all images are synchronized within `max_camera_time_diff`.

**Output**

- `~/output/objects` (`autoware_perception_msgs/msg/DetectedObjects`): oriented 3D boxes with class and score.
- `latency/*` (`autoware_internal_debug_msgs/msg/Float64Stamped`): per-stage timing topics for debugging.
  On an RTX 3090 the package reports a total processing time of about 26 ms per inference
  (the reported time between consecutive predictions is about 111 ms).

## Usage in Autoware

The node expects these artifacts in `~/autoware_data/ml_models/camera_streampetr/` (the default `model_path`)
and launches with:

```bash
ros2 launch autoware_camera_streampetr streampetr.launch.xml \
  model_path:=$HOME/autoware_data/ml_models/camera_streampetr
```

Add `build_only:=true` to build the TensorRT engines from the ONNX files as a one-off pre-task.
The `ml_package_camera_streampetr.param.yaml` shipped here is loaded by the launch file and carries the
model-dependent parameters; node behavior parameters live in the package's own
`config/camera_streampetr.param.yaml`.
See the [package README](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_camera_streampetr)
for the full parameter reference.

## Training

The model was trained and exported with the AWML StreamPETR project:

- <https://github.com/tier4/AWML/tree/main/projects/StreamPETR>

Training datasets, schedules, and evaluation metrics for these specific weights are not publicly documented.

## Limitations

- The model only predicts objects inside the detection range above; the node filters out anything beyond it.
- Only the five classes above are detected. Other road users fall outside the label set.
- The node assumes undistorted input images (or enables undistortion itself), accurate camera calibration,
  cameras synchronized within `max_camera_time_diff`, and tf transforms between the camera frames,
  `base_link`, and `map` (for ego motion compensation).
- Predictions do not include object velocity.

## Provenance

| | |
| --- | --- |
| Original source | `https://awf.ml.dev.web.auto/perception/models/streampetr/v1/` |
| This repository | `AutowareFoundation/camera_streampetr`, tag `v1.0` |

The `v1.0` tag corresponds to the `streampetr/v1` artifact version at the original hosting path.

## Citation

```bibtex
@article{wang2023streampetr,
  title   = {Exploring Object-Centric Temporal Modeling for Efficient Multi-View 3D Object Detection},
  author  = {Wang, Shihao and Liu, Yingfei and Wang, Tiancai and Li, Ying and Zhang, Xiangyu},
  journal = {arXiv preprint arXiv:2303.11926},
  year    = {2023}
}
```

## References

- [1] Wang et al., "Exploring Object-Centric Temporal Modeling for Efficient Multi-View 3D Object Detection", arXiv:2303.11926, 2023.
- AWML StreamPETR project: <https://github.com/tier4/AWML/tree/main/projects/StreamPETR>
- Autoware: <https://github.com/autowarefoundation/autoware>
