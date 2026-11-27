---
license: apache-2.0
pipeline_tag: object-detection
tags:
  - autoware
  - ros2
  - autonomous-driving
  - lidar
  - point-cloud
  - instance-segmentation
  - apollo
  - tensorrt
  - onnx
---

# Apollo LiDAR Instance Segmentation for Autoware (`lidar_apollo_instance_segmentation`)

CNN-based LiDAR instance segmentation models, used by the
[`autoware_lidar_apollo_instance_segmentation`](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_lidar_apollo_instance_segmentation)
node in [Autoware](https://github.com/autowarefoundation/autoware).

The models originate from the Baidu [Apollo](https://github.com/ApolloAuto/apollo) 3D obstacle perception
stack (CNN segmentation). The original Apollo caffemodels were converted to ONNX for use in Autoware, and the
node builds a TensorRT engine from the ONNX file on first launch. The node rasterizes the input point cloud
into a bird's-eye-view feature map, runs the network, and clusters the per-cell predictions into labeled
obstacle instances.

## Model overview

| | |
| --- | --- |
| Task | Instance segmentation of LiDAR point clouds into labeled obstacle clusters |
| Architecture | Apollo CNN segmentation (CNNSeg) on a bird's-eye-view feature map, plus obstacle clustering in the node |
| Detected classes | Apollo meta types mapped to Autoware labels: `CAR` (small vehicle), `BUS` (big vehicle), `MOTORCYCLE` (non-motorized vehicle), `PEDESTRIAN`; unmatched clusters are `UNKNOWN` |
| Runtime | TensorRT (FP32 by default) via the `autoware_lidar_apollo_instance_segmentation` ROS 2 node |
| Format | ONNX, converted from Apollo caffemodels (Autoware builds the TensorRT engine locally on first launch) |
| License | Apache-2.0 (weights derived from Apollo, which is Apache-2.0 licensed) |

## Variants in this repository

One ONNX model per supported LiDAR sensor, selected via the `model` launch argument:

| Variant | Launch value | Feature map grid | Range [m] | Intensity feature |
| --- | --- | --- | --- | --- |
| `vlp-16` (Velodyne VLP-16) | `model_16` | 672 x 672 | 70 | yes |
| `hdl-64` (Velodyne HDL-64) | `model_64` | 672 x 672 | 70 | yes |
| `vls-128` (Velodyne VLS-128) | `model_128` (default) | 864 x 864 | 90 | no |

The per-variant parameter files (`vlp-16.param.yaml`, `hdl-64.param.yaml`, `vls-128.param.yaml`) live in the
consuming package under `config/`, not in this repository. The values above come from those files. Supported
LiDARs are the Velodyne 16, 64, and 128 beam sensors, but the package README notes that other LiDARs such as
the Velodyne 32 can also be used with good accuracy.

## Files

| File | Description |
| --- | --- |
| `vlp-16.onnx` | CNN segmentation model for Velodyne VLP-16 |
| `hdl-64.onnx` | CNN segmentation model for Velodyne HDL-64 |
| `vls-128.onnx` | CNN segmentation model for Velodyne VLS-128 |
| `deploy_metadata.yaml` | Deployment metadata recording the artifact version of this repository |

> **TensorRT engines are not distributed here.** TensorRT engines are specific to the GPU architecture and
> TensorRT version they are built on and are not portable, so Autoware builds them locally from the ONNX files
> on first launch (or via `build_only:=true`).

## Inputs and outputs (as used by the node)

**Input**: `input/pointcloud` (`sensor_msgs/msg/PointCloud2`), point cloud data from LiDAR sensors
(default remap: `/sensing/lidar/pointcloud`).

**Outputs**:

- `output/labeled_clusters` (`tier4_perception_msgs/msg/DetectedObjectsWithFeature`): detected objects with
  labeled point cloud clusters.
- `debug/instance_pointcloud` (`sensor_msgs/msg/PointCloud2`): segmented point cloud for visualization.

Pre-processing (bird's-eye-view feature map generation) and post-processing (2D clustering of the network
output into obstacle instances, score thresholding via `score_threshold`) run in the node, not in the ONNX
graphs.

## Usage in Autoware

The node expects these artifacts under `~/autoware_data/ml_models/lidar_apollo_instance_segmentation/` and
launches with, e.g.:

```bash
ros2 launch autoware_lidar_apollo_instance_segmentation lidar_apollo_instance_segmentation.launch.xml \
  model:=model_128 \
  data_path:=$HOME/autoware_data/ml_models
```

`model:=model_16`, `model_64`, or `model_128` selects `vlp-16.onnx`, `hdl-64.onnx`, or `vls-128.onnx`
respectively, together with the matching parameter file from the package's `config/` directory. Add
`build_only:=true` to build the TensorRT engine from the ONNX as a one-off pre-task. See the
[package README](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_lidar_apollo_instance_segmentation)
for the full parameter reference.

## Training

There is no training code for these models. The weights were trained by the Baidu Apollo project and released
as caffemodels, which were later converted to ONNX for Autoware. Training datasets, schedules, and metrics are
not publicly documented.

Origin and related projects:

- Apollo autonomous driving platform (original caffemodels and CNN segmentation design):
  <https://github.com/ApolloAuto/apollo>
- Initial Autoware integration of the Apollo CNN segmentation detector:
  <https://github.com/k0suke-murakami/autoware_perception>
- TensorRT wrapper library historically used by the node (current code uses `autoware_tensorrt_common`):
  <https://github.com/lewes6369/tensorRTWrapper>

The original caffemodel sources (pinned Apollo commits) are listed in the package README:

- VLP-16: `modules/perception/production/data/perception/lidar/models/cnnseg/velodyne16/deploy.caffemodel` (Apollo commit `88bfa5a`)
- HDL-64: `modules/perception/production/data/perception/lidar/models/cnnseg/velodyne64/deploy.caffemodel` (Apollo commit `88bfa5a`)
- VLS-128: `modules/perception/production/data/perception/lidar/models/cnnseg/velodyne128/deploy.caffemodel` (Apollo commit `91844c8`)

## Provenance

| | |
| --- | --- |
| Original hosting | `https://awf.ml.dev.web.auto/perception/models/lidar_apollo_instance_segmentation/` (unversioned) |
| This repository | `AutowareFoundation/lidar_apollo_instance_segmentation`, tag `v1.0` |

The `v1.0` tag corresponds to the exact file set previously served from the unversioned
`awf.ml.dev.web.auto` path.

## Limitations

- Each ONNX file is tuned for a specific Velodyne sensor (16, 64, or 128 beams); other LiDARs may work with
  good accuracy but are not the intended configuration.
- Only the classes listed above are produced; other road users are reported as `UNKNOWN`.
- No training code or training data documentation exists, so the models cannot be retrained or fine-tuned from
  public sources.

## References

- Apollo 3D Obstacle Perception description:
  <https://github.com/ApolloAuto/apollo/blob/r7.0.0/docs/specs/3d_obstacle_perception.md>
- Consuming Autoware package:
  <https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_lidar_apollo_instance_segmentation>

## Legal notice

The model weights are derived from the Baidu Apollo project, which is licensed under the Apache License 2.0.
The consuming Autoware package additionally incorporates code from the Apollo project (Apache-2.0), the
tensorRTWrapper library (MIT), and the autoware_perception integration (Apache-2.0); see the package README
for the full license texts.
