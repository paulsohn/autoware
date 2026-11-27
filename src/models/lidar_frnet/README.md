---
license: apache-2.0
pipeline_tag: image-segmentation
tags:
  - autoware
  - ros2
  - autonomous-driving
  - lidar
  - point-cloud
  - semantic-segmentation
  - frnet
  - tensorrt
  - onnx
---

# FRNet for Autoware (`lidar_frnet`)

LiDAR semantic segmentation models used by the
[`autoware_lidar_frnet`](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_lidar_frnet)
node in [Autoware](https://github.com/autowarefoundation/autoware).

The models follow the **FRNet** (Frustum-Range Network) [1] architecture and perform per-point 3D semantic
segmentation on LiDAR data (x, y, z, intensity). They are exported as ONNX and run with TensorRT inside
Autoware; the node builds the TensorRT engine from the ONNX file on first launch.

## Model overview

| | |
| --- | --- |
| Task | 3D semantic segmentation of LiDAR point clouds (per-point class ID and probability) |
| Architecture | FRNet (Frustum-Range Network) |
| Classes | 27 semantic classes (see list below) |
| Runtime | TensorRT (FP16 by default) via the `autoware_lidar_frnet` ROS 2 node |
| Format | ONNX (Autoware builds the TensorRT engine locally on first launch) |
| License | Apache-2.0 |

Segmented classes (as defined in the `ml_package_frnet_*.param.yaml` files):

`drivable_surface`, `other_flat_surface`, `sidewalk`, `manmade`, `vegetation`, `car`, `bus`,
`emergency_vehicle`, `train`, `truck`, `tractor_unit`, `semi_trailer`, `construction_vehicle`, `forklift`,
`kart`, `motorcycle`, `bicycle`, `pedestrian`, `personal_mobility`, `animal`, `pushable_pullable`,
`traffic_cone`, `debris`, `stroller`, `other_stuff`, `noise+ghost_point`, `unknown`

Pre-processing (range-image projection, frustum construction) and post-processing (per-point argmax,
probability-based filtering, visualization coloring) run in the node, not in the ONNX graph.

## Variants in this repository

FRNet uses range images, so each variant is tied to the field of view and resolution of a specific LiDAR
sensor:

| Variant | Sensor | FOV up / down [deg] | Frustum (W x H) | Interpolation (W x H) | Points (min / opt / max) |
| --- | --- | --- | --- | --- | --- |
| `frnet_ot128` | HESAI OT128 | `15.0` / `-25.0` | `1024 x 128` | `4096 x 128` | `5000 / 80000 / 160000` |
| `frnet_qt128` | HESAI QT128 | `52.6` / `-52.6` | `256 x 128` | `1024 x 128` | `5000 / 60000 / 120000` |

Both variants share the same 27-class label set and color palette. The point count ranges define the dynamic
TensorRT optimization profile for the input point cloud.

## Files

| File | Description |
| --- | --- |
| `frnet_ot128.onnx` | FRNet network, HESAI OT128 variant |
| `frnet_qt128.onnx` | FRNet network, HESAI QT128 variant |
| `ml_package_frnet_ot128.param.yaml` | Model parameters for `frnet_ot128` (FOV, frustum/interpolation size, classes, palette, profile ranges) |
| `ml_package_frnet_qt128.param.yaml` | Model parameters for `frnet_qt128` (FOV, frustum/interpolation size, classes, palette, profile ranges) |
| `deploy_metadata.yaml` | Deployment metadata recording the artifact version of this repository |

> **TensorRT engines are not distributed here.** TensorRT engines are specific to the GPU architecture and
> TensorRT version they are built on and are not portable, so Autoware builds them locally from the ONNX files
> on first launch (or via `build_only:=true`).

## Inputs and outputs (as used by the node)

**Input**: `~/input/pointcloud` (`sensor_msgs/msg/PointCloud2`). The node operates on the x, y, z and
intensity fields and auto-detects the input layout on the first received message; supported layouts are
`XYZIRCAEDT`, `XYZIRADRT`, `XYZIRC` and `XYZI`.

**Outputs**:

| Topic | Type | Description |
| --- | --- | --- |
| `~/output/pointcloud/segmentation` | `sensor_msgs/msg/PointCloud2` | XYZ cloud with class ID and probability fields |
| `~/output/pointcloud/visualization` | `sensor_msgs/msg/PointCloud2` | XYZ cloud with RGB field (palette colors) |
| `~/output/pointcloud/filtered` | `sensor_msgs/msg/PointCloud2` | Cloud with the configured filter classes removed (default: `drivable_surface`), in the requested `filter.output_format` |

The node also publishes processing time and latency debug topics and `/diagnostics`.

## Usage in Autoware

Autoware's setup tooling (the Ansible `artifacts` role) downloads these artifacts to
`~/autoware_data/ml_models/lidar_frnet/`. The node then loads `frnet_<sensor_model>.onnx` together with the
matching `ml_package_frnet_<sensor_model>.param.yaml` from `model_path`:

```bash
ros2 launch autoware_lidar_frnet lidar_frnet.launch.xml \
  sensor_model:=ot128 \
  model_path:=$HOME/autoware_data/ml_models/lidar_frnet
```

`sensor_model` accepts `ot128` (HESAI OT128) or `qt128` (HESAI QT128). Add `build_only:=true` to build the
TensorRT engine from the ONNX as a one-off pre-task. See the
[package README](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_lidar_frnet)
for the full parameter reference.

## Training

The models were trained with AWML (<https://github.com/tier4/AWML>) on the T4Dataset, using approximately
16,000 frames (4,000 frames x 4 surrounding sensors), as documented in the package README.

- FRNet project page: <https://xiangxu-0103.github.io/FRNet>
- FRNet paper: arXiv:2312.04484

## Provenance and versioning

| HF tag | Original artifact source |
| --- | --- |
| `v2.0` | `https://awf.ml.dev.web.auto/perception/models/frnet/v2/` |

The model and parameter files (`frnet_*.onnx`, `ml_package_frnet_*.param.yaml`) byte-match the artifacts
previously hosted at the source path above; `deploy_metadata.yaml` is generated for this repository and has no
upstream counterpart. Consumers should pin a tag (`--revision v2.0`), never `main`.

## Limitations

- FRNet uses range images, so the input point cloud must be referenced to the sensor's origin.
- Each variant is tuned to its sensor's FOV and resolution; differences in your sensor's specifications
  (FOV, horizontal/vertical resolution) may affect performance.
- Only the two sensor variants above are provided; other LiDAR models require retraining or fine-tuning.

## Citation

```bibtex
@article{xu2025frnet,
  title   = {FRNet: Frustum-Range Networks for Scalable LiDAR Segmentation},
  author  = {Xu, Xiang and Kong, Lingdong and Shuai, Hui and Liu, Qingshan},
  journal = {IEEE Transactions on Image Processing},
  volume  = {34},
  pages   = {2173--2186},
  year    = {2025},
  doi     = {10.1109/TIP.2025.3550011}
}
```

## References

- [1] X. Xu, L. Kong, H. Shuai and Q. Liu, "FRNet: Frustum-Range Networks for Scalable LiDAR Segmentation", IEEE Transactions on Image Processing, vol. 34, pp. 2173-2186, 2025. arXiv:2312.04484.
- [2] AWML: <https://github.com/tier4/AWML>
- [3] FRNet project page: <https://xiangxu-0103.github.io/FRNet>
