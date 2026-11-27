---
license: apache-2.0
pipeline_tag: object-detection
tags:
  - autoware
  - ros2
  - autonomous-driving
  - lidar
  - point-cloud
  - 3d-object-detection
  - semantic-segmentation
  - point-transformer
  - tensorrt
  - onnx
---

# Point Transformer V3 for Autoware (`ptv3`)

Dual-task 3D perception models for LiDAR point clouds, used by the
[`autoware_ptv3`](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_ptv3)
node in [Autoware](https://github.com/autowarefoundation/autoware).

The models follow the **Point Transformer V3 (PTv3)** [1] architecture: a shared sparse-convolution point
transformer encoder feeding two task heads, one for 3D object detection and one for 3D semantic segmentation.
The sparse convolution backend corresponds to [spconv](https://github.com/traveller59/spconv), which Autoware
consumes through [spconv_cpp](https://github.com/autowarefoundation/spconv_cpp). The models are exported as
ONNX so they can be deployed across hardware; Autoware builds the TensorRT engines from the ONNX files on
first launch.

## Model overview

| | |
| --- | --- |
| Task | Joint 3D object detection (oriented bounding boxes) and 3D semantic segmentation from a LiDAR point cloud |
| Architecture | Point Transformer V3 encoder (sparse convolutions) with separate detection and segmentation heads |
| Detection classes | `CAR`, `TRUCK`, `BUS`, `MOTORCYCLE`, `BICYCLE`, `PEDESTRIAN`, `ANIMAL`, `BARRIER`, `TRAFFIC_CONE`, `DEBRIS` |
| Segmentation classes | `car`, `truck`, `bus`, `motorcycle`, `bicycle`, `pedestrian`, `animal`, `barrier`, `traffic_cone`, `debris`, `driveable_flat`, `non_driveable_flat`, `vegetation`, `manmade`, `vertical_thin`, `noise` |
| Point cloud range [m] | `[-122.88, -122.88, -3.0, 122.88, 122.88, 5.0]` |
| Voxel size (x, y, z) [m] | `0.12, 0.12, 0.12` |
| Runtime | TensorRT (FP16 by default) via the `autoware_ptv3` ROS 2 node, with sparse-conv plugins from `autoware_tensorrt_plugins` |
| Format | ONNX (Autoware builds the TensorRT engines locally on first launch) |
| License | Apache-2.0 |

The network is split into three ONNX sub-models, matching how the node consumes them:

1. **Encoder** (`ptv3_encoder.onnx`): shared PTv3 backbone with encoder channels
   `[32, 64, 128, 256, 512]`, serialization orders `z` and `z-trans`, and pooling strides `[2, 2, 2, 2]`.
2. **Detection head** (`ptv3_det3d_head.onnx`): predicts up to 500 oriented 3D boxes with velocity
   (`has_twist: true`) over a BEV grid with `bbox_voxel_size: [0.96, 0.96, 8.0]`.
3. **Segmentation head** (`ptv3_seg3d_head.onnx`): per-point classification over the 16 segmentation
   classes listed above.

Each head can be enabled or disabled independently at launch time (`use_det3d_head`, `use_seg3d_head`).
Pre-processing (multi-frame densification, voxelization) and post-processing (IoU NMS, yaw normalization, distance-based score
thresholding, class remapping by area, point filtering) run in the node, not in the ONNX graphs.

## Files

| File | Description |
| --- | --- |
| `ptv3_encoder.onnx` | Shared PTv3 encoder |
| `ptv3_det3d_head.onnx` | 3D object detection head |
| `ptv3_seg3d_head.onnx` | 3D semantic segmentation head |
| `ml_package_ptv3_encoder.param.yaml` | Encoder model parameters (range, voxel size, max points per voxel, channels) |
| `ml_package_ptv3_det3d_head.param.yaml` | Detection head model parameters (classes, proposals, BEV voxel size) |
| `ml_package_ptv3_seg3d_head.param.yaml` | Segmentation head model parameters (class names, visualization palette) |
| `deploy_metadata.yaml` | Deployment metadata (records the model version) |

> **TensorRT engines are not distributed here.** TensorRT engines are specific to the GPU architecture and
> TensorRT version they are built on and are not portable, so Autoware builds them locally from the ONNX files
> on first launch (or via `build_only:=true`).

## Inputs and outputs (as used by the node)

**Input**: `~/input/pointcloud` (`sensor_msgs/msg/PointCloud2`), by default remapped to
`/sensing/lidar/concatenated/pointcloud`. The node detects the input point format automatically on the first
received message and supports `XYZIRCAEDT` (10 fields), `XYZIRADRT` (9 fields), `XYZIRC` (6 fields), and
`XYZI` (4 fields).

**Outputs**:

| Topic | Type | Description |
| --- | --- | --- |
| `~/output/objects` | `autoware_perception_msgs/msg/DetectedObjects` | Detected 3D objects after score filtering and IoU NMS |
| `~/output/pointcloud/segmentation` | `sensor_msgs/msg/PointCloud2` | XYZ cloud with class ID and probability fields |
| `~/output/pointcloud/visualization` | `sensor_msgs/msg/PointCloud2` | XYZ cloud with RGB field (colored by class palette) |
| `~/output/pointcloud/filtered` | `sensor_msgs/msg/PointCloud2` | Input cloud with configured classes filtered out (default: `driveable_flat`, `non_driveable_flat`, `noise`) |

The node also publishes processing-time and latency debug topics under `debug/`.

## Usage in Autoware

Autoware's setup (the Ansible artifacts role) downloads these artifacts to `~/autoware_data/ml_models/ptv3/`,
and the node launches with, e.g.:

```bash
ros2 launch autoware_ptv3 ptv3.launch.xml \
  model_path:=$HOME/autoware_data/ml_models/ptv3
```

Add `build_only:=true` to build the TensorRT engines from the ONNX files as a one-off pre-task. The
`ml_package_ptv3_*.param.yaml` files in this repository are loaded from the model directory at launch, while
node parameters (precision, workspace sizes, filtering, score thresholds) come from the package's
`config/ptv3.param.yaml`. See the
[package README](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_ptv3)
for the full parameter reference.

## Training

The model was trained by TIER IV on the internal T4Dataset with
[Autoware-ML](https://github.com/tier4/autoware-ml) ([documentation](https://tier4.github.io/autoware-ml/)).

- Sparse convolution backend: <https://github.com/traveller59/spconv>
- C++ sparse convolution implementation used by Autoware: <https://github.com/autowarefoundation/spconv_cpp>
- Point Transformer V3 paper: arXiv:2312.10035

## Provenance

| | |
| --- | --- |
| Source | TIER IV model release, trained and exported with Autoware-ML |
| Source version | `v0.1.1` |
| This repository | `AutowareFoundation/ptv3`, tag `v0.1.1` |

## Limitations

- Trained on the T4Dataset sensor configuration; accuracy on a different LiDAR setup (mounting position, beam
  count, concatenated clouds) can drop without fine-tuning.
- Only the classes listed above are detected or segmented. Other road users fall outside the label set.
- Evaluation metrics for this model are not publicly documented.

## Citation

```bibtex
@inproceedings{wu2024ptv3,
  title     = {Point Transformer V3: Simpler, Faster, Stronger},
  author    = {Wu, Xiaoyang and Jiang, Li and Wang, Peng-Shuai and Liu, Zhijian and Liu, Xihui and Qiao, Yu and Ouyang, Wanli and He, Tong and Zhao, Hengshuang},
  booktitle = {CVPR},
  year      = {2024}
}
```

## References

- [1] Wu et al., "Point Transformer V3: Simpler, Faster, Stronger", CVPR 2024, arXiv:2312.10035.
- spconv: <https://github.com/traveller59/spconv>
- spconv_cpp: <https://github.com/autowarefoundation/spconv_cpp>
