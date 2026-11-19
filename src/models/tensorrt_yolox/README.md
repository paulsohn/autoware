---
license: apache-2.0
pipeline_tag: object-detection
tags:
  - autoware
  - ros2
  - autonomous-driving
  - camera
  - 2d-object-detection
  - yolox
  - semantic-segmentation
  - traffic-light
  - tensorrt
  - onnx
  - int8
---

# YOLOX for Autoware (`tensorrt_yolox`)

2D object detection (and optional semantic segmentation) models for camera images, used by the
[`autoware_tensorrt_yolox`](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_tensorrt_yolox)
node in [Autoware](https://github.com/autowarefoundation/autoware).

The models follow the **YOLOX** [1] architecture. They are exported as ONNX so they can be deployed across
hardware; Autoware builds the TensorRT engine from the ONNX file on first launch. The same node binary consumes
every model in this repository: the camera object detection pipeline uses the detection and detection+segmentation
models, and a separate node instance runs the whole-image traffic light detector that feeds the traffic light
recognition pipeline.

## Model overview

| | |
| --- | --- |
| Task | 2D object detection from a camera image; one variant additionally outputs a semantic segmentation mask, one variant detects traffic lights on the whole image |
| Architecture | YOLOX (tiny and s-based variants); the semantic segmentation variant uses a multi-header structure on a YOLOX-s base |
| Detected classes | `CAR`, `PEDESTRIAN`, `BUS`, `TRUCK`, `BICYCLE`, `MOTORCYCLE` (detection models) or `UNKNOWN`, `CAR_TRAFFIC_LIGHT`, `PEDESTRIAN_TRAFFIC_LIGHT` (traffic light detector) |
| Runtime | TensorRT (FP32 / FP16 / INT8) via the `autoware_tensorrt_yolox` ROS 2 node |
| Format | ONNX plus INT8 calibration tables (Autoware builds the TensorRT engine locally on first launch) |
| License | Apache-2.0 |

Labels listed in the model's label file that are not in the node's known set are reported as `UNKNOWN`.
`yolox-tiny.onnx` has an `EfficientNMS_TRT` module attached after the network to accelerate non-maximum
suppression; the module contains fixed `score_threshold` and `nms_threshold` values, so those node parameters
are ignored for models that include it.

## Model families in this repository

| Family | Files | Original source path | Consuming launch file |
| --- | --- | --- | --- |
| Generic detection | `yolox-tiny.onnx`, `yolox-sPlus-opt.onnx`, `yolox-sPlus-opt.EntropyV2-calibration.table`, `label.txt` | unversioned root of the `awf.ml.dev.web.auto` model store (not browsable as a directory; direct per-file URLs in Provenance) | `yolox_tiny.launch.xml` (tiny); s-Plus-opt selectable via `model_path` |
| Pseudo-finetuned detection | `yolox-sPlus-T4-960x960-pseudo-finetune.onnx`, `yolox-sPlus-T4-960x960-pseudo-finetune.EntropyV2-calibration.table` | `https://awf.ml.dev.web.auto/perception/models/object_detection_yolox_s/v1/` | `yolox_s_plus_opt.launch.xml` (detection-only option for `model_path`) |
| Detection + semantic segmentation (16 classes) | `yolox-sPlus-opt-pseudoV2-T4-960x960-T4-seg16cls.onnx`, `yolox-sPlus-opt-pseudoV2-T4-960x960-T4-seg16cls.EntropyV2-calibration.table`, `semseg_color_map.csv` | `https://awf.ml.dev.web.auto/perception/models/object_detection_semseg_yolox_s/v1/` | `yolox_s_plus_opt.launch.xml` (default `model_path`) |
| Whole-image traffic light detector | `yolox_s_car_ped_tl_detector_960_960_batch_1.onnx`, `yolox_s_car_ped_tl_detector_960_960_batch_1.EntropyV2-calibration.table`, `car_ped_tl_detector_labels.txt` | `https://awf.ml.dev.web.auto/perception/models/tl_detector_yolox_s/v1/` | `yolox_traffic_light_detector.launch.xml` (separate node instance feeding the traffic light recognition pipeline) |

Notes on the families:

- `yolox-sPlus-opt-pseudoV2-T4-960x960-T4-seg16cls` is a multi-header model based on YOLOX-s, tuned to detect
  more accurately than `yolox-tiny` at almost comparable execution speed. Besides detection it outputs a
  semantic segmentation mask used for point cloud filtering. Recommended settings: `precision:=int8`,
  `calibration_algorithm:=Entropy`, `clip_value:=6.0` (these are the defaults in `yolox_s_plus_opt.param.yaml`).
- `yolox-sPlus-T4-960x960-pseudo-finetune` is the detection-only alternative offered by the same launch file.
- The traffic light detector localizes car and pedestrian traffic lights on the full camera image; its labels
  come from `car_ped_tl_detector_labels.txt` (`BACKGROUND`, `traffic_light`, `pedestrian_traffic_light`).
- `label.txt` for the detection models contains `UNKNOWN`, `CAR`, `TRUCK`, `BUS`, `BICYCLE`, `MOTORBIKE`,
  `PEDESTRIAN`, `ANIMAL`.

The semantic segmentation mask is a gray image where each pixel holds a class index; `semseg_color_map.csv`
maps the 16 indices to names and RGB colors for visualization (others, building (spelled `buildling` in the
shipped CSV), wall, obstacle, traffic_light,
traffic_sign, person, vehicle, bike, road, sidewalk, roadPaint, curbstone, crosswalk_others, vegetation, sky).

## Files

| File | Description |
| --- | --- |
| `yolox-tiny.onnx` | YOLOX-tiny detection model with `EfficientNMS_TRT` attached |
| `yolox-sPlus-opt.onnx` | YOLOX-s based optimized detection model |
| `yolox-sPlus-opt.EntropyV2-calibration.table` | INT8 calibration table for `yolox-sPlus-opt` |
| `yolox-sPlus-T4-960x960-pseudo-finetune.onnx` | YOLOX-s based detection model, pseudo-label finetuned, 960x960 input |
| `yolox-sPlus-T4-960x960-pseudo-finetune.EntropyV2-calibration.table` | INT8 calibration table for the pseudo-finetuned model |
| `yolox-sPlus-opt-pseudoV2-T4-960x960-T4-seg16cls.onnx` | Multi-header detection + 16-class semantic segmentation model, 960x960 input |
| `yolox-sPlus-opt-pseudoV2-T4-960x960-T4-seg16cls.EntropyV2-calibration.table` | INT8 calibration table for the detection + segmentation model |
| `yolox_s_car_ped_tl_detector_960_960_batch_1.onnx` | Whole-image traffic light detector (YOLOX-s, 960x960, batch 1) |
| `yolox_s_car_ped_tl_detector_960_960_batch_1.EntropyV2-calibration.table` | INT8 calibration table for the traffic light detector |
| `label.txt` | Class labels for the detection models |
| `car_ped_tl_detector_labels.txt` | Class labels for the traffic light detector |
| `semseg_color_map.csv` | Semantic segmentation class index to name and RGB color map |
| `deploy_metadata.yaml` | Deployment metadata recording the artifact version of this repository |

> **TensorRT engines are not distributed here.** TensorRT engines are specific to the GPU architecture and
> TensorRT version they are built on and are not portable, so Autoware builds them locally from the ONNX files
> on first launch (or via `build_only:=true`). Engines are saved next to the ONNX files with an `.engine`
> extension and reused on subsequent runs; the first build typically takes 10 to 20 minutes. The
> `EntropyV2-calibration.table` files are used by the node when running with `precision:=int8`.

## Inputs and outputs (as used by the node)

**Input**: `~/in/image` (`sensor_msgs/msg/Image`), the camera image.

**Outputs**:

- `~/out/objects` (`tier4_perception_msgs/msg/DetectedObjectsWithFeature`): detected objects or traffic lights
  with 2D bounding boxes.
- `~/out/image` (`sensor_msgs/msg/Image`): input image with 2D bounding boxes drawn, for visualization.
- `~/out/mask` (`sensor_msgs/msg/Image`): semantic segmentation mask (semantic segmentation model only).
- `~/out/color_mask` (`sensor_msgs/msg/Image`): colorized segmentation mask for visualization (semantic
  segmentation model only).

## Usage in Autoware

The node reads these artifacts from `$HOME/autoware_data/ml_models/tensorrt_yolox/` by default and launches
with, e.g.:

```bash
# Detection + semantic segmentation (default), or detection-only via model_path
ros2 launch autoware_tensorrt_yolox yolox_s_plus_opt.launch.xml

# Lightweight detection
ros2 launch autoware_tensorrt_yolox yolox_tiny.launch.xml

# Whole-image traffic light detection
ros2 launch autoware_tensorrt_yolox yolox_traffic_light_detector.launch.xml
```

Add `build_only:=true` to build the TensorRT engine from the ONNX as a one-off pre-task. See the
[package README](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_tensorrt_yolox)
for the full parameter reference, including precision selection (`fp32`, `fp16`, `int8`) and INT8 calibration
options.

## Training

The models are based on the official YOLOX implementation. The training datasets, schedules, and evaluation
metrics of the T4 finetuned variants are not publicly documented.

- YOLOX (architecture and training framework): <https://github.com/Megvii-BaseDetection/YOLOX>
- trt-yoloXP (TIER IV YOLOX extensions): <https://github.com/tier4/trt-yoloXP>
- yolox_onnx_modifier (embeds `EfficientNMS_TRT` into exported ONNX): <https://github.com/wep21/yolox_onnx_modifier>

The consuming package documents how to export custom YOLOX models to ONNX (plain or with `EfficientNMS_TRT`)
in its README.

## Provenance

Original hosting before migration to Hugging Face (tag `v1.0` bundles all four families):

| Family | Original source |
| --- | --- |
| Generic detection | unversioned root of the model store; the root itself is not browsable (directory listing is disabled), so the direct per-file URLs are: <https://awf.ml.dev.web.auto/perception/models/yolox-tiny.onnx>, <https://awf.ml.dev.web.auto/perception/models/yolox-sPlus-opt.onnx>, <https://awf.ml.dev.web.auto/perception/models/yolox-sPlus-opt.EntropyV2-calibration.table>, <https://awf.ml.dev.web.auto/perception/models/label.txt> |
| Pseudo-finetuned detection | `https://awf.ml.dev.web.auto/perception/models/object_detection_yolox_s/v1/` |
| Detection + semantic segmentation | `https://awf.ml.dev.web.auto/perception/models/object_detection_semseg_yolox_s/v1/` |
| Traffic light detector | `https://awf.ml.dev.web.auto/perception/models/tl_detector_yolox_s/v1/` |

## Limitations

- Detection models output only the classes listed above; other road users are reported as `UNKNOWN`.
- `label.txt` is incompatible with models that output COCO labels (e.g. models from the official YOLOX
  repository); those need their own label file.
- Fixed `score_threshold` and `nms_threshold` are baked into models with `EfficientNMS_TRT`
  (e.g. `yolox-tiny.onnx`); the node parameters of the same names have no effect for them.
- Training data details of the T4 finetuned variants are not publicly documented, so accuracy on sensor setups
  and environments different from those used for training is not characterized here.

## Citation

```bibtex
@article{yolox2021,
  title   = {YOLOX: Exceeding YOLO Series in 2021},
  author  = {Ge, Zheng and Liu, Songtao and Wang, Feng and Li, Zeming and Sun, Jian},
  journal = {arXiv preprint arXiv:2107.08430},
  year    = {2021}
}
```

## References

- [1] Ge et al., "YOLOX: Exceeding YOLO Series in 2021", arXiv:2107.08430, 2021.
- Megvii-BaseDetection/YOLOX: <https://github.com/Megvii-BaseDetection/YOLOX>
- tier4/trt-yoloXP: <https://github.com/tier4/trt-yoloXP>
- wep21/yolox_onnx_modifier: <https://github.com/wep21/yolox_onnx_modifier>
