---
license: apache-2.0
pipeline_tag: image-segmentation
tags:
  - autoware
  - ros2
  - autonomous-driving
  - camera
  - road-segmentation
  - localization
  - yabloc
  - tensorflow
  - onnx
---

# Road Segmentation for Autoware YabLoc (`yabloc_pose_initializer`)

Road semantic segmentation model used by the
[`yabloc_pose_initializer`](https://github.com/autowarefoundation/autoware_universe/tree/main/localization/yabloc/yabloc_pose_initializer)
package in [Autoware](https://github.com/autowarefoundation/autoware).

The `camera_pose_initializer` node estimates the vehicle's initial pose from a camera image at the request of
AD API. It segments the road surface in the undistorted camera image with this model and matches the result
against the Lanelet2 vector map to score initial pose candidates.

The model is Intel Open Model Zoo's **road-segmentation-adas-0001**, converted to a TensorFlow frozen graph and
to ONNX via the PINTO model zoo (entry 136). The node loads the frozen graph on CPU through OpenCV DNN; the
ONNX export is shipped alongside it as the migration path off OpenCV's legacy TensorFlow importer (see
[Why ONNX is included](#why-onnx-is-included)).

## Repository versions

| Tag | Contents | Purpose |
| --- | --- | --- |
| `v0.1` | The complete upstream PINTO model zoo export, all 104 files | Archival snapshot of exactly what Autoware's previous hosting served, kept so the provenance chain survives the decommissioning of the old S3 bucket |
| `v1.0` | `saved_model/model_float32.pb` and `saved_model/model_float32.onnx` | What Autoware installs. `main` tracks this tag |

Consumers must pin a revision explicitly, never `main`. Autoware's ansible artifacts role pins `v0.1` while the
artifact hosting migration lands, so that move delivers exactly the same files as the old hosting did, and switches
to `v1.0` in a separate follow-up (see [autowarefoundation/autoware#7223](https://github.com/autowarefoundation/autoware/issues/7223)).

`v0.1` additionally contains the TFLite variants (float32, float16, dynamic-range and full integer quantized),
the TensorFlow SavedModel with its checkpoint, the TensorFlow.js graph models, the Intel OpenVINO IR pairs
(FP32 and FP16) and the Myriad blob, and two TF-TRT converted SavedModels with their prebuilt TensorRT engines.
None of them is loaded by Autoware. The TF-TRT engines in particular are 2021-vintage TensorRT 8.0 plans that
cannot deserialize on a current stack: Autoware builds TensorRT engines locally on first launch and never ships
them. They are archived in `v0.1` for completeness only.

## Model overview

| | |
| --- | --- |
| Task | Road semantic segmentation of a camera image, used for camera-based initial pose estimation |
| Origin | Intel Open Model Zoo `road-segmentation-adas-0001`, converted by the PINTO model zoo |
| Runtime | OpenCV DNN (`cv::dnn::readNet`), OpenCV backend, CPU target |
| Formats | TensorFlow frozen graph float32 (`model_float32.pb`), ONNX opset 11 (`model_float32.onnx`) |
| Network input | 512 x 896 RGB image, float32, scale 1.0, no mean subtraction. The frozen graph presents it as a `1 x 3 x 512 x 896` NCHW blob through OpenCV; the ONNX graph boundary is NHWC, `1 x 512 x 896 x 3` |
| Network output | 4-channel softmax score map (background, road, curb, marking), NCHW for the frozen graph and NHWC for the ONNX |
| License | Apache-2.0 (Intel Open Model Zoo) |

For the class definitions and architecture details of the segmentation network, see the upstream
[Intel Open Model Zoo model page](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/road-segmentation-adas-0001).

## Files

Contents of `v1.0`:

| File | Description |
| --- | --- |
| `saved_model/model_float32.pb` | TensorFlow frozen graph, float32; the file the node loads today |
| `saved_model/model_float32.onnx` | The same network exported to ONNX opset 11 by tf2onnx; not loaded today, see below |
| `deploy_metadata.yaml` | Deployment metadata recording the artifact version of this repository |

The directory layout (`saved_model/model_float32.pb`) is preserved exactly as the package's launch file expects
it.

## Why ONNX is included

The frozen graph and the ONNX file are the same network with the same weights, but they do not behave the same
on current OpenCV releases.

Running the node's own call sequence (`cv::dnn::readNet`, OpenCV backend, CPU target, `blobFromImage` at
896 x 512, `forward`) against `model_float32.pb`:

| OpenCV | Softmax normalized over | Result |
| --- | --- | --- |
| 4.6.0, 4.8.1 | channel axis | correct, the 4 class scores sum to 1.0 per pixel |
| 4.9.0, 4.10.0 | width axis | wrong, class scores sum to 0.0059 and the output mean is exactly 1/896 |

On 4.9 and newer, OpenCV's TensorFlow importer applies the terminal softmax along the wrong axis. The output
shape and the layer name are unchanged and no error is raised, so the failure is silent: at the node's default
score threshold of 0.5 no pixel is ever selected. `model_float32.onnx` produces correct output on all four
versions.

Autoware currently builds against OpenCV 4.5.4 (Ubuntu 22.04) and 4.6.0 (Ubuntu 24.04), which are unaffected.
Ubuntu 26.04 LTS ships OpenCV 4.10.0. The ONNX file is therefore distributed so the consuming node can move to
`cv::dnn::readNetFromONNX`, ONNX Runtime or TensorRT without needing the deprecated hosting or a reconversion.
Note that the ONNX graph is NHWC at its boundary, so the node's blob construction and output conversion need to
be adapted when it switches.

## Inputs and outputs (as used by the node)

**Subscriptions**

| Topic | Type | Description |
| --- | --- | --- |
| `~/input/camera_info` | `sensor_msgs/msg/CameraInfo` | undistorted camera info |
| `~/input/image_raw` | `sensor_msgs/msg/Image` | undistorted camera image |
| `~/input/vector_map` | `autoware_map_msgs/msg/LaneletMapBin` | vector map |

**Publications**

| Topic | Type | Description |
| --- | --- | --- |
| `~/debug/init_candidates` | `visualization_msgs/msg/MarkerArray` | initial pose candidates (the package README lists this topic as `output/candidates`, but the node publishes it under `debug/init_candidates`) |

**Services**

| Service | Type | Description |
| --- | --- | --- |
| `~/yabloc_align_srv` | `autoware_internal_localization_msgs/srv/PoseWithCovarianceStamped` | initial pose estimation request |

Pre-processing and post-processing run in the node: the image is resized to 896 x 512 and converted to a float32
RGB blob; the 4-channel output score map is resized back to the image resolution, the first (background) channel
is dropped, and the remaining three channels are thresholded into a binary mask image used for map matching.

The node's only ROS parameter besides `model_path` is `angle_resolution` (default 30, the number of divisions of
the 1 sigma angle range).

## Usage in Autoware

Autoware downloads this artifact to `~/autoware_data/ml_models/yabloc_pose_initializer/` during environment
setup (the ansible artifacts role). To fetch it manually:

```bash
hf download AutowareFoundation/yabloc_pose_initializer --revision v1.0 \
  --local-dir ~/autoware_data/ml_models/yabloc_pose_initializer
```

The package's launch file resolves the model at:

```text
$HOME/autoware_data/ml_models/yabloc_pose_initializer/saved_model/model_float32.pb
```

and passes it to the node as the `model_path` parameter (overridable via the `model_path` launch argument). The
node is started as part of the YabLoc localization stack; see the
[package README](https://github.com/autowarefoundation/autoware_universe/tree/main/localization/yabloc/yabloc_pose_initializer)
for details. If the model is missing, initialization still completes, but accuracy may be compromised.

## Training

This model was not trained by the Autoware project; it is redistributed as-is from upstream:

- Original model: Intel Open Model Zoo `road-segmentation-adas-0001`
  (<https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/road-segmentation-adas-0001>),
  licensed under Apache License 2.0.
- Conversion to TensorFlow and ONNX: PINTO model zoo entry 136
  (<https://github.com/PINTO0309/PINTO_model_zoo/tree/main/136_road-segmentation-adas-0001>); the conversion
  scripts are released under the MIT license.

Training datasets, schedules, and metrics are not documented in the Autoware sources; refer to the Intel Open
Model Zoo model page for upstream details.

## Provenance and versioning

| | |
| --- | --- |
| Original source | `https://autoware-files.s3.us-west-2.amazonaws.com/models/yabloc/136_road-segmentation-adas-0001/resources.tar.gz` (unversioned tarball) |
| Upstream mirror | `https://s3.ap-northeast-2.wasabisys.com/pinto-model-zoo/136_road-segmentation-adas-0001/resources.tar.gz` (PINTO model zoo entry 136) |
| Source `resources.tar.gz` sha256 | `1f660e15f95074bade32b1f80dbf618e9cee1f0b9f76d3f4671cb9be7f56eb3a` (the archive itself is not redistributed here; `v0.1` holds its extracted contents) |
| `saved_model/model_float32.pb` sha256 | `c4e373552f4efb91592ed99f49afc6df179f8a95174ceaddd1ab35692f435b47` |
| `saved_model/model_float32.onnx` sha256 | `931f80db4faa9eb37fafaaec35cdd6c7498a0b8afb45e335a535da028d27e50a` |

Every file in `v0.1` is byte-identical to the corresponding member of that tarball.

## Limitations

- Inference runs on CPU via OpenCV DNN; the node does not use a GPU for this model.
- The network operates at a fixed 896 x 512 input resolution; images are resized by the node.
- The model was trained upstream by Intel, not on Autoware-specific data; segmentation quality on cameras or
  scenes that differ from the upstream training domain is not characterized here.
- Pose initialization quality depends on the vector map and the undistorted camera input; a missing or poorly
  matching segmentation degrades the initial pose accuracy.
- The frozen graph is affected by the OpenCV importer regression described above on OpenCV 4.9 and newer.

## References

- Intel Open Model Zoo, road-segmentation-adas-0001:
  <https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/road-segmentation-adas-0001>
- PINTO model zoo, entry 136: <https://github.com/PINTO0309/PINTO_model_zoo/tree/main/136_road-segmentation-adas-0001>
- Consuming package:
  <https://github.com/autowarefoundation/autoware_universe/tree/main/localization/yabloc/yabloc_pose_initializer>
- Autoware: <https://github.com/autowarefoundation/autoware>

## Acknowledgment

Special thanks to [openvinotoolkit/open_model_zoo](https://github.com/openvinotoolkit/open_model_zoo) and
[PINTO0309](https://github.com/PINTO0309) for providing and converting the original model.

## Legal Notice

_The original model is distributed by Intel Open Model Zoo under the Apache License, Version 2.0. The PINTO
model zoo conversion scripts are released under the MIT license. See the upstream repositories for the full
license terms._
