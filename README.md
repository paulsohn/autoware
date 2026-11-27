---
license: apache-2.0
pipeline_tag: image-classification
tags:
  - autoware
  - ros2
  - autonomous-driving
  - camera
  - traffic-light
  - classification
  - mobilenetv2
  - tensorrt
  - onnx
---

# Traffic Light Classifier for Autoware (`traffic_light_classifier`)

Traffic light state classification models for cropped camera images, used by the
[`autoware_traffic_light_classifier`](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_traffic_light_classifier)
node in [Autoware](https://github.com/autowarefoundation/autoware).

The repository contains **MobileNetV2** [1] CNN classifiers for vehicular (car) and pedestrian traffic lights,
plus a **lamp recognizer** model (`classifier_type: 2`) that detects and classifies individual lamps inside a
traffic light ROI. All models are exported as ONNX and run with TensorRT inside Autoware; Autoware builds the
TensorRT engine from the ONNX file on first launch.

## Model overview

| | |
| --- | --- |
| Task | Traffic light state classification from cropped camera ROIs |
| Architecture | MobileNetV2 image classifiers (car and pedestrian variants); anchor-based per-lamp recognizer (`comlops`) |
| Runtime | TensorRT (FP16 by default; fp32/int8 selectable) via the `autoware_traffic_light_classifier` ROS 2 node |
| Format | ONNX (Autoware builds the TensorRT engine locally on first launch) |
| Input normalization | mean `[123.675, 116.28, 103.53]`, std `[58.395, 57.12, 57.375]` (RGB) |
| License | Apache-2.0 |

Label strings follow the Vienna Convention style unified road-sign naming used by
`tier4_perception_msgs::msg::TrafficLightElement`: one lamp is `color-shape` and multiple lamps are
comma-separated (e.g. `red,right`).

## Variants in this repository

| Variant | Model files | Label file | Classes |
| --- | --- | --- | --- |
| Car (vehicular) classifier | `traffic_light_classifier_mobilenetv2_batch_{1,4,6}.onnx` | `lamp_labels.txt` | `green`, `left,red`, `left,red,straight`, `red`, `red,right`, `red,straight`, `unknown`, `yellow`, `red,up_left`, `red,right,straight`, `red,up_right` |
| Pedestrian classifier | `ped_traffic_light_classifier_mobilenetv2_batch_{1,4,6}.onnx` | `lamp_labels_ped.txt` | `red`, `green`, `unknown` |
| Lamp recognizer (`comlops`) | `traffic_light_lamp_recognizer_comlops.onnx` | `lamp_labels.txt` or `lamp_labels_ped.txt` | per-lamp color (3) and shape/type (6) outputs |

Each MobileNetV2 classifier is exported with three fixed batch sizes (1, 4, 6). The package launch files load
the **batch_6** exports by default; the batch_1 and batch_4 exports are alternative exports of the same model.
MobileNetV2 classifiers use a 224 x 224 input, per the package README.

The lamp recognizer is selected with `classifier_type: 2` and decodes an anchor-based output head using the
parameters in `lamp_recognizer_ml.param.yaml` (3 anchors, 16 channels per anchor, 3 color and 6 shape/type
classes, YOLO-style center decode). Both the car and the pedestrian launch profile point to the same
`traffic_light_lamp_recognizer_comlops.onnx` file when this mode is chosen.

## Which launch profile loads which file

| Launch file | `classifier_type` | Model loaded | Label file |
| --- | --- | --- | --- |
| `car_traffic_light_classifier.launch.xml` | `1` (CNN, default) | `traffic_light_classifier_mobilenetv2_batch_6.onnx` | `lamp_labels.txt` |
| `car_traffic_light_classifier.launch.xml` | `2` (LampRecognizer) | `traffic_light_lamp_recognizer_comlops.onnx` | `lamp_labels.txt` |
| `pedestrian_traffic_light_classifier.launch.xml` | `1` (CNN, default) | `ped_traffic_light_classifier_mobilenetv2_batch_6.onnx` | `lamp_labels_ped.txt` |
| `pedestrian_traffic_light_classifier.launch.xml` | `2` (LampRecognizer) | `traffic_light_lamp_recognizer_comlops.onnx` | `lamp_labels_ped.txt` |

`classifier_type: 0` selects the rule-based HSV filter, which loads no model from this repository.

## Files

| File | Description |
| --- | --- |
| `traffic_light_classifier_mobilenetv2_batch_1.onnx` | Car classifier, MobileNetV2, batch size 1 |
| `traffic_light_classifier_mobilenetv2_batch_4.onnx` | Car classifier, MobileNetV2, batch size 4 |
| `traffic_light_classifier_mobilenetv2_batch_6.onnx` | Car classifier, MobileNetV2, batch size 6 (launch default) |
| `ped_traffic_light_classifier_mobilenetv2_batch_1.onnx` | Pedestrian classifier, MobileNetV2, batch size 1 |
| `ped_traffic_light_classifier_mobilenetv2_batch_4.onnx` | Pedestrian classifier, MobileNetV2, batch size 4 |
| `ped_traffic_light_classifier_mobilenetv2_batch_6.onnx` | Pedestrian classifier, MobileNetV2, batch size 6 (launch default) |
| `traffic_light_lamp_recognizer_comlops.onnx` | Lamp recognizer model for `classifier_type: 2` |
| `lamp_labels.txt` | Car (vehicular) label list |
| `lamp_labels_ped.txt` | Pedestrian label list |
| `lamp_recognizer_ml.param.yaml` | Anchor/decode parameters for the lamp recognizer |
| `deploy_metadata.yaml` | Deployment metadata recording the artifact version of this repository |

> **TensorRT engines are not distributed here.** TensorRT engines are specific to the GPU architecture and
> TensorRT version they are built on and are not portable, so Autoware builds them locally from the ONNX files
> on first launch (or via `build_only:=true`).

## Inputs and outputs (as used by the node)

**Inputs**

| Topic | Type | Description |
| --- | --- | --- |
| `~/input/image` | `sensor_msgs::msg::Image` | Camera image |
| `~/input/rois` | `tier4_perception_msgs::msg::TrafficLightRoiArray` | Traffic light ROIs to classify |

**Outputs**

| Topic | Type | Description |
| --- | --- | --- |
| `~/output/traffic_signals` | `tier4_perception_msgs::msg::TrafficLightArray` | Classified signals |
| `~/output/debug/image` | `sensor_msgs::msg::Image` | Debug visualization |

If an ROI has zero height or width, the output becomes `UNKNOWN`/`CIRCLE` with confidence `0.0`. ROIs judged
as backlit (over/under exposure thresholds, defaults `0.85` and `-0.83`) are overwritten with `UNKNOWN` and
confidence `0.0`.

## Usage in Autoware

The node expects these artifacts under `~/autoware_data/ml_models/traffic_light_classifier/` and launches with,
e.g.:

```bash
# Vehicular traffic lights (loads traffic_light_classifier_mobilenetv2_batch_6.onnx)
ros2 launch autoware_traffic_light_classifier car_traffic_light_classifier.launch.xml

# Pedestrian traffic lights (loads ped_traffic_light_classifier_mobilenetv2_batch_6.onnx)
ros2 launch autoware_traffic_light_classifier pedestrian_traffic_light_classifier.launch.xml
```

Pass `classifier_type:=2` to either launch file to use the lamp recognizer model instead, or
`classifier_type:=0` for the rule-based HSV classifier. Add `build_only:=true` to build the TensorRT engine
from the ONNX as a one-off pre-task. See the
[package README](https://github.com/autowarefoundation/autoware_universe/tree/main/perception/autoware_traffic_light_classifier)
for the full parameter reference and the training/deployment guide.

## Training

The MobileNetV2 classifiers were trained with [mmpretrain](https://github.com/open-mmlab/mmpretrain) and
exported to ONNX with [mmdeploy](https://github.com/open-mmlab/mmdeploy); the package README documents the
full pipeline for retraining on custom data.

As reported in the package README:

- The vehicular classifier was fine-tuned on 83400 TIER IV internal images of Japanese traffic lights
  (58600 train, 14800 eval, 10000 test); the MobileNet-v2 model reaches 99.81% test accuracy at 224 x 224.
- The pedestrian classifier was fine-tuned on 21199 TIER IV internal images of Japanese traffic lights
  (17860 train, 2114 eval, 1225 test); the MobileNet-v2 model reaches 99.10% test accuracy at 224 x 224.

A 1045-image sample dataset (red/green/yellow) for the training tutorial is available at
<https://autoware-files.s3.us-west-2.amazonaws.com/dataset/traffic_light_sample_dataset.tar.gz>.

The training data and procedure for the lamp recognizer model (`traffic_light_lamp_recognizer_comlops.onnx`)
are not publicly documented.

## Limitations

- The classifiers were fine-tuned on Japanese traffic lights; accuracy on other regions' signal designs may
  drop without retraining.
- Only the label sets listed above are recognized; other signal states fall outside the label set.
- Over/under-exposed ROIs are intentionally reported as `UNKNOWN` with confidence `0.0`.

## Provenance and versioning

| | |
| --- | --- |
| Original source | `https://awf.ml.dev.web.auto/perception/models/traffic_light_classifier/v4` |
| This repository | `AutowareFoundation/traffic_light_classifier`, tag `v4.0` |

Consumers should pin a tag (`--revision v4.0`) rather than tracking `main`.

## Citation

```bibtex
@inproceedings{sandler2018mobilenetv2,
  title     = {MobileNetV2: Inverted Residuals and Linear Bottlenecks},
  author    = {Sandler, Mark and Howard, Andrew and Zhu, Menglong and Zhmoginov, Andrey and Chen, Liang-Chieh},
  booktitle = {CVPR},
  year      = {2018}
}
```

## References

- [1] Sandler et al., "MobileNetV2: Inverted Residuals and Linear Bottlenecks", CVPR 2018.
- mmpretrain (training framework) - <https://github.com/open-mmlab/mmpretrain>
- mmdeploy (ONNX export) - <https://github.com/open-mmlab/mmdeploy>
