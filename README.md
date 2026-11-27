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
  - bevdet
  - tensorrt
  - onnx
---

# BEVDet for Autoware (`tensorrt_bevdet`)

Multi-view camera 3D object detection model, used by the
[`autoware_tensorrt_bevdet`](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_tensorrt_bevdet)
node in [Autoware](https://github.com/autowarefoundation/autoware).

The model follows the **BEVDet** architecture: it unifies six surround-view camera images into a bird's-eye-view
(BEV) representation for the 3D object detection task, deployed with the **BEVPoolv2** [1] TensorRT/C++
implementation. It is distributed as ONNX; Autoware builds the TensorRT engine from the ONNX file on first
launch.

## Model overview

| | |
| --- | --- |
| Task | 3D object detection (oriented bounding boxes) from 6 surround-view camera images |
| Architecture | BEVDet (configuration `bevdet_r50_4dlongterm_depth`: depth-aware BEV pooling with temporal fusion of 8 adjacent frames) |
| Detected classes | `car`, `truck`, `construction_vehicle`, `bus`, `trailer`, `barrier`, `motorcycle`, `bicycle`, `pedestrian`, `traffic_cone` |
| Cameras | `CAM_FRONT_LEFT`, `CAM_FRONT`, `CAM_FRONT_RIGHT`, `CAM_BACK_LEFT`, `CAM_BACK`, `CAM_BACK_RIGHT` |
| Network input size | 256 x 704 (resized/cropped from 900 x 1600 source images) |
| BEV range | `[-51.2, -51.2, -5.0, 51.2, 51.2, 3.0]` m, 0.8 m grid |
| Runtime | TensorRT (FP16 by default, FP32 selectable) via the `autoware_tensorrt_bevdet` ROS 2 node |
| Format | ONNX (Autoware builds the TensorRT engine locally on first launch) |
| License | Apache-2.0 (see Legal Notice for training-data terms) |

The node also requires two configuration files that ship inside the `autoware_tensorrt_bevdet` package, not in
this repository:

- `config/bevdet.param.yaml`: ROS parameters (precision, score threshold, class names, camera list)
- `config/bevdet_r50_4dlongterm_depth.yaml`: model configuration (BEV range, grid, image pre-processing, NMS)

## Files

| File | Description |
| --- | --- |
| `bevdet_one_lt_d.onnx` | BEVDet network (long-term temporal fusion, depth branch), all six camera views |
| `deploy_metadata.yaml` | Deployment metadata recording the artifact version of this repository |

> **TensorRT engines are not distributed here.** TensorRT engines are specific to the GPU architecture and
> TensorRT version they are built on and are not portable, so Autoware builds them locally from the ONNX file
> on first launch (the node appends `_fp16.engine` or `_fp32.engine` to the configured engine path
> depending on the precision, so with the default config the engine lands next to the ONNX file).

## Inputs and outputs (as used by the node)

**Inputs**: six `sensor_msgs/msg/Image` topics (`~/input/topic_img_front_left`, `~/input/topic_img_front`,
`~/input/topic_img_front_right`, `~/input/topic_img_back_left`, `~/input/topic_img_back`,
`~/input/topic_img_back_right`) plus the matching six `sensor_msgs/msg/CameraInfo` topics.

**Outputs**:

- `~/output/boxes` (`autoware_perception_msgs/msg/DetectedObjects`): detected 3D objects
- `~/output_bboxes` (`visualization_msgs/msg/MarkerArray`): bounding-box markers for nuScenes visualization,
  published when `debug_mode` is enabled

## Usage in Autoware

The node loads the model from `~/autoware_data/ml_models/tensorrt_bevdet/` by default and launches with:

```bash
ros2 launch autoware_tensorrt_bevdet tensorrt_bevdet.launch.xml
```

Key launch arguments: `model_name` (default `bevdet_one_lt_d`), `model_path`
(default `$HOME/autoware_data/ml_models/tensorrt_bevdet`), `model_config`, and `param_file`. Precision
(`fp16`/`fp32`) and `debug_mode` are set in `config/bevdet.param.yaml`. The package README documents
TensorRT 10.8.0.43 and CUDA 12.4 as prerequisites, and describes how to test the node against
nuScenes data played back with `ros2_dataset_bridge`. See the
[package README](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_tensorrt_bevdet)
for the full instructions.

## Training

The model was trained on the nuScenes dataset for 20 epochs. Training, export, and deployment code:

- Original BEVDet: <https://github.com/HuangJunJie2017/BEVDet/tree/dev2.1>
- TensorRT C++ implementation: <https://github.com/LCH1238/bevdet-tensorrt-cpp/tree/one>
- ONNX export fork: <https://github.com/LCH1238/BEVDet/tree/export>
- Training/export fork adapted to the TIER IV dataset: <https://github.com/cyn-liu/BEVDet/tree/train_export>
- Autoware vendor package (modified TensorRT implementation): <https://github.com/autowarefoundation/bevdet_vendor>

## Limitations

- Trained only on nuScenes; the package README notes poor generalization to other data. To use this model on
  your own vehicle and camera setup, retraining is required (see the `train_export` fork above).
- Expects a six-camera surround-view rig matching the nuScenes camera layout.
- Only the ten nuScenes classes listed above are detected.

## Provenance and versioning

| | |
| --- | --- |
| Original source | `tensorrt_bevdet.tar.gz` from the Autoware artifacts S3 bucket (`https://autoware-files.s3.us-west-2.amazonaws.com/models/tensorrt_bevdet.tar.gz`), unversioned |
| This repository | `AutowareFoundation/tensorrt_bevdet`, extracted ONNX published as tag `v1.0` |

## Citation

```bibtex
@article{huang2022bevpoolv2,
  title   = {BEVPoolv2: A Cutting-edge Implementation of BEVDet Toward Deployment},
  author  = {Huang, Junjie and Huang, Guan},
  journal = {arXiv preprint arXiv:2211.17111},
  year    = {2022}
}
@article{huang2021bevdet,
  title   = {BEVDet: High-performance Multi-camera 3D Object Detection in Bird-Eye-View},
  author  = {Huang, Junjie and Huang, Guan and Zhu, Zheng and Ye, Yun and Du, Dalong},
  journal = {arXiv preprint arXiv:2112.11790},
  year    = {2021}
}
```

## References

- [1] Huang and Huang, "BEVPoolv2: A Cutting-edge Implementation of BEVDet Toward Deployment", arXiv:2211.17111, 2022.
- [2] Huang et al., "BEVDet: High-performance Multi-camera 3D Object Detection in Bird-Eye-View", arXiv:2112.11790, 2021.
- [3] nuScenes: <https://www.nuscenes.org/nuscenes>

## Legal Notice

_The nuScenes dataset is released publicly for non-commercial use under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International Public License. Additional Terms of Use can be found at
<https://www.nuscenes.org/terms-of-use>. To inquire about a commercial license please contact
<nuscenes@motional.com>._
