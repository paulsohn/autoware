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
  - transfusion
  - tensorrt
  - onnx
---

# TransFusion for Autoware (`lidar_transfusion`)

3D object detection model for LiDAR point clouds, used by the
[`autoware_lidar_transfusion`](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_lidar_transfusion)
node in [Autoware](https://github.com/autowarefoundation/autoware).

The model follows the **TransFusion** [1] architecture and, in this deployment, operates on LiDAR data only
(x, y, z, intensity). It is exported as ONNX so it can be deployed across hardware; Autoware builds the
TensorRT engine from the ONNX file on first launch.

## Model overview

| | |
| --- | --- |
| Task | 3D object detection (oriented bounding boxes) from a LiDAR point cloud |
| Architecture | TransFusion (LiDAR-only input in this deployment) |
| Variant | `t4xx1_90m` |
| Detected classes | `CAR`, `TRUCK`, `BUS`, `BICYCLE`, `PEDESTRIAN` |
| Runtime | TensorRT (FP16 by default) via the `autoware_lidar_transfusion` ROS 2 node |
| Format | ONNX (Autoware builds the TensorRT engine locally on first launch) |
| License | Apache-2.0 |

Model parameters from `transfusion_ml_package.param.yaml`:

| Parameter | Value |
| --- | --- |
| Point cloud range [m] | `[-92.16, -92.16, -3.0, 92.16, 92.16, 7.0]` |
| Voxel size (x, y, z) [m] | `0.24, 0.24, 10.0` |
| Voxel count (min, opt, max) | `5000, 30000, 60000` |
| Number of proposals | `500` |

Pre-processing (point cloud densification, voxelization) and post-processing (circle NMS, IoU-based NMS, yaw
normalization, score thresholding) run in the node, not in the ONNX graph.

## Files

| File | Description |
| --- | --- |
| `transfusion.onnx` | TransFusion network, variant `t4xx1_90m` |
| `transfusion_ml_package.param.yaml` | Model parameters (classes, ranges, voxel settings, proposals) |
| `detection_class_remapper.param.yaml` | Area-based class remapping (e.g. oversized cars to truck/trailer) |
| `deploy_metadata.yaml` | Deployment metadata recording the artifact version of this repository |

> **TensorRT engines are not distributed here.** TensorRT engines are specific to the GPU architecture and
> TensorRT version they are built on and are not portable, so Autoware builds them locally from the ONNX file
> on first launch (or via `build_only:=true`).

## Inputs and outputs (as used by the node)

**Input**: `~/input/pointcloud` (`sensor_msgs/msg/PointCloud2`). The node operates on raw cloud data and
requires at least the following fields (additional fields are allowed):

```python
[
  sensor_msgs.msg.PointField(name='x', offset=0, datatype=7, count=1),
  sensor_msgs.msg.PointField(name='y', offset=4, datatype=7, count=1),
  sensor_msgs.msg.PointField(name='z', offset=8, datatype=7, count=1),
  sensor_msgs.msg.PointField(name='intensity', offset=12, datatype=2, count=1)
]
```

**Output**: `~/output/objects` (`autoware_perception_msgs/msg/DetectedObjects`): oriented 3D boxes with class
and score. The node also publishes debug topics for cyclic time, pipeline latency, and per-stage processing
times.

## Usage in Autoware

The node expects these artifacts in `~/autoware_data/ml_models/lidar_transfusion/` (the launch file's default
`model_path`) and launches with:

```bash
ros2 launch autoware_lidar_transfusion lidar_transfusion.launch.xml
```

Add `build_only:=true` to build the TensorRT engine from the ONNX as a one-off pre-task, and
`log_level:=debug` for verbose logging. See the
[package README](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_lidar_transfusion)
for the full parameter reference.

## Training

The model was trained with [MMDetection3D](https://github.com/open-mmlab/mmdetection3d). According to the
consuming package README, the TransFusion model of this family was trained on TIER IV's internal database
(approximately 11k LiDAR frames) for 50 epochs; that statement is documented for the `t4xx1_90m/v2` release,
and no separate training notes are published for `v2.1`. The training configuration is not publicly
documented.

Related implementations:

- CUDA-TransFusion: <https://github.com/wep21/CUDA-TransFusion>
- OpenPCDet: <https://github.com/open-mmlab/OpenPCDet>

## Provenance

| | |
| --- | --- |
| Original source | `https://awf.ml.dev.web.auto/perception/models/transfusion/t4xx1_90m/v2.1/` |
| Source version path | `transfusion/t4xx1_90m/v2.1` |
| Hugging Face tag | `v2.1` |

Consumers should pin the `v2.1` revision when downloading, not `main`.

## Limitations

- Only the five classes above are detected. Other road users fall outside the label set.
- The input point cloud must provide at least the `x`, `y`, `z` (float32) and `intensity` (uint8) fields in
  the layout shown above.
- Detections are limited to the configured point cloud range (about 92 m around the sensor origin).
- Trained on TIER IV internal data; accuracy on a different LiDAR setup (mounting position, beam count,
  concatenated clouds) can drop without fine-tuning.

## Citation

```bibtex
@article{bai2022transfusion,
  title   = {TransFusion: Robust LiDAR-Camera Fusion for 3D Object Detection with Transformers},
  author  = {Bai, Xuyang and Hu, Zeyu and Zhu, Xinge and Huang, Qingqiu and Chen, Yilun and Fu, Hongbo and Tai, Chiew-Lan},
  journal = {arXiv preprint arXiv:2203.11496},
  year    = {2022}
}
```

## References

- [1] Bai et al., "TransFusion: Robust LiDAR-Camera Fusion for 3D Object Detection with Transformers", arXiv:2203.11496, 2022.
- [2] CUDA-TransFusion: <https://github.com/wep21/CUDA-TransFusion>
- [3] MMDetection3D: <https://github.com/open-mmlab/mmdetection3d>
- [4] OpenPCDet: <https://github.com/open-mmlab/OpenPCDet>
