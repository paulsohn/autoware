# Clean colcon transport experiment

This branch starts at a6e04807 with the CUDA and shared-memory backends already
imported as squashed subtrees. The original experiment and its historical
reports remain on experiment/rosidl-buffer-colcon at 0693fc37. The fresh
branch is experiment/rosidl-buffer-clean. The [journal](JOURNAL.md) records
observed failures, upstream alignment and validation. All timings on this
branch come from fresh runs. The [ordinary-output samples](../../src/crop_pipeline_benchmark/results/),
[two-edge samples and diagnostics](../../src/crop_pipeline_benchmark/two-edge-results/),
and [Universe samples](../../src/universe_crop_benchmark/results/) use the same
summary script as the archived experiment.

## Reproduce on the reference laptop

Use a GPU-capable Lyrical environment with the dependencies in the
[historical setup](https://github.com/paulsohn/autoware/blob/0693fc37/src/buffer_pointcloud_probe/JOURNAL.md)
or set LYRICAL_ROOT to an exported Lyrical root with those system packages.
The namespace runner sources only the ROS underlay and this checkout's
install. It mounts the host CUDA toolkit, NVIDIA devices and driver libraries.
Start in a clean worktree with no build products. For the first stage run:

```bash
e=experiments/buffer
"$e/run-lyrical.sh" bash -ec '
  colcon build --base-paths src/core/autoware_cmake src/core/autoware_utils/autoware_utils_rclcpp --packages-up-to autoware_utils_rclcpp --executor sequential --cmake-args -DBUILD_TESTING=OFF
  source install/local_setup.bash
  bash experiments/buffer/test-wrapper.sh'
"$e/run-lyrical.sh" bash "$e/build-lyrical.sh"
BUFFER_RUNTIME_ONLY=1 "$e/run-lyrical.sh" bash "$e/cyclone-matrix.sh"
BUFFER_RUNTIME_ONLY=1 "$e/run-lyrical.sh" bash "$e/profile-lyrical.sh"
"$e/run-lyrical.sh" bash "$e/test-stop.sh"
"$e/run-limited.sh" env -i HOME="$HOME" PATH=/usr/local/bin:/usr/bin:/bin LANG=C.UTF-8 bash "$e/build-jazzy.sh"
```

The timing harness opens each CSV exclusively. For an independent repetition
use a scratch worktree and move its checked-in result directories aside before
starting. The Jazzy run requires a matching Agnocast 2.4.1 kernel device and
heap hook, Clang 18, and the host ROS development dependencies. Colcon and Make
run one job at a time. Each group has a 1,200 MiB host-memory cap and zero extra
swap. The runner samples host MemAvailable every 250 ms, requires 3 GiB to
start, and stops its scope below 2 GiB. For binary-only checks BUFFER_RUNTIME_ONLY=1 lowers the cap to 512 MiB and admission to 2,816 MiB, retaining the 2 GiB stop threshold. Run one group at a time.

## What is measured

The Core source, filter and sink are three processes using standard PointCloud2.
The source publishes a 512 KiB fixture at 10 Hz and the CPU crop filter keeps
256 KiB. Time spans publish to sink callback entry. The sink checks every point
and its order. The first variant uses mapped or staged input with ordinary CPU
output. The second uses the disabled wrapper and allocates exact-sized output
in the input memory domain. A CPU crop with two CUDA edges stages input and
output across the host/device boundary. The two-edge sink checks its requested
backend, retained first output and, for shared memory, the remote descriptor.
The ordinary CPU controls run on Cyclone and Zenoh. Separate Cyclone native
shmem and iceoryx cases keep the production message type and SensorDataQoS.

Universe uses the existing Jazzy crop plus its own synthetic PointXYZIRC input
and verifier. Its Cyclone and Agnocast runs use the same binaries, with the
production Agnocast CIE on the enabled path and a single-threaded executor
on the rclcpp path. All timing cases use 60 frames in three fresh process
groups. Per case, drop the first ten in each group and report median and
nearest-rank p95 over 150 samples.

## Migration decision

Build and validate the existing Universe Agnocast path on Lyrical first. Use
the laptop as the reference host and report additional hosts independently.
Include the ordinary Zenoh CPU control alongside Cyclone DDS and native Agnocast
in pipeline comparison tables. Label explicit shared-memory and CUDA buffer
backends separately. For a matched Lyrical target-pipeline comparison, divide each candidate latency
statistic by the corresponding Agnocast statistic. Both the median ratio and
the p95 ratio must be at most 1.20. Report the absolute values and the ratios.
Report other metrics directly alongside Agnocast, including CPU usage, memory
and frame loss, without assigning additional thresholds.

Keep the separate Universe production-pipeline measurement. The current Jazzy
Universe and Lyrical Core results differ in filter, input schema and executor.
They provide context only. For the decision, match the filter workload, input,
QoS, process topology and sampling protocol, and record executor settings.
Where executor behavior differs, measure its contribution separately before
attributing a difference to transport.

Executor isolation, mixed and remote readers, bridging and restart behavior
remain open migration decisions. The previous [migration roadmap](https://github.com/paulsohn/autoware/blob/0693fc37/src/buffer_pointcloud_probe/MIGRATION.md)
describes the conditions for revisiting the default backend and Bazel adoption.

### Lyrical reference status

On the laptop, Agnocast 2.4.1 passed its Lyrical library and heap-hook suites
with the existing kernel module. The enabled wrapper also passed its C++ suite.
The three-process Universe CPU crop now passes with native Agnocast, Cyclone DDS and
Zenoh. The CPU readers take a const reference to the vector in the generated
PointCloud2 buffer. This avoids calling a virtual method through a foreign
process's backend. The unmodified ROS libraries and kernel module were used.

The matched matrix uses one build for all rows. Every row delivered and
verified 180/180 frames. Latencies pool the last 50 of 60 frames per round.
Ratios use this run's native Agnocast measurements.

| Transport | Payload on both hops | Median ms | p95 ms | Median / Agnocast | p95 / Agnocast | CPU s/run | Peak RSS MiB |
|---|---|---:|---:|---:|---:|---:|---:|
| Native Agnocast | CPU | 0.353 | 0.626 | 1.00× | 1.00× | 0.560 | 139.8 |
| Cyclone DDS | CPU | 1.180 | 1.461 | 3.34× | 2.33× | 0.682 | 135.2 |
| Zenoh | CPU | 1.006 | 1.389 | 2.85× | 2.22× | 1.429 | 290.0 |
| Zenoh | POSIX shared memory | 0.751 | 0.930 | 2.12× | 1.48× | 1.371 | 288.7 |
| Zenoh | CUDA device memory | 1.594 | 5.807 | 4.51× | 9.27× | 1.531 | 288.7 |

The candidate ceilings were 0.424 ms median and 0.752 ms p95. Both explicit
buffer candidates exceeded both ceilings. CUDA round medians were 1.444,
4.655 and 4.533 ms. The CPU crop stages its CUDA input and output. This run
did not isolate the source of the round-to-round variation.

CPU time sums child user and system time through teardown, including the
Zenoh router. RSS is the largest individual child's peak. Both figures are
medians over three groups. Native Agnocast uses CIE and reports that it
ignores the requested best-effort reliability. The ROS modes use a
single-threaded crop executor. These measurements compare the deployed paths.

Cyclone plus native shmem remains a caveated cell: the earlier Core control
delivered 0/60 with loopback and 0/60 with iceoryx PSMX. Cyclone plus CUDA
has no verified native descriptor path or timing in this matrix. Native
Agnocast carries the CPU reference; using a foreign polymorphic buffer
backend directly in an Agnocast-shared message remains unvalidated.
[Universe matrix samples](../../src/universe_crop_benchmark/results-lyrical-buffer-matrix/)
and [Cyclone compatibility evidence](../../src/crop_pipeline_benchmark/two-edge-results/cyclone-matrix.tsv).

### Crop executor comparison on Lyrical

Each row delivered 180/180 verified frames. The suffix `-cie` selects the
Agnocast library's mixed-capable callback-isolated executor on the ordinary
ROS crop. The native reference uses the Agnocast-only CIE. Other rows use
the ordinary single-threaded crop executor. Endpoint executors stay fixed
within each pair. Native Agnocast and Cyclone use CPU storage. Plain Zenoh
uses CPU storage, zenoh-shmem uses POSIX shared memory, and zenoh-cuda stages
device buffers through the CPU crop.

| Mode | Median ms | p95 ms | Median / Agnocast | p95 / Agnocast | CPU s/run | Peak RSS MiB |
|---|---:|---:|---:|---:|---:|---:|
| agnocast | 0.354 | 0.574 | 1.00× | 1.00× | 0.557 | 139.8 |
| cyclone | 1.224 | 1.570 | 3.46× | 2.74× | 0.739 | 134.4 |
| cyclone-cie | 1.303 | 1.577 | 3.68× | 2.75× | 0.757 | 129.4 |
| zenoh | 1.034 | 1.493 | 2.92× | 2.60× | 1.454 | 290.5 |
| zenoh-cie | 1.059 | 1.628 | 2.99× | 2.84× | 1.516 | 303.4 |
| zenoh-shmem | 0.739 | 1.040 | 2.09× | 1.81× | 1.432 | 288.2 |
| zenoh-shmem-cie | 0.774 | 1.003 | 2.19× | 1.75× | 1.378 | 287.2 |
| zenoh-cuda | 1.164 | 4.853 | 3.29× | 8.46× | 1.600 | 287.4 |
| zenoh-cuda-cie | 3.531 | 5.007 | 9.97× | 8.73× | 1.647 | 304.5 |

With default scheduling, shared memory plus CIE remains above both 1.20×
ceilings. Three rounds give limited resolution for the small differences
between the two shared-memory rows. A competing callback group and a deliberate
priority policy are needed to evaluate isolation under load. The standalone
AWF CIE remains untested. [Measurements](../../src/universe_crop_benchmark/results-lyrical-cie/)

### Agnocast-backed Buffer ownership result

A receiver-side reservation fixed the delayed-import failure from the
[original experiment](JOURNAL.md#agnocast-backed-buffer-ownership-on-lyrical).
Before Zenoh queues a descriptor, the plugin asks the destination to take the
matching Agnocast loan. Three Lyrical runs verified both queued payloads after
native history advanced. ROS PointCloud2, rclcpp, Zenoh and the Agnocast kernel
module required no patches. Agnocast and POSIX-only Buffer participants also
exchanged verified data in both directions through CPU fallback with the
Agnocast bridge disabled.

The probe uses one ordered writer, one subscription per receiving process,
a private shared socket directory and bounded reservations. Concurrent streams,
endpoint restarts, prompt release of a final dropped descriptor, and the
Universe latency/CPU cost of the per-peer handshake remain open.
[Reproduction and limits](JOURNAL.md#reserve-the-destination-loan-before-descriptor-publication)
and [results](../../src/agnocast_buffer_probe/reservation-results.txt).

### Bounded CUDA LiDAR edge milestone

Compare one real Universe CUDA concatenation transform between two GPU
stages. Use the composed Autoware blackboard path as the control and the
Lyrical CUDA Buffer backend over Zenoh as the candidate. Verify device
payloads, a same-host process boundary and CPU fan-out. Record the limits
of the comparison, then resume the CPU synchronizer output-publication
experiment. Broader CUDA integration in rmw_agnocast requires a measured
need plus a delayed-take/pool-reuse qualification.

Completed on the reference laptop. The GPU subscriber verified 240/240
transformed frames and the simultaneous CPU observer verified 48/48 across
the two orders. Both composed fast paths preserved the device address.
Split-process GPU delivery worked over Zenoh. Timing varied enough to leave
the performance ranking open. The next Universe experiment is synchronizer
output publication from retained host inputs.
[Reproduction and decision](JOURNAL.md#2026-11-21--one-universe-cuda-transform-edge-on-the-laptop)
and [per-round measurements](../../src/universe_gpu_edge_benchmark/results.csv).

The CPU track has since tested publication of the completed synchronizer
output. Retained input with ordinary publication remains the laptop baseline.
[Publisher-loan comparison and next diagnostic](../rmw-agnocast/README.md#transfer-the-universe-synchronizer-output-through-a-publisher-loan).

The launch-driven three-LiDAR CPU pipeline passed with three crop components
in the standard ROS container and a separate synchronizer and consumer.
Both original-source and retained-input builds ran with runtime rmw_agnocast.
[Build, launch and matched comparison](../rmw-agnocast/README.md#launch-three-universe-crops-into-the-three-lidar-synchronizer).
