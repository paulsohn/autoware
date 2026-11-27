---
license: apache-2.0
pipeline_tag: object-detection
tags:
  - autoware
  - ros2
  - autonomous-driving
  - lidar
  - camera
  - point-cloud
  - sensor-fusion
  - pointpainting
  - pointpillars
  - tensorrt
  - onnx
---

# PointPainting for Autoware (`image_projection_based_fusion`)

Camera-LiDAR fusion 3D object detection model for the `pointpainting_fusion` node of the
[`autoware_image_projection_based_fusion`](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_image_projection_based_fusion)
package in [Autoware](https://github.com/autowarefoundation/autoware).

Following the **PointPainting** [1] approach, LiDAR points are projected onto the output of an image-only 2D
object detector and the class scores are appended to each point. The painted point cloud is then fed to a
CenterPoint-style 3D detection network with a **PointPillars** [2] voxel encoder. The model is exported as ONNX
and runs with TensorRT inside Autoware; the TensorRT engine is built from the ONNX files on first launch.

## Model overview

| | |
| --- | --- |
| Task | 3D object detection (oriented bounding boxes) from a LiDAR point cloud painted with 2D detection class scores |
| Architecture | PointPainting point decoration + PointPillars-style voxel encoder + CenterPoint-style detection head |
| Detected classes | `CAR`, `TRUCK`, `BUS`, `BICYCLE`, `PEDESTRIAN` |
| Painted classes | `CAR`, `BICYCLE`, `PEDESTRIAN` (class scores appended to each point) |
| Runtime | TensorRT (FP16 by default, FP32 selectable) via the `pointpainting_fusion` ROS 2 node |
| Format | ONNX (Autoware builds the TensorRT engine locally on first launch) |
| License | Apache-2.0 |

The network is split into two ONNX sub-models, matching how the node consumes them:

1. **Voxel encoder**: `pts_voxel_encoder_pointpainting.onnx`
   - input: `input_features` -> output: `pillar_features`
2. **Backbone / neck / head**: `pts_backbone_neck_head_pointpainting.onnx`
   - input: `spatial_features` -> outputs: `heatmap`, `reg`, `height`, `dim`, `rot`, `vel`

Point painting, voxelization, and post-processing (circle NMS, IoU NMS, yaw normalization, distance-based score
thresholding) run in the node, not in the ONNX graphs.

Key model parameters (from `pointpainting_ml_package.param.yaml`): `point_feature_size: 7` (x, y, z, time-lag
and the three painted class scores), `encoder_in_feature_size: 12`, `max_voxel_size: 40000`, point cloud range
`[-121.6, -76.8, -3.0, 121.6, 76.8, 5.0]` m, voxel size `[0.32, 0.32, 8.0]` m, `downsample_factor: 1`.

## Files

| File | Description |
| --- | --- |
| `pts_voxel_encoder_pointpainting.onnx` | Voxel encoder |
| `pts_backbone_neck_head_pointpainting.onnx` | Backbone/neck/head |
| `pointpainting_ml_package.param.yaml` | Model parameters (classes, voxel grid, score thresholds) |
| `detection_class_remapper.param.yaml` | Area-based class remapping (e.g. large car -> truck/trailer) |
| `deploy_metadata.yaml` | Version marker for this artifact set (`version: v5.0`) |

> **TensorRT engines are not distributed here.** TensorRT engines are specific to the GPU architecture and
> TensorRT version they are built on and are not portable, so Autoware builds them locally from the ONNX files
> on first launch (or via `build_only:=true`).

## Inputs and outputs (as used by the node)

**Inputs**

| Topic | Type | Description |
| --- | --- | --- |
| `input/pointcloud` | `sensor_msgs/msg/PointCloud2` | LiDAR point cloud |
| `input/camera_info[N]` | `sensor_msgs/msg/CameraInfo` | Camera information to project 3D points onto image planes |
| `input/rois[N]` | `tier4_perception_msgs/msg/DetectedObjectsWithFeature` | ROIs from each camera's 2D object detector |
| `input/image_raw[N]` | `sensor_msgs/msg/Image` | Images for visualization (debug) |

One `camera_info`/`rois` pair per camera; the launch file defaults to 6 cameras (`input/rois_number`).

**Outputs**

| Topic | Type | Description |
| --- | --- | --- |
| `output/objects` | `autoware_perception_msgs/msg/DetectedObjects` | Detected objects (oriented 3D boxes with class and score) |
| `debug/painted_pointcloud` | `sensor_msgs/msg/PointCloud2` | Painted point cloud (debug) |

## Usage in Autoware

Autoware's setup (ansible artifacts role) downloads these artifacts to
`~/autoware_data/ml_models/image_projection_based_fusion/`; the node then launches with, e.g.:

```bash
ros2 launch autoware_image_projection_based_fusion pointpainting_fusion.launch.xml \
  model_name:=pointpainting \
  model_path:=$HOME/autoware_data/ml_models/image_projection_based_fusion \
  model_param_path:=$(ros2 pkg prefix autoware_image_projection_based_fusion --share)/config/pointpainting.param.yaml
```

Add `build_only:=true` to build the TensorRT engine from the ONNX as a one-off pre-task.
See the [package README](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_image_projection_based_fusion)
for the full parameter reference, including ROI/point cloud synchronization and matching strategies.

## Training

The model was trained by TIER IV. No public training configuration, dataset description, or training code is
available for this model.

## Provenance and versioning

| | |
| --- | --- |
| Original source | `https://awf.ml.dev.web.auto/perception/models/pointpainting/v5/` |
| Source version path | `pointpainting/v5` |
| Tag in this repository | `v5.0` |

## Limitations

- Only the five classes above are detected, and only car, pedestrian, and bicycle class scores are painted
  onto the points. Other road users fall outside the label set.
- Multi-frame painting is not implemented yet (single-frame painting only).
- Fusion quality depends on accurate camera-LiDAR calibration and correct per-camera timestamp offsets
  (`rois_timestamp_offsets`), as described in the package README.
- Detection accuracy depends on the upstream 2D object detector that provides the ROIs.

## Citation

```bibtex
@inproceedings{vora2020pointpainting,
  title     = {PointPainting: Sequential Fusion for 3D Object Detection},
  author    = {Vora, Sourabh and Lang, Alex H. and Helou, Bassam and Beijbom, Oscar},
  booktitle = {CVPR},
  year      = {2020},
  eprint    = {1911.10150}
}
@inproceedings{lang2019pointpillars,
  title     = {PointPillars: Fast Encoders for Object Detection from Point Clouds},
  author    = {Lang, Alex H. and Vora, Sourabh and Caesar, Holger and Zhou, Lubing and Yang, Jiong and Beijbom, Oscar},
  booktitle = {CVPR},
  year      = {2019}
}
```

## References

- [1] Vora et al., "PointPainting: Sequential Fusion for 3D Object Detection", arXiv:1911.10150, CVPR 2020.
- [2] Lang et al., "PointPillars: Fast Encoders for Object Detection from Point Clouds", CVPR 2019.
