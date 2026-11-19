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
For a matched Lyrical target-pipeline comparison, divide each candidate latency
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
The three-process Universe CPU crop now passes with native Agnocast and
Cyclone. The CPU readers take a const reference to the vector in the generated
PointCloud2 buffer. This avoids calling a virtual method through a foreign
process's backend. The unmodified ROS libraries and kernel module were used.

| Lyrical Universe pipeline | Verified frames | Median (ms) | p95 (ms) | Child CPU (s/run) | Largest child peak RSS (MiB) |
|---|---:|---:|---:|---:|---:|
| Native Agnocast | 180/180 | 0.375 | 0.632 | 0.604 | 134.3 |
| Cyclone DDS | 180/180 | 1.232 | 1.565 | 0.685 | 130.0 |

The 1.20× candidate ceilings on this laptop are 0.450 ms median and
0.758 ms p95 (rounded). Latencies pool the last 50 of 60 frames per process
group. Resource columns give medians across three complete runs. Each group's
CPU total includes child user and system time through teardown. Both modes
requested SensorDataQoS; native Agnocast reports it ignores best-effort
reliability. The crop uses the production Agnocast CIE or the ROS single-threaded
executor respectively. A matched buffer-backend comparison remains open.
[Samples and resource usage](../../src/universe_crop_benchmark/results-lyrical-cpu-view/)
