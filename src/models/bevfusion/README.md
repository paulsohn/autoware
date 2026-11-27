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
  - 3d-object-detection
  - bevfusion
  - tensorrt
  - onnx
---

# BEVFusion for Autoware (`bevfusion`)

3D object detection models for LiDAR-only and camera-LiDAR fusion, used by the
[`autoware_bevfusion`](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_bevfusion)
node in [Autoware](https://github.com/autowarefoundation/autoware).

The models follow the **BEVFusion** [1] architecture (MIT Han Lab) and run with TensorRT inside Autoware.
They are exported as ONNX so they can be deployed across hardware; Autoware builds the TensorRT engines from
the ONNX files on first launch. The sparse convolution backend corresponds to
[spconv](https://github.com/traveller59/spconv), executed at inference time through the
[spconv_cpp](https://github.com/autowarefoundation/spconv_cpp) TensorRT plugins that Autoware installs
automatically in its setup script.

## Model overview

| | |
| --- | --- |
| Task | 3D object detection (oriented bounding boxes) from a LiDAR point cloud, optionally fused with multi-camera images |
| Architecture | BEVFusion (sparse-convolution LiDAR encoder, optional camera-to-BEV branch, transformer detection head) |
| Variant family | `t4base_120m` |
| Detected classes | `CAR`, `TRUCK`, `BUS`, `BICYCLE`, `PEDESTRIAN` |
| Runtime | TensorRT (FP16 by default) via the `autoware_bevfusion` ROS 2 node, with `autoware_tensorrt_plugins` for sparse convolution |
| Format | ONNX (Autoware builds the TensorRT engines locally on first launch) |
| License | Apache-2.0 |

## Variants in this repository

| Variant | Modality | ONNX files used | Cameras |
| --- | --- | --- | --- |
| `bevfusion_lidar` | LiDAR only | `bevfusion_lidar.onnx` | 0 |
| `bevfusion_camera_lidar` | Camera-LiDAR fusion | `bevfusion_camera_lidar.onnx` + `bevfusion_image_backbone.onnx` | 6 (raw 1440x1080, ROI 576x384) |

Common model parameters for both variants (from the `ml_package_*.param.yaml` files): point cloud range
`[-122.4, -122.4, -3.0, 122.4, 122.4, 5.0]` m (roughly 120 m detection radius, matching the `t4base_120m`
family name), voxel size `[0.17, 0.17, 0.2]` m, `max_points_per_voxel: 10`, `num_proposals: 500`,
`out_size_factor: 8`, `use_intensity: false`.

Pre-processing (voxelization, multi-frame densification, optional image undistortion) and post-processing
(circle NMS, IoU NMS, yaw normalization, distance-based score thresholding, area-based class remapping) run in
the node, not in the ONNX graphs.

## Files

| File | Description |
| --- | --- |
| `bevfusion_lidar.onnx` | Main network, `bevfusion_lidar` variant |
| `bevfusion_camera_lidar.onnx` | Main network, `bevfusion_camera_lidar` variant |
| `bevfusion_image_backbone.onnx` | Image backbone, used by `bevfusion_camera_lidar` |
| `ml_package_bevfusion_lidar.param.yaml` | Model parameters for `bevfusion_lidar` |
| `ml_package_bevfusion_camera_lidar.param.yaml` | Model parameters for `bevfusion_camera_lidar` |
| `detection_class_remapper.param.yaml` | Area-based class remapping (e.g. large car to truck/trailer) |
| `deploy_metadata.yaml` | Deployment metadata recording the artifact version of this repository |

> **TensorRT engines are not distributed here.** TensorRT engines are specific to the GPU architecture and
> TensorRT version they are built on and are not portable, so Autoware builds them locally from the ONNX files
> on first launch (or via `build_only:=true`).

## Inputs and outputs (as used by the node)

**Inputs**

| Topic | Type | Description |
| --- | --- | --- |
| `~/input/pointcloud` | `sensor_msgs/msg/PointCloud2` | Input point cloud, `PointXYZIRC` layout as defined in `autoware_point_types` |
| `~/input/image*` | `sensor_msgs/msg/Image` | Input images (RGB8), camera-lidar variant only |
| `~/input/camera_info*` | `sensor_msgs/msg/CameraInfo` | Camera intrinsics, camera-lidar variant only |

**Output** is `~/output/objects` (`autoware_perception_msgs/msg/DetectedObjects`): oriented 3D boxes with class
and score. The node also publishes per-stage processing-time debug topics.

## Usage in Autoware

The node expects these artifacts in `$HOME/autoware_data/ml_models/bevfusion/` and launches with, e.g.:

```bash
ros2 launch autoware_bevfusion bevfusion.launch.xml \
  model_name:=bevfusion_lidar \
  model_path:=$HOME/autoware_data/ml_models/bevfusion
```

`model_name` selects the variant (`bevfusion_lidar`, the default, or `bevfusion_camera_lidar`). Add
`build_only:=true` to build the TensorRT engines from the ONNX files as a one-off pre-task.
See the [package README](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_bevfusion)
for the full parameter reference.

## Training

The models were trained on TIER IV's internal database; the training data is not publicly available. The
consuming package README documents training on roughly 35k LiDAR frames for 30 epochs. Version-specific
training details for this `t4base_120m/v2` release are not publicly documented.

Related training and inference resources:

- Sparse convolution: <https://github.com/traveller59/spconv>
- Sparse convolution TensorRT plugins used by Autoware: <https://github.com/autowarefoundation/spconv_cpp>

## Limitations

- Trained on TIER IV's internal sensor configurations; accuracy on a different LiDAR or camera setup (mounting
  positions, beam count, camera count and resolution) can drop without fine-tuning.
- Only the five classes above are detected. Other road users fall outside the label set.
- The input point cloud must follow the `PointXYZIRC` layout defined in `autoware_point_types`.
- The consuming package notes that full integration of the camera-LiDAR fusion mode into the Autoware pipeline
  is still future work; the model can be employed without changes as a LiDAR-only detector.

## Provenance

| | |
| --- | --- |
| Original source | `https://awf.ml.dev.web.auto/perception/models/bevfusion/t4base_120m/v2/` |
| Hugging Face tag | `v2.0` |

## Citation

```bibtex
@inproceedings{liu2023bevfusion,
  title     = {BEVFusion: Multi-Task Multi-Sensor Fusion with Unified Bird's-Eye View Representation},
  author    = {Liu, Zhijian and Tang, Haotian and Amini, Alexander and Yang, Xinyu and Mao, Huizi and Rus, Daniela and Han, Song},
  booktitle = {IEEE International Conference on Robotics and Automation (ICRA)},
  year      = {2023}
}
```

## References

- [1] Liu et al., "BEVFusion: Multi-Task Multi-Sensor Fusion with Unified Bird's-Eye View Representation", ICRA 2023, arXiv:2205.13542.
- [2] spconv, sparse convolution library: <https://github.com/traveller59/spconv>
- [3] spconv_cpp, Autoware's sparse convolution TensorRT plugin implementation: <https://github.com/autowarefoundation/spconv_cpp>
