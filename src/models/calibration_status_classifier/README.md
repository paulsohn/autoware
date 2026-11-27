---
license: apache-2.0
pipeline_tag: image-classification
tags:
  - autoware
  - ros2
  - autonomous-driving
  - lidar
  - camera
  - calibration
  - sensing
  - tensorrt
  - onnx
---

# Calibration Status Classifier for Autoware (`calibration_status_classifier`)

Deep learning model for real-time LiDAR-camera calibration validation, used by the
[`autoware_calibration_status_classifier`](https://github.com/autowarefoundation/autoware_universe/tree/main/sensing/autoware_calibration_status_classifier)
node in [Autoware](https://github.com/autowarefoundation/autoware).

The node projects the LiDAR point cloud onto the (undistorted) camera image, feeds the resulting
5-channel tensor to this network, and classifies whether the LiDAR-camera extrinsic calibration is
still valid. The model is exported as ONNX and runs with TensorRT inside Autoware; Autoware builds
the TensorRT engine from the ONNX file on first launch.

## Model overview

| | |
| --- | --- |
| Task | Binary classification of LiDAR-camera calibration status (calibrated vs. miscalibrated) |
| Input | 5-channel normalized image tensor: RGB + projected LiDAR depth + intensity |
| Output | Calibration status with a miscalibration confidence score |
| Input resolution | Dynamic; TensorRT optimization profile height `[1080, 1860, 2160]`, width `[1920, 2880, 3840]` (min, opt, max) |
| Runtime | TensorRT (FP16 by default, FP32 selectable) via the `autoware_calibration_status_classifier` ROS 2 node |
| Format | ONNX (Autoware builds the TensorRT engine locally on first launch) |
| License | Apache-2.0 |

Pre-processing runs CUDA-accelerated in the node, not in the ONNX graph: image undistortion,
projection of 3D LiDAR points onto the 2D image plane (adding depth and intensity channels), and
morphological dilation of the projected points.

## Files

| File | Description |
| --- | --- |
| `calibration_status_classifier.onnx` | Calibration status classification network |
| `ml_package_calibration_status_classifier.param.yaml` | Model parameters (max projection depth, dilation kernel size, input resolution profile) |
| `deploy_metadata.yaml` | Deployment metadata recording the artifact version of this repository |

> **TensorRT engines are not distributed here.** TensorRT engines are specific to the GPU
> architecture and TensorRT version they are built on and are not portable, so Autoware builds them
> locally from the ONNX file on first launch (or via `build_only:=true`).

## Inputs and outputs (as used by the node)

**Inputs**

- LiDAR point cloud topics (`sensor_msgs/msg/PointCloud2`, XYZIRC format with intensity)
- Camera image topics (`sensor_msgs/msg/Image`, BGR8 format)
- Camera info topics (`sensor_msgs/msg/CameraInfo`, intrinsics and distortion coefficients)

**Outputs**

- `/diagnostics` (`diagnostic_msgs/msg/DiagnosticArray`): calibration status per camera-LiDAR pair
- Preview image topics (`sensor_msgs/msg/Image`): visualization with projected points

The node supports three runtime modes: `MANUAL` (on-demand validation via a `std_srvs/srv/Trigger`
service), `PERIODIC` (validation at a configurable interval), and `ACTIVE` (continuous monitoring
with synchronized sensor data). Inference is gated by configurable prerequisite checks on vehicle
linear velocity, angular velocity, and the number of detected objects in the scene.

## Usage in Autoware

The node loads these artifacts from `~/autoware_data/ml_models/calibration_status_classifier/` and
launches with:

```bash
ros2 launch autoware_calibration_status_classifier calibration_status_classifier.launch.xml
```

Add `build_only:=true` to build the TensorRT engine from the ONNX as a one-off pre-task, and
`model_path:=...` to point at a non-default artifact directory. See the
[package README](https://github.com/autowarefoundation/autoware_universe/tree/main/sensing/autoware_calibration_status_classifier)
for the full parameter reference.

## Training

The model was trained with the AWML CalibrationStatusClassification project:
<https://github.com/tier4/AWML/tree/main/projects/CalibrationStatusClassification>.
It was trained on calibrated and miscalibrated LiDAR-camera data; the exact training datasets and
schedules are not publicly documented beyond the AWML project page.

## Provenance and versioning

| Hugging Face tag | Original source |
| --- | --- |
| `v2.0` | `https://awf.ml.dev.web.auto/sensing/models/calibration_status_classifier/v2/` |

This repository migrates the artifacts previously hosted at the URL above. Consumers should pin a
tag (`--revision v2.0`) rather than tracking `main`.

## Limitations

- Input images must be BGR8 (8-bit per channel); input point clouds must contain intensity
  (XYZIRC format).
- The classifier judges calibration from the projected overlay, so scenes must satisfy the node's
  prerequisite checks (motion and object-count gates) for reliable results.
- Sensor setups that differ substantially from the training configuration may require retraining
  via the AWML project.

## References

- AWML CalibrationStatusClassification: <https://github.com/tier4/AWML/tree/main/projects/CalibrationStatusClassification>
- Consuming package: <https://github.com/autowarefoundation/autoware_universe/tree/main/sensing/autoware_calibration_status_classifier>
