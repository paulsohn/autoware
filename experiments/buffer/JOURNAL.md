# Native buffer experiment, clean restart

## 2026-11-19 — Reproduce the wrapper failures from a6e04807

The branch starts at a6e04807d453fae51c6ad710ef69b9f99a9cc621. The previous
experiment and samples are preserved at 0693fc37 on experiment/rosidl-buffer-colcon.
The new worktree started without build, install or log directories. The Lyrical
root is the previous exported ROS image userspace, with host GPU access.
Source its /opt/ros/lyrical installation and the new /ws/install overlay only.
The existing /work/install overlay is excluded.

Run the first build on the unmodified base from /ws, with ENABLE_AGNOCAST=0
and the Cyclone RMW. Limit Make and colcon to one worker. The guarded runner
uses a 1,200 MiB cgroup with zero swap, 3 GiB minimum available host memory at
start and a 2 GiB stop threshold. The laptop has a Ryzen 9 8940HX and RTX 5070.

```bash
colcon build --base-paths src/core/autoware_cmake src/core/autoware_utils/autoware_utils_rclcpp --packages-up-to autoware_utils_rclcpp --executor sequential --cmake-args -DBUILD_TESTING=OFF
source install/local_setup.bash
colcon build --base-paths src/core/autoware_core/common/autoware_agnocast_wrapper --executor sequential --cmake-args -DBUILD_TESTING=ON
```

1. Baseline configure failed at CMakeLists.txt:47 on the removed
   ament_target_dependencies command. Applied the wrapper CMakeLists.txt diff
   from AWF core PR 1540 at 8fda07c6a16c247a435651a768df8955083a361a.
   This uses upstream's explicit target linkage for both configurations and
   std_msgs test linkage. [AWF PR 1540](https://github.com/autowarefoundation/autoware_core/pull/1540)
2. Compilation then failed on message_filters/subscriber.h. The .hpp headers
   exist on both the Lyrical and Jazzy installations. After changing the
   includes, Lyrical rejected the two-argument Subscriber template. Use its
   default node type. A regression instantiating construction and
   resubscription then exposed the raw rmw QoS argument mismatch. For rclcpp
   32 onward use the wrapper's existing profile-preserving to_qos helper.
   Exercise both entry points and the actual subscription QoS. AWF main at
   e05daabe1524b57b24d330f39470c77b4d6b6995 still uses the older API.
   PR 1257 at 7e008dfec3a290bbe251fd04566a2de5f00e1bc9 also routes the
   constructor through subscribe, but changes the downstream API and still
   forwards a raw QoS profile. The header/template/QoS compatibility fix is
   local. [AWF PR 1257](https://github.com/autowarefoundation/autoware_core/pull/1257)
3. The complete disabled-wrapper colcon suite reported 169 checks with zero
   failures, errors or skips. Run experiments/buffer/test-wrapper.sh inside
   the guarded environment. It supplies the rclcpp version header and the
   packaged GoogleTest model to one cppcheck worker. The baseline generic
   pub/sub test executor setup required no patch for the disabled build.

## Fresh Lyrical backend probes

Built the CUDA backend, POSIX shared-memory backend and their two message
packages from the pins in a6e04807. Built buffer_pointcloud_probe from its
historical source fixture. No old install prefix or samples were imported.
In order, run run-case.sh with cuda/native, cuda/mixed, shmem/native and
shmem/mixed (the backend and mode are separate arguments). All six
subscriber processes checked 12 ordered frames and re-read the first after
the last. In each mixed run an ordinary CPU reader received the same stream.
The wrapper-disabled test and probes shared a Lyrical overlay built here.

## Core target pipeline, Lyrical

Built both variants against freshly generated Autoware interfaces, compiled
each upstream crop-box suite, and passed all ten algorithm cases and the TF
integration case on Cyclone for each variant. Run build-lyrical.sh after the
wrapper prerequisite. The fixture source came from the archived experiment.
Both builds used GCC -O2 with the documented single-worker limits. Each
configuration delivered 60/60 in each of three rounds, 1,440 frames total.
Pooled figures below use 150 timed frames per configuration in milliseconds.

| Input transport | Ordinary CPU output median / p95 | Same backend on both wrapper edges median / p95 |
| --- | ---: | ---: |
| Cyclone DDS, ordinary CPU | 1.248 / 1.524 | 1.282 / 1.469 |
| Zenoh, ordinary CPU | 1.068 / 1.348 | 1.015 / 1.238 |
| Zenoh, mapped shmem | 0.876 / 1.055 | 0.754 / 0.865 |
| Zenoh, CUDA | 1.140 / 1.357 | 1.114 / 1.484 |

Recompute with src/crop_pipeline_benchmark/summarize.py and the corresponding
results or two-edge-results directory. The two variants use different output
allocation/packing. Within each column, each transport uses the same filter
binary. Host kernel 7.0.0-43-generic, NVIDIA driver 595.99.02.

## Cyclone native shmem, iceoryx and resource profiles

Tested rmw_cyclonedds_cpp 4.1.5 and Cyclone DDS 11.0.1 against the freshly
built two-edge wrapper on the same domain, QoS, ordinary PointCloud2 and
fixture. Both receivers check the requested memory domain. Each failed
delivery row has no usable timing statistic.

| Cyclone configuration | Payload backend | Result |
| --- | --- | --- |
| Loopback | CPU | 60/60 and the retained first frame passed |
| Loopback | Native rosidl shmem | 0/60, malformed 56-byte RTPS packets |
| Loopback plus iceoryx PSMX and RouDi | CPU | 0/60, sink timeout |
| Loopback plus iceoryx PSMX and RouDi | Native rosidl shmem | 0/60, publisher reported CPU-backend requirement at serdata.cpp:714 |

[Machine-readable results and fresh diagnostics](../../src/crop_pipeline_benchmark/two-edge-results/cyclone-matrix.tsv).
The earlier sender trace established that the default Cyclone RMW recorded a
serialization exception and passed an empty DATA payload downstream. A fix to
propagate the error would improve diagnostics. A CPU-copy fallback would still
leave the native allocation-sharing requirement open. A working native
descriptor path must verify receiver storage and lifetime. The iceoryx
control timeout remains undiagnosed. The documented iceoryx shared-memory
exchange requires fixed-size types and reliable QoS; the target cloud contains
variable-size fields and the target QoS is best effort.
[Historical sender trace](https://github.com/paulsohn/autoware/blob/0693fc37/src/buffer_pointcloud_probe/JOURNAL.md#why-default-cyclone-emitted-malformed-packets)
and [Cyclone iceoryx limitations](https://cyclonedds.io/docs/cyclonedds/latest/shared_memory/limitations.html).

Four separate profiles exercised the two-edge paths again. The fresh CUDA
trace recorded 60 downloads of 512 KiB and 60 uploads of 256 KiB inside the
filter. The sink performed 61 readbacks including the held first frame.
Host memory fell below the 3 GiB build admission threshold before these
runtime-only checks. Their scopes used a smaller 512 MiB cap, a 2,816 MiB
start threshold and the same 2,048 MiB stop threshold. Minimum available
host memory was 2,848 MiB during the matrix and 3,105 MiB during profiling.

## Agnocast-disabled production consumer

Built the unmodified stop filter against the fresh Lyrical wrapper overlay.
Its 27 checks passed without skips. The first standalone smoke run verified
the two velocities but timed out at SIGINT. The surviving ros2 launch
process had SIGINT in SigIgn, inherited from the background resource guard.
Reset SIGINT/SIGQUIT for the guarded command and give the test launcher a
dedicated signal group. On timeout the fixture cleans up that group.
Reran experiments/buffer/test-stop.sh. All 27 checks passed again. Both
the stock standalone launch and the component loaded via ComponentManager
zeroed 0.05 m/s, preserved 0.2 m/s and exited cleanly on SIGINT.

## Existing Universe pipeline, Jazzy Agnocast versus Cyclone

Built ten dependencies in a fresh host Jazzy overlay with Clang 18 -O1, then
built the existing Universe crop and benchmark endpoints. The package cache
resolved agnocastlib and the wrapper from this checkout's build/universe-jazzy
install tree. The kernel device and heap hook were the installed 2.4.1
versions. The base's Agnocast sources built successfully on Jazzy. Run
experiments/buffer/build-jazzy.sh under run-limited.sh as in the README.
Both transports delivered all 60 frames each round (360 total). The same
binaries ran three rounds in alternating order.

| Universe configuration | Median ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: |
| Cyclone DDS, ordinary rclcpp executor | 1.259 | 1.575 | 1.787 |
| Agnocast IPC, production CIE | 0.317 | 0.518 | 0.762 |

Agnocast measured about 75% lower median and 67% lower p95 on this graph.
It also reported that its publishers do not honor best-effort reliability
semantics. The transport and executor both change. A direct comparison with
Lyrical Core also changes the filter, schema and distribution. Minimum host
MemAvailable for the Jazzy build and runs was 3,404 MiB.

The complete timed rerun covers 1,800 verified frames across Core and
Universe. Probe, profiling, launch and expected-failure runs are additional.
Recovery reference experiment/rosidl-buffer-colcon remains at 0693fc37.

## Estimate the Core crop's share of two-edge mapped-memory latency

Launched the unmodified two-edge Zenoh shmem pipeline three times with an
extra subscriber to the existing processing_time_ms diagnostic. All 180
output frames passed. The timer begins inside the callback before filtering
and ends before output publication, after the polygon-subscriber check.
Each observer discovered the timer after startup and received 59 consecutive
samples (largest adjacent stamp interval 100.26 ms). Use the final 50 per run
for each distribution. Pooled median/p95 was 0.242/0.351 ms inside that
interval and 0.744/0.909 ms end to end. Time outside it includes both hops,
scheduling and work outside the timer; this run cannot apportion those costs.

To repeat in the guarded Lyrical environment, source build/two/install/local_setup.bash
and export RMW_IMPLEMENTATION=rmw_zenoh_cpp before starting Python. Create a
fresh output directory per group and run python3 experiments/buffer/observe-crop.py
with that directory as its argument. Selecting the RMW after importing rclpy
initially produced a successful pipeline and zero timer samples. The profiling
runs with the correct RMW used a 512 MiB cap and retained at least 4,642 MiB
of available host memory. [Pooled input samples](shmem-crop-timer.csv)

## Start the Lyrical Agnocast reference build

The latency decision requires both median and p95 to fall within 1.20 times
their corresponding matched Lyrical Agnocast measurements. Other metrics get
side-by-side comparisons. The existing Jazzy timings remain historical.

On the reference laptop, used the exported Lyrical userspace at
`/tmp/aw-twoedge-root` and the branch's Agnocast 2.4.1 source. To reproduce
the initial production-library build from the experiment worktree:

```bash
LYRICAL_ROOT=/tmp/aw-twoedge-root experiments/buffer/run-lyrical.sh bash -ec '
  export ENABLE_AGNOCAST=1
  w=build/agnocast-lyrical
  colcon --log-base "$w/log" build \
    --base-paths src/core/agnocast/src/{agnocast_cie_config_msgs,agnocast_cie_thread_configurator,agnocastlib} \
    --build-base "$w/obj" --install-base "$w/install" --executor sequential \
    --cmake-args -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG --param=ggc-min-expand=20 --param=ggc-min-heapsize=32768"'
```

The CIE configuration messages built in 15.4 seconds. CMake stopped at line
34 of the CIE thread configurator because Lyrical did not provide
`ament_target_dependencies`. Colcon therefore never compiled agnocastlib.
The guard measured a minimum of 9,458 MiB of available host memory. This
failure precedes kernel access and timing. Next, check upstream for the
target-linking migration, rebuild serially, then validate the library, heap
hook and Universe pipeline together before collecting latency samples.

### Validate the CIE prerequisite on Lyrical

AWF Agnocast main still called ament_target_dependencies for the configurator
library and both consumers on inspection. An AWF Agnocast issue/PR search for
“Lyrical” returned no match. Following the exported-target pattern of AWF
autoware_core PR 1540, link rclcpp::rclcpp and the generated message targets
publicly on the configurator. Both executables inherit those dependencies.
This is a local Agnocast patch. The existing kernel module is unchanged.

Repeat the preceding guarded build with BUILD_TESTING=ON and only the two
CIE package paths. Source build/agnocast-lyrical/install/local_setup.bash.
In the offline namespace, set XML_CATALOG_FILES to a libxml catalog mapping
http://download.ros.org/schema/package_format3.xsd to a local copy of
package_format3.xsd with package_common.xsd alongside it, both from
ros-infrastructure/rep's xsd directory. With the same build and install bases
run colcon test for those two packages with --executor sequential
--return-code-on-test-failure, then colcon test-result --test-result-base
build/agnocast-lyrical/obj. Both packages passed (three message-package
checks and five configurator test executables). Colcon reported 147 tests,
zero errors, failures or skips. Minimum host MemAvailable during the build
was 7,877 MiB. No Agnocast transport timing is implied by these tests.

### Build and test Agnocast 2.4.1 with Lyrical colcon

The production source and unit-test changes come from bazel commits c9051afc
and cd9f0479. They adapt rclcpp node interfaces, CIE executor overrides,
message_filters and tf2. AWF callback_isolated_executor issue 64 discusses
the executor override transition. It does not supply these Agnocast patches.
For colcon, link the exported package targets for the library, containers
and upstream tests and export the corresponding dependencies to consumers.
For upstream context see [AWF CIE issue 64](https://github.com/autowarefoundation/callback_isolated_executor/issues/64)
and [AWF Core PR 1540](https://github.com/autowarefoundation/autoware_core/pull/1540).

Use the preceding exported-root setup with the 2.4.1 host kernel module and
device, the offline XML catalog for the message package and a native Rust
toolchain on PATH. The heap hook uses the same Cargo.lock as the bazel branch.
Run experiments/buffer/build-agnocast-lyrical.sh through the guarded Lyrical
runner. In this offline root we staged rustc/cargo 1.99 and used cargo vendor
--locked before building. Neither the Jazzy ROS overlay nor a prebuilt Jazzy
heap hook was used. ldd resolved the heap hook to the colcon-built libagnocast
and the Lyrical rclcpp and tf2 libraries.

The library and three containers built in 8m13s. All 26 heap-hook Rust tests
passed. With the Cyclone RMW and upstream CMake preload settings, all 20
agnocastlib test executables passed in 1m43s. Their gtest XML contains 672
cases, 667 passed and five upstream skips for adding timers after a CIE
starts (AWF Agnocast issue 1263). Minimum available host memory was
7,577 MiB during compilation and 8,701 MiB during library tests. The discovery
agent was absent in this local-library configuration. This run validates the
kernel-backed library suites, not automatic discovery or remote bridging.

### Validate the enabled Autoware wrapper in both runtime modes

Build src/core/autoware_core/common/autoware_agnocast_wrapper against the
sourced Agnocast overlay, using the same colcon build/install bases and
BUILD_TESTING=ON, with ENABLE_AGNOCAST=1. Use the cppcheck configuration from
experiments/buffer/test-wrapper.sh. With that same binary use ENABLE_AGNOCAST=0
for the complete colcon wrapper suite. Then set ENABLE_AGNOCAST=1 and preload
the absolute path to install/agnocastlib/lib/libagnocast_heaphook.so into
obj/autoware_agnocast_wrapper/test_autoware_agnocast_wrapper.

Four generic pub/sub tests initially failed only in enabled mode. Two spun a
plain rclcpp executor which never dispatched their Agnocast callbacks, and two
requested an rclcpp node from an Agnocast-owned wrapper. The test-only patch
from bazel commit 1d0cd537 selects the corresponding executor and initializes
the required contexts. After rebuilding, the complete colcon wrapper suite
passed in fallback mode and all 37 C++ cases passed with the real heap hook in
enabled mode. Minimum available host memory for the rerun was 7,942 MiB.

### Port the Universe crop pipeline dependencies to Lyrical

The enabled node-registration macro from AWF autoware_core PR 1540 applies
directly. The old macro called the removed ament_target_dependencies in the
generated executable path. The enabled Universe crop and endpoint executables
both configured and linked against the patched installed macro.

For managed_transform_buffer 0.3.0, link the enabled wrapper and add its
exported include directories, following the consumer pattern from that same
AWF PR. The managed_transform_buffer tracker search did not identify a
Lyrical patch. In the Lyrical root with ros-lyrical-pcl-ros and
ros-lyrical-pcl-conversions installed, source the Agnocast overlay, export
ENABLE_AGNOCAST=1, and build the managed_transform_buffer package with
BUILD_TESTING=ON using Clang 21, -O1 -DNDEBUG and the GCC 15 C++ standard
library. The complete colcon suite passed with ENABLE_AGNOCAST=0 at runtime
(6.68 seconds). The original fixture publishes its TF data through rclcpp.
The serial GCC build approached the 1,200 MiB scope cap while compiling PCL
headers. Switching the PCL consumers to Clang kept the original cap.

In the original Universe filter, Lyrical removes the .h message_filters
forwarders, the second Subscriber template parameter and the raw RMW QoS
overload for plain ROS subscribers. Both explicit FilterBase instantiations
must compile. Retain the wrapper's RMW QoS input and the pre-Lyrical subscriber
signature. PCL 1.15 also removes pcl/io/io.h; PCL's previous forwarding
header explicitly names pcl/common/io.h as its replacement. AWF Universe main
still had the old filter includes when checked. The selected crop component
built and all three Cyclone runs verified its output. This validation is
scoped to the selected crop pipeline; other Universe filters were not built.

### Lyrical Universe reference: Cyclone passes, native Agnocast fails

Use a separate exported Lyrical root with ros-lyrical-pcl-ros,
ros-lyrical-pcl-conversions and clang-21 installed and the same host device.
Create the writable-workspace mount destination /ws and CUDA bind destinations
in that root as for the original exported Lyrical environment. Build the
enabled wrapper against the runtime overlay first. For the point_cloud_msg_wrapper
header dependency, extract the ROS Lyrical release 1.0.7-6 to
build/agnocast-lyrical/point_cloud_msg_wrapper (strip one archive component).
The release archive matched the [RCR source checksum](https://bazel.ros.org/modules/point_cloud_msg_wrapper/1.0.7-6.rcr.2/source.json).
No RCR source patch was applied to it.

With LYRICAL_ROOT set to the new root, run experiments/buffer/run-lyrical.sh
bash experiments/buffer/build-universe-lyrical.sh, then run the guarded
bench-universe-lyrical.sh with BUFFER_RUNTIME_ONLY=1 and a fresh output
directory. The benchmark selects the local Agnocast library prefix for the
source-built hook and keeps the ROS prefix fallback for Jazzy. Source, filter,
and sink are separate processes running the same executables across both
modes. The existing harness disables bridging and discovery in both modes.

The Cyclone control verified all 60 frames in each of three runs. Discarding
the first ten per group, pooled median/p95 were 1.514/1.745 ms. Each resource
CSV reports child user/system CPU seconds over startup, workload and teardown,
and the largest individual child's peak RSS in KiB. RSS is not summed across
processes. The benchmark stopped at the first native Agnocast failure. The
two outstanding Cyclone groups were collected independently with run_case.py.

The Agnocast crop process exited on signal 11 in FilterBase::is_valid, called
by faster_input_indices_callback on the Agnocast executor. No complete
Agnocast run exists. The installed sensor_msgs/PointCloud2 C++ struct uses
rosidl::Buffer for data even with ordinary CPU storage. Input validation calls
cloud->data.size(); the installed rosidl_buffer/buffer.hpp implements size()
through a virtual call on impl_. Sharing the heap allocation does not provide
the receiving process with the publisher's C++ virtual dispatch addresses.
This is the likely cause from the stack and source inspection. The successful
upstream library tests and wrapper tests do not cover this cross-process
PointCloud2 representation. The kernel module required no change.

A replacement must arrange receiver-local buffer state or use a separately
identified transport representation. Keep any copy/translation costs in the
measurement. A vector-backed compatibility message would answer a different
benchmark question. Changing the ROS buffer implementation remains a separate
planned experiment. The matched native Agnocast baseline and the migration
performance decision are open.

### Fix CPU PointCloud2 reads on the Lyrical Universe Agnocast pipeline

The Lyrical 5.2.1 rosidl_buffer header implements Buffer::size() through a
virtual backend call. Its public conversion to const std::vector<uint8_t> &
uses the cached CPU implementation's nonvirtual get_storage(). A reference
to that vector lets the consumer read the shared payload while its incoming
Agnocast message is alive. Use the view for native Agnocast size validation,
CPU cropping and sink verification. The ordinary validation path retains its
backend-aware size query. Keep independent height/stride/length checks and
the point-by-point sink verifier. No schema change, ROS patch, payload copy
for the view or kernel change is required. The crop still copies selected
points into its output as before.

This fix covers the measured native CPU allocation and CPU-only consumers.
General virtual backend operations, foreign GPU backends and mixed/bridged
readers require their own handling. AWF tracks the broader buffer integration
in [Agnocast issue 1187](https://github.com/autowarefoundation/agnocast/issues/1187).

With LYRICAL_ROOT pointing at the PCL-enabled Lyrical root from the preceding
entry, repeat from the worktree root:

```sh
experiments/buffer/run-lyrical.sh bash experiments/buffer/build-universe-lyrical.sh
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh bash \
  experiments/buffer/bench-universe-lyrical.sh build/agnocast-lyrical/cpu-view-results
```

Use a fresh results directory for each repetition. The harness now records
a successful CSV only after verifying all frames and checking child exit
statuses at teardown. Three fresh process groups per transport verified all
360 frames. For 150 post-warmup samples per transport, native Agnocast
median/p95 were 0.375/0.632 ms and Cyclone 1.232/1.565 ms. No buffer-backend
candidate ran in this gate. Minimum host MemAvailable was 6,245 MiB during
the serial build and 6,873 MiB during the runs. The earlier failed run and
its Cyclone samples remain independently documented.

### Add the ordinary Zenoh CPU control to the Lyrical Universe reference

With the same installed binaries and CPU-backed PointCloud2, select
rmw_zenoh_cpp with ENABLE_AGNOCAST=0. Start a fresh rmw_zenohd for each
group. Child CPU time includes the router, and the RSS statistic is the
largest peak among the router, source, filter and sink. No explicit rosidl
shared-memory buffer backend was selected.

For this addition, source build/agnocast-lyrical/install/local_setup.bash in
the guarded Lyrical runtime and invoke src/universe_crop_benchmark/run_case.py
zenoh with a fresh latency CSV and --resources CSV three times. All 180 frames
passed content and order checks. Across 150 post-warmup samples, median/p95
were 0.981/1.218 ms. Minimum host MemAvailable was 6,566 MiB. The table
retains the existing Agnocast and Cyclone measurements from the same build.
For future full comparisons, bench-universe-lyrical.sh includes all three
modes and rotates their order across rounds.

### Compare explicit host shared memory and CUDA on both Universe crop hops

On the same Ryzen 9 8940HX / RTX 5070 laptop, the Lyrical colcon fixture
applies buffer-transport.patch to build-directory copies of the Universe
filter. All five cases use the same enabled wrapper binary, original crop
algorithm, 512 KiB PointXYZIRC input at 10 Hz and 256 KiB retained output.
The two explicit cases construct the rclcpp subscriptions with
acceptable_buffer_backends set on both the filter and the verifier. The
Agnocast wrapper's ordinary subscription options do not expose that field.
Native Agnocast retains its production CIE; the RMW paths use the ROS
single-threaded executor. The fixture separately selects the source buffer,
checks the received backend at each hop, and verifies every output point and
frame in order. Incoming native Agnocast CPU bytes use the existing const
vector view.

The shared-memory case holds a read handle while the CPU filter reads the
mapped input. It verifies an IPC descriptor with a foreign publisher PID at
both subscribers. The filter packs retained points in CPU storage and copies
the exact output size into the outgoing shared-memory allocation. For CUDA,
the CPU filter downloads the input and uploads the packed output, synchronizing
the selected CUDA stream. The timed interval starts after source preparation
and ends at sink callback entry. Filter staging is included; source upload
and sink verification/readback are outside that interval. Child CPU and RSS
cover the complete run, including the Zenoh router.

To repeat, use the exported PCL-enabled Lyrical userspace from the preceding
Universe experiments, with the local shmem_buffer, cuda_buffer and corresponding
RMW backend plugins built in the sourced colcon overlay. From the worktree:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/buffer/build-universe-lyrical.sh
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh bash \
  experiments/buffer/bench-universe-lyrical.sh build/agnocast-lyrical/universe-buffer-matrix
```

Supply a fresh output directory for another run. The runtime script rotates
five cases over three rounds and launches one pipeline at a time. For each
case pool the final 50 of 60 frames per round. Report the median and
nearest-rank p95, plus median run-level child user+system CPU seconds and
median largest-child peak RSS. RSS excludes GPU VRAM.

All 15 process groups passed the payload and shutdown checks (900/900 verified
frames). Using the Agnocast baseline from this interleaved run, the 1.20×
limits were 0.424 ms median and 0.752 ms p95. Zenoh shared memory reached
0.751/0.930 ms and CUDA 1.594/5.807 ms. Shared memory improved on the Zenoh
CPU control, but both explicit backends exceeded both limits. CUDA's three
round medians varied from 1.444 to 4.655 ms. No clock/power or per-operation
trace was collected in this timing run to explain that variation. The known
Cyclone native-shmem incompatibility remains in the comparison notes with
its earlier diagnostics. Cyclone CUDA and Agnocast with explicit Buffer
backends have no measured Universe rows. Minimum host MemAvailable was
6,762 MiB during compilation and 7,312 MiB during the 512 MiB runtime scope.
[All samples](../../src/universe_crop_benchmark/results-lyrical-buffer-matrix/)
and [matrix with CPU, RSS and Agnocast ratios](README.md#lyrical-reference-status).

### Isolate the crop executor in the Lyrical Universe Buffer matrix

The preceding matrix used AgnocastOnlyCallbackIsolatedExecutor for the
native crop and SingleThreadedExecutor for every ROS crop. To test callback
isolation on the ordinary ROS path, register the same crop component a second
time with rclcpp_components_register_node, selecting the mixed-capable
agnocast::CallbackIsolatedAgnocastExecutor from the pinned Agnocast library.
Its child executor for an ordinary ROS callback group uses rclcpp.
This second launcher uses the rclcpp component main. All ROS cases run with
ENABLE_AGNOCAST=0 and the runner only preloads the heap hook for the native
Agnocast reference. This tests the linked Agnocast CIE implementation in the
existing enabled build. A standalone-CIE build and a kernel-module-free host
are independent deployment checks.

Use the same exported PCL-enabled Lyrical userspace, GPU and colcon overlays
as the preceding experiment. Use a fresh output directory:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/buffer/build-universe-lyrical.sh
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh bash \
  experiments/buffer/bench-universe-lyrical.sh build/agnocast-lyrical/universe-cie --compare-cie
```

Each ROS pair runs adjacently with the same endpoint binaries and crop library.
The script varies the order across the matrix. Three rounds include a fresh
native reference in each. Every run requires 60 validated frames and successful
teardown. Pool the last 50 per round for latency. CPU seconds include all child
processes and teardown. RSS is the largest individual child's peak. The runner
applies the default scheduling policy and launches no thread configurator or
competing callback workload. Native Agnocast retains its existing best-effort
QoS warning. Cyclone native shmem retains the earlier expected-failure result.

All 27 runs passed, covering 1,620 verified frames. Build and runtime minimum
host MemAvailable were 5,688 and 6,519 MiB respectively. The 1.20× native
ceilings were about 0.425/0.688 ms. POSIX shmem with CIE reached 0.774/1.003 ms
median/p95. The single-threaded pair reached 0.739/1.040 ms. CUDA plus CIE
round medians varied from 1.141 to 4.402 ms. The run supplies no attribution
for that variation. [All nine rows and samples](README.md#crop-executor-comparison-on-lyrical).

For the next buffer experiment, instrument crop output packing, promotion to
shared storage, descriptor export/import and callback entry separately. Try
writing surviving points directly into a shared allocation and committing the
actual length. The current ShmemBufferImpl resize copies on a size change.
Check empty, partial and full crops, descriptor length on both hops and
outstanding reader lifetime before timing. Keep the executor fixed while
testing that change. The executor result alone gives no estimate of the
latency recoverable by removing the copy.

### Agnocast-backed Buffer ownership on Lyrical

The original failing fixture is preserved at commit `c1680efb`.

Test the smallest adaptation of the existing Agnocast APIs before measuring
the Universe pipeline. The new colcon package registers a real
rosidl::BufferBackend plugin in a separate, opt-in overlay. A single writer
fills a 65,536-byte vector directly inside an Agnocast publisher loan, publishes
it on a private native topic, and takes a local reference for its PointCloud2
data field. Zenoh transports the ordinary ROS message with a UInt64 native
entry ID for each compatible reader. Each importing process wraps its native
ipc_shared_ptr in a local BufferImpl. The plugin checks the entry ID before
exposing the read-only view. CPU conversion copies bytes using to_cpu().
The private topic is unique to each invocation. The fixture assumes one writer
and one native reader per process. Normal applications would need endpoint
discovery and allocation integrated into the backend. Native participants
use the existing matched kmod and heaphook with bridging disabled.

Use the PCL-enabled exported Lyrical userspace and the Agnocast install from
the Universe comparison. Source the probe prefix only inside this experiment.
From the colcon worktree:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash src/agnocast_buffer_probe/build.sh
for run in 1 2 3; do
  BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh bash -ec '
    source build/agnocast-lyrical/install/local_setup.bash
    source build/agnocast-lyrical/probe-install/local_setup.bash
    python3 src/agnocast_buffer_probe/run.py'
done
```

All three repetitions reproduced the following outcomes:

- Both native-backed ROS readers waited for the publisher to destroy its ROS
  message before spinning. The second waited until the first held its loan.
  Both verified every byte again after a new native publication displaced
  the original history entry.
- A CPU-only ROS subscription verified the serialized fallback with no heap
  hook preloaded in that process. The Agnocast library was linked in the
  common probe executable.
- A depth-one ROS reader waited for two middleware arrivals before taking.
  It verified the replacement sample after the earlier descriptor was dropped.
- A depth-four ROS reader delayed taking the first descriptor until the second
  native publication. Its depth-one native reader could no longer take the
  matching generation. Zenoh reported a deserialization exception, and the
  probe verified that the stale descriptor never reached the ROS callback.
  An acquired reference to the original bytes was still live in another
  reader. The failure is in obtaining that specific reference.
- One holder released normally and the harness killed the other with SIGKILL.
  The publisher observed the original native payload destructor during
  subsequent native publishing. This checks reclamation of that allocation.
  It does not establish reclamation for exports in a future reservation protocol.

The harness succeeds when it observes the expected rejection. The migration
gate for valid queued ROS samples remains **failed**. A larger native history
moves the overrun point. Public TakeSubscription::take() advances a subscriber
watermark and supplies the entry ID after selecting a sample. The Lyrical
BufferBackend interface has export and import hooks and no per-sample
queue-discard notification. For the next experiment, reserve a specific
allocation for the destination before exposing its descriptor, acquire by
identifier at import, and release abandoned reservations on drop, disconnect,
or process death. Evaluate a bounded userspace protocol and identify any
required RMW queue-lifecycle support before changing the installed kernel
module. Repeat the delayed and dropped cases before collecting target latency.
Separate that work from direct-write POSIX and the Bazel integration.

The build ran sequentially with a 1,200 MiB cap, and runtime groups used the
512 MiB guard. Minimum host MemAvailable was 7,082 MiB at build and 6,901 MiB
across the three runs. [Recorded output](../../src/agnocast_buffer_probe/ownership-results.txt)

### Reserve the destination loan before descriptor publication

Test whether Lyrical's existing endpoint-aware Buffer hooks can close the
gap between Agnocast publication and ROS subscription take. The starting
case at c1680efb acquired the native loan during deserialization. Its delayed
depth-four ROS reader lost sample A when the depth-one native history moved
to B, even though A remained alive in another process.

In the revised plugin the destination advertises its process in backend
discovery metadata. For each compatible destination the publisher uses the
endpoint GID to find a private Unix socket. During descriptor creation it
sends the native entry ID and waits for confirmation. A receiver thread takes
the native loan, verifies the ID, and stores the loan before acknowledging.
On import the plugin transfers the matching reservation to the process-local
Buffer object. Only the ID and acknowledgement travel over the socket. An
unavailable peer, mismatched entry or exhausted budget can decline the
descriptor so the serializer sends CPU bytes. The test runs with
AGNOCAST_BRIDGE_MODE=OFF, stock Lyrical ROS libraries and the existing
Agnocast kernel module. The writer and native readers use the heap hook.

Repeat using the exported Lyrical root, native Agnocast install, and existing
POSIX Buffer overlay from the Universe experiment. From the experiment tree:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash src/agnocast_buffer_probe/build.sh
for run in 1 2 3; do
  BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh bash -ec '
    source build/agnocast-lyrical/install/local_setup.bash
    source build/agnocast-lyrical/probe-install/local_setup.bash
    python3 src/agnocast_buffer_probe/run.py'
done
```

All three runs verified:

- The delayed ROS reader consumed A and B in order as Agnocast-backed Buffers
  after the native history advanced. The depth-one ROS reader verified B after
  two arrivals. The fast reader held A across replacement.
- CPU readers verified all 65,536 bytes, including a receiver advertising
  Agnocast support with no running reservation service. The POSIX-only
  subscriber received CPU bytes, promoted them to POSIX storage, and published
  a POSIX-backed reply. The Agnocast-only ROS subscription received that reply
  as CPU bytes. These cross-backend transfers include copying.
- A receiver acquired A and was killed with B still pending. Subsequent native
  publishes triggered destruction of both original allocations after the other
  readers finished. The bounded receiver accepted 16 queued native loans, read
  the seventeenth payload via CPU fallback, then accepted native storage on the
  following publication after draining. The pending limit is 16 entries and
  4 MiB per receiver. Each run exercised the entry limit.
- Minimum host MemAvailable was 7,020 MiB during the sequential build and
  6,416 MiB over the three 512 MiB runtime scopes.
  [Captured observations](../../src/agnocast_buffer_probe/reservation-results.txt)

The probe assumes one ordered writer and one subscription per destination
process. Taking a newer native descriptor retires older unconsumed reservations
for that stream. ROS exposes no per-descriptor queue-discard notification in
this backend API. A final dropped frame or lost acknowledgement can therefore
hold a bounded reservation until later native progress or teardown. If all
reserved descriptors are dropped at saturation, subsequent CPU fallback can
persist until teardown. The private socket directory and run-specific metadata
are fixture setup. A general implementation needs per-endpoint stream identity,
restart handling, and recovery from that saturation case. The synchronous
per-peer socket exchange also needs a cost measurement on the Universe path
before considering the 1.20× native target. No latency figures were collected.

### Whole-message RMW loan experiment

The first whole-message experiment is in [experiments/rmw-agnocast](../rmw-agnocast/README.md).
It pins the matching Lyrical rclcpp subtree, records the stock lifetime failure,
and tests retained dynamic trajectories with the heap hook and native kernel
module. Its ROS transport entry points differ from the Buffer descriptor path.

### Loadable same-host RMW

Built the two colcon packages and selected rmw_agnocast from two standard
rclcpp processes in the exported Lyrical userspace on the laptop. The native
executor signalled a guard in the ROS wait set. Each writer waited 100 ms
after reader readiness before sending, and the reader checked callback arrival
within two seconds with a three-second spin timeout. Three repetitions passed
in each of two modes (loan and copy), four trajectories per pair. Loans
preserved the object, vector and string addresses for the three borrowed
publications. Both modes validated retained A, callback exception C and
ordinary copy publication D. The build used one job and minimum sampled host
MemAvailable was 5,582 MiB over this build and the three runs. These are
functional results for one type and one reader. Cross-host reachability,
bridge policy and pipeline performance are open.
[Setup, reproduction commands and migration gates](../rmw-agnocast/README.md#loadable-local-transport-and-migration-plan)

### Two native readers under queue pressure

The loadable RMW passed three bridge-disabled pressure runs and a fourth
under syscall tracing. A fast reader received sequences 1–11 plus a
delegated Cyclone control message. A paused reader kept 1 through ROS
context shutdown, observed the KeepLast(2) tail 8 and 9, and returned its
kernel reference once on another thread before the subscriber was removed.
A fresh process with the same node name received 11. The original single-reader
loan and copy checks passed again. The final build and validation used one
build job, a 1,200 MiB build cap and a 512 MiB runtime cap. Minimum sampled
host MemAvailable was 5,439 MiB in that series. Only the fixture changed.
[Reproduction, kernel evidence and limits](../rmw-agnocast/README.md#two-reader-pressure-and-restart)

### Generalize the native object loan

The middleware now uses Agnocast TypeErasedPublisher and Subscription<void>
with installed rosidl_typesupport_introspection_cpp. Message packages supply
their normal generated C++ type support. The shared library has no Autoware
message dependency. The introspection size, init and fini callbacks provide
object lifetime. Its missing whole-object assignment is handled by Cyclone
serialization/deserialization on ordinary copy calls only. Buffer members
are detected recursively before sharing any object with foreign virtual state.

In the same exported root as the pressure experiment, run build-loadable.sh
via run-lyrical.sh, then run run-loadable.sh with point, path, trajectory and
pressure in separate guarded runtime invocations. Each type passed four
publications in each reader mode. The Path check included five PoseStamped
elements with heap-backed frame IDs, retained ownership, cross-process
addresses and ordinary publish/take. The earlier pressure case also passed.
Minimum sampled host MemAvailable over build and runs was 4,805 MiB.

### Check recorder delivery, QoS, metadata and abrupt reader exit

Run run-loadable.sh contract after the guarded loadable build. The writer
offers best effort. One process holds a typed Point loan, deserializes another
subscription's recorder-style sample and requests a third, reliable
subscription. Implemented both serialized take entry points on the native
queue; a delegated Cyclone reader has no native payload. The writer's graph
count reached two. The reliable reader received an incompatibility warning
and nevertheless got a native callback. The typed callback's source and
receive timestamps were zero and its GID implementation identifier was null.
Treat both as open RMW contract failures. Do not infer reliability matching
from DDS discovery.

The reader sent SIGKILL to itself with the first sample retained. Only after
the shell verified its exit status and checkpoint did the surviving publisher
send 512 more loans. It reused the original object address. This establishes
recovery of that allocation on this run; it is not a heap-wide leak test.
Guarded build/run minimum MemAvailable was 4,830 MiB. The bridge was disabled.

### Measure Buffer-containing PointCloud2 on the Universe target

Enabled an explicit serialized representation for Buffer types only and
selected /input and /output on the ordinary ROS crop. The serialized byte
allocation has a private native topic per ROS topic and type. This costs
copies but preserves process-local virtual backend dispatch. Minimum host
MemAvailable over the three-round matrix was 5,141 MiB. Verified 1,080/1,080
frames. The rmw_agnocast median/p95 measured a factor
of 2.61/2.04 relative to native itself, missing both 1.20× gates. Retain the
native reference and defer the Bazel migration milestone.
[Method, complete comparison and reproduction](../rmw-agnocast/README.md#type-support-and-the-universe-decision)

### Launch the upstream bridge explicitly on the laptop

Ran the guarded loadable build and run-loadable.sh bridge. An ordinary
Cyclone peer and a native RMW peer matched through the graph in each
direction. Both bridge-off controls delivered zero Path callbacks. Launching
the upstream generic bridge with BridgeLoader delivered exactly one verified
Path in each direction. Only the native peer and bridge preloaded the
Agnocast hook. The bridge runtime minimum sampled host MemAvailable was
5,637 MiB. The second host and second container were explicitly deferred.
[Fixture scope and verification](../rmw-agnocast/README.md#explicit-bridge-boundary-on-the-laptop)

### Distinguish Cyclone codec cost from DDS payload transport

Control for RMW selection and heap-hook effects by loading the same library
and clearing only the topic allowlist. All six timed runs delivered 60/60.
Two separate instrumented payload runs and a codec-only run verified the
transport path and measured the four codec calls per frame. The Path bridge
fixture separately reported the Cyclone graph GID and the native callback's
missing sample metadata. Point, Path, Trajectory, two-reader pressure, crash
recovery and all four bridge cases passed the final functional rerun, with
the recorded QoS and metadata limitations. Builds used one job.
[Commands, numbers and interpretation](../rmw-agnocast/README.md#which-part-of-the-latency-comes-from-cyclone)

### Restore the laptop environment on native Docker Engine

The old exported Universe root was in /tmp and had disappeared. The persistent
colcon overlays and cached Clang packages were still in the worktree. On
2026-11-20 the source Desktop containers were stopped. Commit each of
awf-buffer-lyrical-zenoh, awf-cyclone-baseline and awf-buffer-wrapper-disabled
to its own local/autoware-migration tag, save the linux/amd64 image to a file,
load into Engine and compare image inspect .Id across the two contexts.
All three IDs matched. The existing session used sg docker for Engine access.
The wrapper snapshot already contained the Lyrical PCL, Cyclone and Zenoh
packages. Recreate its /sources bind read-only from the original worktree.

Export the recreated wrapper container with docker export and extract into a
user-owned persistent directory. Extract build/agnocast-lyrical/clang-debs/*.deb
there with dpkg-deb -x. Create the /ws and /usr/local/cuda-13.0 mountpoints
inside that root, the relative /usr/local/cuda symlink to cuda-13.0, and empty
destinations for the host driver files bound by run-lyrical.sh. The read-only
root requires these mount destinations before running the namespace helper.

Set LYRICAL_ROOT to the resulting directory and run:

```bash
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh bash -ec \
  'test -c /dev/agnocast; clang++-21 --version; ros2 pkg prefix pcl_ros; ros2 pkg prefix rmw_cyclonedds_cpp; ros2 pkg prefix rmw_zenoh_cpp'
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout 60s \
  bash experiments/rmw-agnocast/run-loadable.sh point
```

Clang reported 21.1.8. All three package lookups resolved in /opt/ros/lyrical.
The existing Point loan and copy probes each verified four samples. Record
the verified LYRICAL_ROOT in build/agnocast-lyrical/universe-root. This
migration check produced no new performance measurements.

### Compare native and RMW Path loans with the same payload

Added a two-process nav_msgs/Path benchmark to the colcon loan probe package.
The source loan, nested pose vector and heap string addresses matched across
processes for all 960 frames over two series. Each reader also revalidated its
retained previous sample. Four alternating-order rounds per series ran
sequentially on the restored laptop using its memory guard. In the series
with child resource accounting, native median/p95 were 41.86/56.25 µs and
RMW median/p95 were 79.04/116.61 µs. Both 1.20× limits were exceeded even
with the Buffer and codec costs removed. Minimum sampled host MemAvailable
over the build and both series was 15,348 MiB. Next isolate receiver wakeup
and dispatch before attributing the excess to rmw_wait.
[Exact setup, commands, both series and limitations](../rmw-agnocast/README.md#matched-path-object-loans)

### Attribute the Path receiver handoff

Instrumented only the selected reader and reused the matched 10 Hz Path
fixture on the restored laptop. All 1,080 payloads passed and all 360 traced
entries matched the verified loan addresses. In 300 post-warmup traced
samples, guard-trigger start to delegated wait return measured
26.59 µs median, ready to loan take
entry 10.68 µs and the take body
0.63 µs. The untraced control still
missed both 1.20× targets. No transport semantics were intentionally changed.
[Controls, exact commands, stage boundaries and raw data](../rmw-agnocast/README.md#receiver-handoff-timing)
The tracing-off mixed native/Cyclone pressure and restart case and both Path
loan/copy regression cases passed after instrumentation. Minimum sampled host
MemAvailable was 14,614 MiB over the serial build, matrix and regressions.

### Poll Agnocast readiness on the ROS executor

On the laptop's exported Lyrical root, built the loadable RMW and standard
rclcpp probes serially with colcon. Added an opt-in wait mode that consumes
the Agnocast notification fd on the ROS executor and delegates ordinary DDS
waits to a per-wait-set helper. The helper must release the caller's wait
arrays before the executor can return. No kernel or Agnocast subtree patch
was needed for this step. Strengthened the mixed pressure test with a delayed
DDS publication and a deadline shorter than the executor spin timeout.

Both modes passed mixed wake, pressure and restart. Direct mode passed
loan/copy, retained ownership, killed-reader recovery and both manual bridge
directions. The Universe CDR path verified 60/60 frames. The final matched
Path series verified 1,080/1,080 frames. Direct median/p95 were 82.38/119.38 µs,
existing RMW 80.42/113.86 µs and native Agnocast 40.74/62.01 µs. Both series
missed the native-relative 1.20× latency gate. The final build and run groups
kept at least 14,917 MiB host memory available. Keep the current default and
measure the remaining helper rendezvous before advancing migration.
[Reproduction commands, resource accounting, limitations and raw samples](../rmw-agnocast/README.md#direct-native-readiness-in-a-mixed-ros-executor)

### Measure the DDS helper rendezvous on the direct wait path

Added opt-in timestamps to the selected direct reader and a matched six-round
native/direct/traced Path comparison. The existing Lyrical root, loan probe,
colcon overlay and memory guard were reused. The executor records poll return,
guard handling, native acquisition/drain and synchronization. The DDS helper
independently timestamps its wait return. One row per verified loan ties these
events to the publisher and application callback. The harness rejects a
missing blocking wake or an inconsistent timestamp chain.

All 1,080 frames passed. On the untraced direct path, median/p95 were
89.82/130.97 µs versus native 44.56/69.42 µs. In 300 post-warmup traced
samples, the per-frame sum of interrupt and post-drain synchronization was
20.26/35.92 µs. Cyclone had returned by the end of native drain in 168 cases.
Acquisition and application dispatch also used substantial time. Keep the
migration gate closed and measure the corresponding native executor stages
before selecting the next implementation change.

With tracing disabled, both direct and default modes passed the mixed DDS wake,
queue-pressure, retention and restart checks; direct Path loan and copy both
passed. Reproduce with the previous entry's pressure/path commands and the
[matched matrix, stage definitions and raw data](../rmw-agnocast/README.md#timing-the-direct-wait-dds-helper).
The benchmark's minimum sampled host MemAvailable was 15,288 MiB.

### Compare native and direct RMW at the same Agnocast loan boundary

Added two compile-time, default-off userspace probes at subscription epoll
return and subscriber loan creation. The first native-only trial could not
pair the RMW's callback-entry timestamp with the exact loan-creation boundary.
Built a private copy of the instrumented Agnocast library for both traced
readers and restarted the matrix in a fresh directory. The preliminary runs
are excluded. The installed library and kernel were reused. The new harness
also checks the direct RMW's inner native timestamps against its outer wait
trace by object address and entry ID.

The four-round, order-balanced laptop matrix verified 960/960 Path frames.
Between native epoll return and loan creation, native/direct medians were
25.08/21.21 µs. Between loan creation and application callback, they were
0.68/33.03 µs. All four rounds showed the post-loan gap. Untraced end-to-end
median/p95 were 51.90/94.28 µs native and 111.21/186.62 µs direct. Keep the
Bazel milestone closed. Compare the ROS executor's dispatch overhead next.

The single-job build and serial matrix kept at least 13,792 and 14,396 MiB
host memory available respectively. After the matrix, the untraced direct
Path loan/copy and mixed DDS wake, queue pressure, retained-loan and reader
restart cases passed using the previous entry's path and pressure commands.
[Exact build and run commands, timing boundaries and raw measurements](../rmw-agnocast/README.md#comparing-the-native-executor-and-direct-rmw-at-loan-creation).

### Isolate single-ready-subscription dispatch

On the exported laptop Lyrical root, built a probe-only single-threaded
executor control that uses the same wait_for_work and execute_subscription
methods as normal rclcpp dispatch. Only the ready-subscription selection and
scheduling path changes. Five serial, position-balanced rounds verified all
1,500 Path frames, all retained payloads and all 600 traced wake records.
Minimum available host memory was 17,357 MiB during build and 17,092 MiB
during the matrix. Ready-to-callback median/p95 fell from 20.04/35.37 to
18.18/31.22 µs. Untraced single-selection latency remained 1.73×/1.80× native,
with slightly worse p95 than ordinary selection. Keep this fixed-topology
control diagnostic and retain standard ROS executor behavior. Revisit the
mixed wake and DDS synchronization path before optimizing entity selection.
[Commands, matched controls, metrics and raw frames](../rmw-agnocast/README.md#dispatching-a-single-ready-path-subscription)

### Measure the helper-free native wake control

In the existing Lyrical colcon overlay, added a diagnostic native-only poll
to the direct RMW reader. Used standard ROS executor dispatch throughout.
Six serial rounds balanced all three run orders pairwise and verified 1,080
Path deliveries. Native Agnocast median/p95 were 41.03/58.69 µs, mixed RMW
83.91/121.37 µs, and helper-free RMW 66.39/105.53 µs. Minimum host memory
available during build and matrix was 17,000 and 17,060 MiB respectively.
The mixed pressure/restart test passed. The helper-free reader missed the
idle DDS wake deadline as expected. Both helper-free latency metrics exceed
the 1.20× native target. Keep the Bazel milestone closed and investigate
PointCloud2 CDR avoidance on Universe next.
[Exact commands, control semantics and measurements](../rmw-agnocast/README.md#removing-the-dds-helper-from-the-direct-wait)

### Give the blocking mixed DDS helper its own wait set

A fixed-topology probe scope pins entity lifetimes in a single-threaded
executor. The helper waits indefinitely in Cyclone on private arrays and
signals an eventfd on DDS readiness. Native delivery completes on the ROS
executor without interrupting or joining the helper. The scope stops and
joins it before entity teardown. This required changes to the experimental
RMW and probes only. Three runs also replaced an ordinary DDS subscription
between scopes, resumed on the same executor, delivered simultaneous DDS and
native samples and shut down the idle context. The ordinary mixed pressure
regression passed.

Three serial laptop matrices verified 2,880 frames. Every fixed reader
reported 60 native wakes and zero native-triggered helper interruptions.
Pooled median/p95 were 69.44/101.55 µs fixed, 68.74/98.57 helper-free,
86.64/130.73 original mixed and 43.86/78.32 native. The fixed path recovered
the helper-free median with both wake sources, while p95 varied between
series. The 1.20× migration gate remains unmet. Minimum sampled host
MemAvailable during the three matrices was 13,582 MiB. Keep coordinated
reconfiguration and stable entity ownership explicit before broadening use.
[Setup, checks, exact commands, all three series and limitations](../rmw-agnocast/README.md#fixed-topology-blocking-mixed-wait)

### Test fixed wait and CDR avoidance in the Universe crop pipeline

Ran the existing Lyrical Universe source, crop and sink on the laptop. Starting
fixed wait immediately rejected a crop event-list growth (10 to 18 after the
first native wake). Used ordinary mixed wait through the first verified
frame, drained ready work, then entered the fixed scope under the standard
executor. All fixed receivers recorded 59 native wakes, zero native-triggered
interruptions and one stop interruption. The broad three-round matrix
verified 1,440 frames. Fixed CDR median/p95 were 0.970/1.120 ms, versus
0.998/1.328 ms with ordinary direct mixed wait and 0.365/0.545 ms native.

Added an opt-in, versioned same-host representation for CPU PointCloud2. Typed
delivery copies metadata and point bytes into Agnocast storage, then
reconstructs a process-local CPU Buffer. The callback never uses another
process's Buffer implementation. Empty and padded cloud fixtures matched
field-for-field in typed and serialized subscriptions. Direct Path loan/copy
and mixed pressure/restart regressions passed. Two focused matrices verified
another 1,080 frames: pooled median/p95 were 0.554/0.961 ms for host-cloud,
0.994/1.126 ms for fixed CDR and 0.395/0.876 ms for native Agnocast. The 1.40×
median misses the 1.20× target. Keep the Bazel milestone closed and focus the
next control on payload copies and receiver-held loan lifetime. Minimum sampled
available host memory in the three matrices was 13,940 MiB.
[Commands, startup failure, counters, CPU/RSS and raw frames](../rmw-agnocast/README.md#fixed-mixed-wait-in-the-universe-pointcloud2-pipeline)

### Receive Universe point bytes through a Buffer that owns the native loan

Added an opt-in typed host-cloud take that restores PointCloud2 metadata and
attaches the received Agnocast loan to a reader-local, read-only Buffer. The
new backend requires an explicit accepting subscription. A CPU-only subscriber
in the same process still receives a mutable CPU Buffer, and a generic reader
receives serialized ROS data. The two-process probe verified payload address
identity, independent mutation of a copied message, survival past history
eviction and ROS shutdown, and final release on another thread. The copying
and pressure/restart regression probes also passed.

The first component run exposed a cross-library vtable comparison in the
cast to a final header-defined backend class. A backtrace and disassembly
located the failure. Removing final enabled RTTI lookup and both receiving
components completed six runs. Both RMW controls used explicit typed
subscriptions. Each retained the blocking wait and both achieved 59 native
wakes with zero native interrupts at each receiver in every timed run.

Six laptop runs per case verified 1,080 frames. Native, receive-copy and
receive-view median/p95 in ms were 0.387/0.602, 0.510/0.659 and 0.475/0.595.
The view's 1.23× native median misses the 1.20× gate in both run orders.
The pooled p95 passes. Total child CPU time increased from 0.652 s for
copy to 0.710 s for view. Builds ran serially with a 1,200 MiB cap and
runtime used 512 MiB. Lowest sampled available host memory was 12,465 MiB.
Keep the Bazel milestone closed. Next isolate the publisher copy at each
edge with an explicitly loaned output allocation and the same receiving view,
retaining a conventional CPU publication as the control. Open issues include
mutable publisher ownership, cancellation, and the cost of slow readers.
[Reproduction, raw samples and limitations](../rmw-agnocast/README.md#receiver-local-buffer-view-of-an-agnocast-point-payload)

### Borrow CPU PointCloud2 before filling both pipeline outputs

Tested the Lyrical rclcpp publisher-loan API on the same Universe crop fixture
and fixed blocking wait as the receiver-view experiment. A benchmark-local
wrapper borrows at unique output allocation time. The RMW restores typed
metadata in shared storage, moves the point vector when it is in the writer
pool, and copies a private vector. The contract checked an abandoned loan,
retention through shutdown, serialized and CPU subscribers, address identity
for shared storage and a deliberate private-vector fallback. The copy-mode
and mixed pressure/restart regressions passed.

A late borrow let process-private fields reach the reader, which faulted in
metadata restoration. Moving the borrow before filling and checking allocation
ownership fixed this. Loaning during the crop's first callback also caused a
Cyclone abort while creating the lazy PublishedTime publisher. Running the
first frame with ordinary publication allowed the diagnostic graph to initialize.
The other 59 publications per edge used loans with zero private payload copies.

Six laptop runs per case verified 1,080 frames. Native, conventional publication
with a receive view, and loaned publication with a receive view measured
0.317/0.523, 0.424/0.553 and 0.386/0.582 ms pooled median/p95. Loan/native
was 1.19×/1.04× in the forward series and 1.23×/1.15× in reverse. The pooled
1.22×/1.11× misses the median criterion. Keep Bazel adoption pending.
Minimum sampled host availability was 12,244 MiB during the final Universe
build and 13,175 MiB during the paired matrices. Next isolate the allocation
window around the point payload so DDS diagnostics and other callback
allocations can run on the ordinary heap, then test cold-start entity creation
and repeat the paired measurement. The same-thread publisher restriction and
stable graph during fixed wait still apply.
[Commands, ownership checks, failures and raw runs](../rmw-agnocast/README.md#transfer-cpu-point-payloads-through-a-publisher-loan)

### Use shared point storage from the first crop callback

Changed the source and crop to scope Agnocast heap selection around point
vector growth. They fill ordinary PointCloud2 messages and acquire the
publisher loan at handoff. At that point the existing RMW copies the metadata
and transfers the already-shared vector. This eliminated the first-frame copy
and allowed cold DDS diagnostic creation on the original crop path. A separate
Cyclone subscriber started before the crop, confirmed the topic initially had
zero publishers and received 60 stamped PublishedTime messages. Its pipeline
also verified 60 output frames and 60 zero-copy point publications per edge.

Six fresh process groups per case verified 1,080 frames. Pooled native,
previous longer loan and scoped median/p95 were 0.357/0.616, 0.421/0.641 and
0.424/0.671 ms. Scoped/native met the pooled target at 1.19×/1.09×. Median
varied with order (1.30× forward, 1.17× reverse). The scoped case used 0.668 s
child CPU per run versus 0.619 s native. Serial builds and runtime used
1,200 and 512 MiB memory caps. Lowest sampled host availability was 17,200 MiB.

I edited the benchmark driver while the forward series was running. Bash
reported a parse error after writing all nine verified frame logs and their
resource files. Reran the summary on those complete files and ran the reverse
series and diagnostic with the finalized driver. A Bazel reproduction milestone
is warranted for this specific same-host CPU path. Broad wrapper migration
still needs evidence on a dynamic graph, realistic sensor load and stable
latency across repeated runs.
[Commands, controls, raw results and restrictions](../rmw-agnocast/README.md#scope-the-agnocast-allocation-to-point-storage)

### Record the bridge decision and test native reliability matching

Treat 1.20× native median and p95 as an engineering milestone. Judge small
latency changes by paired runs and variability. Prefer supervisor-owned,
startup-configured bridge routes for the pilot. The explicit bridge and
Cyclone PSMX designs remain alternatives. Their comparative PointCloud2
bridge costs are open.

Ran the standalone two-writer Path test first. In the two native runs all
endpoints used rmw_agnocast, with bridging off. Counts reported one compatible
writer. Both native copy and loaned takes delivered all three incompatible
samples alongside the three compatible samples, with empty GIDs. A separate
ordinary Cyclone run delivered only the three compatible samples and passed
receiver-local attribution. An initial writer-process byte comparison also
failed under Cyclone, so the final probe calibrates callback identities in the
receiving process. The native assertions remain red to catch the actual fix.
[Commands, evidence, diagnosis and next test](../rmw-agnocast/README.md#two-native-writers-and-one-reliable-path-reader)

### Enforce native reliability matching and preserve the writer GID

The two-writer Path probe from the previous entry exposed a data-plane mismatch
with the Cyclone discovery counts. Reading the Agnocast kernel receive loop
showed why rejecting incompatible messages during take would be too late at
shallow depth. Implemented separate native reliability routes and a versioned
writer-GID trailer in the experimental colcon RMW. Native data stays on the
laptop and uses the existing heaphook and kernel module. Cyclone supplies the
graph endpoint identity and resolved QoS.

Validated both blocking wait paths and both rclcpp take modes. With two writers
on one ROS topic the reliable reader received sequences 2, 4 and 6, zero
incompatible samples, and the originating writer's GID. With an idle depth-one
reader, a reliable sample followed by 64 incompatible publications survived
in all four configurations. A best-effort calibration reader received all six
subsequent samples, classified by writer. Typed and generic serialized cloud
callbacks also matched the writer GID for CDR, host-cloud, view and publisher
loan paths. Pressure, reader death, retained references, Path, Trajectory and
60 distinct delegated diagnostics passed. Both upstream bridge directions
passed their on/off controls with the legacy wire explicitly selected.
A bridge into the versioned native routes remains open.

[Reproduction, benchmark and deployment implications](../rmw-agnocast/README.md#native-writer-identity-and-reliability-before-queue-admission)

The legacy depth-one negative control lost its reliable sample and delivered
one incompatible sample. Ordinary Cyclone and the new implementation retained
the reliable sample. All 1,440 crop frames across 24 timed runs passed. The
new wire reached 1.23× native median and 1.17× p95. Paired p95 deltas against
the legacy wire varied from −72 to +151 µs. Keep tracking that spread as we
implement a bridge for the versioned format.

### Bridge the versioned native wire to ordinary Cyclone DDS

The previous raw bridge needed legacy mode and lost native writer attribution
and reliability filtering. Built a typed startup route on top of the v2 RMW.
The RMW now accepts separate native topic lists for publisher and subscriber
creation. A single relay process uses a native endpoint on one side and a
delegated Cyclone endpoint on the other. No modification to the kernel module,
heaphook or ROS message definitions was needed.

On the laptop, Path tests passed on the same public topic in both directions.
The disabled route delivered zero messages. The enabled route forwarded
one reliable Path and excluded the incompatible Path. Native GID equality
held between the original writer and the bridge ingress for outbound traffic,
and between the bridge publisher and native reader for inbound traffic.
The Cyclone leg exposed receiver-local GIDs. The legacy bridge controls and
the native depth-one incompatibility flood also passed.

The Universe crop compared a local sink alone, a live subscribed bridge with
zero external readers, and a bridge forwarding to an additional stock Cyclone
sink. Both sinks validated point content and order. The biggest visible
difference was at the external reader. Across the six paired rounds it saw
the same frame 0.551 ms later at median. The local median ranged from
0.436 to 0.438 ms across configurations. Idle bridging still consumed CPU
because it took native clouds before checking the external match count.
The separate profile measured 0.538 ms median from source stamp to bridge
callback, 0.334 ms inside that callback through publication and 0.429 ms
from callback entry to observer. The latter two intervals overlap.
The next candidate is avoiding idle payload materialization while qualifying
delivery to a newly attached reader.

[Build, test commands, complete matrix and limitations](../rmw-agnocast/README.md#crossing-the-versioned-native-boundary)

### Start serious-load item 5 with a branched Universe sensing graph

Prioritize representative graph coverage before spending the next latency
budget on the simple crop or tuning the idle bridge. Reused the existing two
Universe crop binaries as serial stages, with a second verifying subscriber
on the intermediate output. Both readers checked all 60 frames, including
every point's content and order. The fixed-wait RMW verified native wakes
and scoped loans at all three publishers. Ran four rotated rounds across
native Agnocast, rmw_agnocast, Cyclone DDS and Zenoh under the 512 MiB runtime
cap. A publisher timer parameter enabled a second rate on the same laptop.
Repeated native and RMW with interleaved rates on the rebuilt binary and
rechecked the original two-edge cold diagnostic. The matrix and confirmation
delivered 2,400 source frames at each reader.

Final RMW/native median ratios at 10 Hz were 1.23 and 1.24 in the two
batches. At 50 Hz they were 0.97 and 1.03. The corresponding 50 Hz p95
ratios were 1.03 and 1.25. A 1.10× engineering milestone is worth pursuing
on selected paths. Median parity on a short burst is plausible. A repeatable
tail or full-system parity result is still open. This step covers branch
fan-out, multiple stages and higher offered load. Next add heterogeneous
sensor inputs and synchronization on a real Universe sensing path under
sustained replay before expanding to the full launch.
[Setup, commands, measurements and limits](../rmw-agnocast/README.md#item-5-first-branched-universe-workload)

### Separate the preceding gap from the sustained rate

The previous 10 and 50 Hz batches changed the rate for a whole process
lifetime. Tested both intervals inside the same process using the branched
Universe workload. Reversed starting phase and transport order over four
runs, first without callback probes and then with a first-crop thread
profile. Also ran constant-gap controls with the same publisher code.
The first native smoke timed out because the fixture checked the rclcpp
context in a native Agnocast-only process. Bounded the diagnostic publisher
loop by the existing 60-frame contract and reran from fresh directories.

Without callback profiling, the final median after a 100 ms gap exceeded
the median after a 20 ms gap in all four RMW runs (56–98 µs). The pooled
medians were 678 and 592 µs. Native Agnocast also slowed after the longer
gap (604 and 554 µs). The first RMW crop's thread accumulated no reported
between-callback runnable delay in any of the 200 profiled post-warmup
intervals. Its stamp-to-entry median rose by 32 µs and its callback CPU time
rose by 37 µs. Longer CPU execution and pre-callback delivery both warrant
investigation. Raising executor priority lacks a measured run-queue bottleneck
here. Exact kernel wakeup times were unavailable to the unprivileged user.
[Setup, raw observations, controls and interpretation](../rmw-agnocast/README.md#does-the-preceding-idle-interval-explain-the-branched-latency-gap)

### Localize the first-hop 10 Hz penalty before choosing a fix

Added opt-in buffered timestamps at native publish, blocking wait and
host-cloud take. Joined writer, reader and first-crop observations by frame
stamp and uniquely matched each post-warmup poll to its loan. Ran four
alternating-gap rounds and two steady-gap pairs per transport. Repeated the
steady pairs with two extra native admission/readiness markers. All 1,440
source frames passed both readers and the original cold diagnostic passed.

Poll return to the take marker carried a repeatable 18–20 µs median increase.
In the steady repeat, +8 µs was before loan admission and +10 µs from exposed
readiness to the take marker. Handoff-to-poll changed sign between batches.
The steady 10 Hz end-to-end median ratio moved from 1.07× to 1.18× despite
similar first-hop results. No latency optimization was applied. Next test
one blocking ready-descriptor dispatch change against the unchanged control
before attributing a 1.10× or parity result to it.
[Commands, stage definitions, evidence and target arithmetic](../rmw-agnocast/README.md#locate-the-first-hop-cost-after-a-longer-gap)

### A/B dispatch from the already-ready subscription descriptor

Implemented an opt-in RMW path that resolves each route FD to its registered
Agnocast callback ID at reader creation and hands the known POLLIN route to
the stock event handler. It drains deferred work and continuations using the
same executor machinery. Compared with zero-time inner epoll in the same
binary, preserving the blocking outer wait and DDS delegation. Reliability
flood, pressure/restart, retention, crash cleanup, host-cloud loan and cold
DDS diagnostics passed. Dispatch counts verified the new path on both crop
stages and both readers. All 2,640 frames from 44 runs passed both sinks.

At 10 Hz the first unprofiled result moved from 1.15× to 1.08× native median.
A separate repeat moved from 1.15× to 1.20×. B won three of eight same-round
median comparisons and used more child CPU in both batches. Both variants
measured 41.4 µs median from poll to take at 10 Hz. B retained the longer-gap
poll-to-loan penalty. This isolates redundant epoll discovery as an
insufficient explanation. Keep A as default and investigate the event handler
and downstream ROS dispatch separately before another optimization.
[Commands, controls, measurements and limitations](../rmw-agnocast/README.md#ab-the-ready-subscription-descriptor)

### Trace the existing receive handler and ROS dispatch at 10 and 50 Hz

Kept A as the control following the ready-descriptor A/B. Instrumented an
isolated Agnocast library at the eventfd read, deferred receive function,
receive ioctl and loan callback. Loaded it only in the first RMW crop.
Wrapped the RCL wait, capability check and take APIs there. Joined the
markers to each original cloud timestamp and required their ordering.
Added a repeat that separates rclcpp ready-selection and subscription
execution, and a light-probe control to quantify broad observer effects.

The eventfd read changed by +0.2 µs in alternating gaps and −0.2 µs in the
first steady comparison. The receive ioctl rose by 1.2–1.3 µs. Several
smaller costs accumulated on the native receive side. The first two profiles
both put about 6 µs of extra time between RCL wait return and the loan check.
The lighter steady profile still measured 30.1 µs additional time from
source stamp to first callback at 100 ms. These measurements narrow possible
optimizations while leaving CPU frequency and cache effects open.
[Build, profiles and evidence](../rmw-agnocast/README.md#split-the-default-receive-handler-and-ros-take-path)

Two detailed steady profiles put +2.4/+3.7 µs between rcl_wait return and
entering rclcpp ready selection, and +1.0/+1.5 µs inside selection itself.
An alternating-gap placement check then found the gap even when the first
crop returned from its wait on the same logical CPU as for the preceding
frame (49.0 vs 83.3 µs stamp-to-callback). CPU migration alone cannot
explain the result. Keep the current transport and executor behavior.
The next discriminating control is a fixed-work timing probe to test sensitivity
to the interval independent of ROS code. The six batches verified 2,880
source frames at both readers. The separate original cold diagnostic passed.

## 2026-11-21 — Does the idle-gap penalty persist into ordinary callback work?

Built the first-crop fixed-work probe with sequential colcon inside the
exported Lyrical root. Compared native Agnocast and blocking rmw_agnocast A
on the branched laptop graph, using 20 and 100 ms gaps in each run. Timed
two identical arithmetic blocks, repeated with zero iterations, then
repeated the original work batch. Verified both checksums, actual source
intervals, and all 1,440 frames at each reader. The separate 10 Hz cold
diagnostic passed with 60 distinct timestamps. The lowest available host
memory was 9,867 MiB during build and 10,321 MiB during measurement.

Long gaps increased time to callback entry in every RMW round, by 7.2 to
32.8 µs. The first block's pooled median thread time changed by −0.16 µs
and +0.14 µs in the work batches. The same observed CPU subset gave the
same qualitative result. Keep A and move the diagnostic earlier, to the
return from blocking poll, where a transient wake effect could still appear.
This experiment did not change transport behavior or measure sustained
end-to-end 10 Hz parity.
[Reproduction, empty control, native comparison and raw data](../rmw-agnocast/README.md#fixed-cpu-work-at-the-first-crop-callback)

## 2026-11-21 — Close the crop-only 10 Hz investigation

Used the installed Lyrical overlays, laptop Agnocast module and guarded
colcon runner to probe the first reader immediately after its blocking poll.
The 4,096 dependent arithmetic iterations took 5.3/5.2 µs of thread time
after 20/100 ms gaps. With the work, the median interval from its end to
take was 30.9/41.5 µs (empty control 30.6/42.6). An arithmetic warm-up
left the receive-path gap. Tested one scheduling change that can apply at
wake: a process-scoped utilization minimum of 512, inherited by the whole
candidate graph. Verified the effective minimum for each application. The
timed matrix used the ordinary sensor timer and disabled tracing.

Ran two independent, interleaved native/A/candidate matrices with both
steady rates. The candidate's 10 Hz final-sink median rose from 670 to 676
µs and from 652 to 668 µs. Its pooled 10 Hz p95 and CPU improved in both
batches, while 50 Hz p95 reached 1.85 ms in the repeat. Keep blocking A
and the default scheduler. The low-rate gap remains workload evidence; a
root cause and an end-to-end fix are still open. Close this line of
microbenchmark tuning and carry A forward to multi-LiDAR synchronization.

Both readers validated 960 frames across diagnostic runs and another 2,880
across the timed matrices. The original cold diagnostic delivered all 60
distinct samples. Sequential builds used the 1,200 MiB limit and runs the
512 MiB limit. Existing archived uninstrumented ready-FD and light hop
summaries reproduced byte-for-byte with the updated analysis scripts.
[Exact commands, paired medians, p95, CPU and raw observations](../rmw-agnocast/README.md#blocking-poll-probe-and-graph-wide-scheduler-hint)

## 2026-11-21 — Enter Universe through the wrapper's ROS path

The Core wrapper's build switch controls availability of the native API.
A native-capable executable can select rclcpp at runtime with ENABLE_AGNOCAST=0.
On that path RMW_IMPLEMENTATION selects the experimental middleware. Use
the normal rclcpp container and provide the heaphook in its process environment.
The wrapper's disabled launch branch preserves existing LD_PRELOAD and does
not add the hook. All local Agnocast participants need the matching module,
device and runtime libraries.

Ran the two-crop branched smoke through the generated crop and endpoint
executables in the compiled-with-native overlay. All five processes logged
rclcpp mode. The RMW reported CDR endpoints on /input, /stage1 and /output.
Both verifying readers received 60 frames at 10 Hz. The memory-guarded run
reported 11,834 MiB minimum host memory available. This verifies middleware
selection independently of the specialized scoped wait runner. This route
serializes the point cloud and provides a functional control.

From the experimental worktree, use a fresh output directory:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
export BUFFER_RUNTIME_ONLY=1
out=$(mktemp -d build/rmw-agnocast/wrapper-entry-XXXXXX)
experiments/buffer/run-lyrical.sh timeout 65 bash -ec '
  source build/agnocast-lyrical/install/local_setup.bash
  source build/rmw-agnocast/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  python3 src/universe_crop_benchmark/run_case.py rmw-agnocast "$1/frames.csv" --branched --resources "$1/resources.csv"
' bash "$out"
```

The Universe concatenate_and_time_sync_node in this checkout registers with
rclcpp_components and uses regular rclcpp subscriptions and publishers. Start
with its normal executable, route resolved cloud topic names on both endpoints
and leave TF, motion inputs, status and services delegated to Cyclone. Verify
cloud contents and timestamps against Cyclone before enabling the host-cloud
representation and testing the scoped blocking wait on this executor. Its
default subscription options, output allocation and loan lifetimes need
separate verification for a zero-copy claim. Keep native-wrapper A/B on a
component that actually implements the native API.
[Smoke observations](../rmw-agnocast/wrapper-entry-results/)

## 2026-11-21 — Integrate the Universe three-LiDAR synchronizer

Built the real CPU concatenate_and_time_sync_node sources and rclcpp component
registration in an isolated colcon package. The laptop runs its exported
Lyrical userspace and Agnocast device. The build uses one worker, the
1,200 MiB build limit and ENABLE_AGNOCAST=0. Use the existing patched
rclcpp/RMW overlay and heaphook for the RMW cases. Checked the resolved
native input/output endpoints, validated delegated output_info and static
TF, and left bridging off in every mode.

Two source changes were necessary. The initial compile failed at the removed
message_filters/pass_through.h. Removed five unused includes from the public
synchronizer header in 0a4bcd6e. The Bazel snapshot and inspected AWF main
still carried them. On the executable's first functional runs all groups
were valid, followed by SIGSEGV at shutdown in both RMW layouts. One
diagnostic rerun reported a double free. The node stored shared ownership of
the collectors and each collector stored shared ownership of the node.
Changed that back-reference to weak ownership and retained the parent only
while executing its callbacks (a61d55cf). Subsequent runs checked the child
exit statuses as well as application output. This isolates the observed
failure to a real graph lifecycle path and does not require a kernel patch.
The CPU component is the tested instantiation of the shared collector template.
[Inspected AWF source](https://github.com/autowarefoundation/autoware_universe/tree/8361f0ef2dd4558864f0d00d8bfb9e036c7102af/sensing/autoware_pointcloud_preprocessor/include/autoware/pointcloud_preprocessor/concatenate_data)

The first scoped-run launcher inadvertently preloaded the hook into an
intermediate env executable. Its re-exec under the same PID produced an
Agnocast add-process error before discovery. Pass all settings in one env
invocation so only the actual process receives the hook. In the scoped
test, enter the fixed wait after the first cloud, once the node has created
its collectors. Verify native wake and delegated completion counters and
application progress through both complete and timed-out groups.

Use the build and matrix commands in the linked README on a fresh output
directory. The separate C++ fixture verifies each segment of every cloud
against known XYZIRC points and frame translations. Matched rates are
10/10/5 Hz with 1 ms inter-sensor timestamp offsets. Each timed run uses 60
half-MiB input clouds, producing 12 complete outputs and 12 intentional
partial outputs. Control modes are Cyclone and Zenoh. The candidate modes
use CDR, copying host-cloud, and copying host-cloud plus blocking A. Native
Agnocast requires a separate native data path for this rclcpp-only node.
Ordinary subscriptions do not opt into the loan-backed host view. The
next integration decision is where the actual synchronizer can consume that
view and where constructing transformed or concatenated data requires a new
output allocation.
[Reproduction, comparison and raw evidence](../rmw-agnocast/README.md#universe-multi-lidar-concatenation-and-synchronization)

Final passes verified 240 matched cloud/metadata pairs from 600 inputs.
Blocking A's complete-group medians were 4.38/5.15 ms and its intentional
partial-group medians were 42.15/42.44 ms. Both runs reported 57 native
wakes and 24 delegated completions. All ten graph invocations passed the
payload, TF, timeout and teardown checks. No consistent complete-group
median advantage over ordinary host-cloud was established.

## 2026-11-21 — Read the synchronizer inputs from retained Agnocast loans

Kept the experimental adapter in universe_sync_benchmark. The fixture applies
loan-input.patch to two build-directory copies when enabled and directly
compiles the tracked Universe sources when disabled. Compared the Universe
subtree against 1c78effe, which already includes the Lyrical header and
collector lifetime fixes. Keep direct Universe edits for demonstrated fixes.

The ordinary rclcpp subscriptions previously requested CPU buffers. The
RMW now accepts its named host-view backend in the Lyrical subscription
options and exports the header via ament. The three experimental subscriptions
request that backend. The consumer reads the retained vector at conversion,
after the collector has waited for its peers or for the 40 ms timer. A
matched CPU-copy control performs the identical XYZIRC byte conversion
under the same blocking A wait. Packed rows take a bulk copy into canonical
XYZIRC output. The padded-input test additionally exercises per-point stride
and skips end-of-row padding. Keep transformation and output ownership
identical in both cases.

Use the guarded build and run commands in the linked README. For the
unadapted colcon control run build-sync.sh under the same guarded runner
with AUTOWARE_ENABLE_AGNOCAST_HOST_VIEW=OFF and run the Cyclone graph before
rebuilding the enabled case. Build with one worker under the 1,200 MiB cap,
then run graphs one at a time under the 512 MiB cap. The pre-existing host-view
probe also passed address identity, retention, independent mutation, final
release, and the empty and padded cloud checks. The RMW-to-CPU input copy
and the algorithm's output construction are separate costs. The experiment
does not establish native Agnocast parity.
[Commands, per-run evidence and latency comparison](../rmw-agnocast/README.md#read-a-retained-input-loan-in-the-universe-synchronizer)

In the final isolated build all 22 timed graphs exited cleanly and verified
528 output groups and their metadata. The four view runs took all 240 input
clouds as retained views, and the four matched copying runs took all 240 as
CPU buffers. Complete-group median fell in all four pairs, from 3.89–4.16 ms
to 2.60–2.88 ms. Partial-group latency remained dominated by the 40 ms
collector timeout. The padded audit verified all 60 owner references expired
at teardown. Use the view for further experiments and keep the Universe
subtree at its last verified fixes while studying the output publication path.

## 2026-11-21 — One Universe CUDA transform edge on the laptop

The question was whether a GPU-resident LiDAR edge needs CUDA support in
rmw_agnocast before Autoware nodes can use standard PointCloud2. The colcon
fixture links the unchanged Universe concatenate transform and the production
blackboard publisher, subscriber, registry, and allocator sources. Two test
nodes call the kernel. A writer translates X by the frame sequence directly
into transport-owned GPU storage. A GPU reader adds three on a second stream.
The verifier checks every XYZIRC field of all 32,768 points after each frame.
An independent CPU observer in the mixed topology checks the writer's result.
The control exercises blackboard negotiation and asserts device address
identity. The composed rclcpp intra-process Buffer case also asserts identity.
The split case checks the CUDA backend type, imports its input and runs the
consumer kernel. Address equality across processes is not meaningful.

Use the exported Lyrical root and the CUDA Buffer overlay described in the
README. This laptop has an RTX 5070 Laptop GPU, driver 595.99.02 and CUDA 13.0.
The image's glibc math headers and nvcc disagreed on the exception specifications
of rsqrt and rsqrtf. The build script compiles just the unchanged .cu object on
the laptop with GCC 13.3, then builds the ROS C++ endpoints and the blackboard
sources with colcon in Lyrical. It leaves the toolkit and the Universe tree
intact. On another GPU select its architecture with GPU_ARCH. Run from the
experiment worktree, with the guard enabled throughout:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
bash src/universe_gpu_edge_benchmark/build.sh
export BUFFER_RUNTIME_ONLY=1 ROS_DOMAIN_ID=183
for mode in blackboard intra composed split mixed mixed split composed intra blackboard; do
  experiments/buffer/run-lyrical.sh bash src/universe_gpu_edge_benchmark/run.sh "$mode" || exit 1
done
```

All cases select rmw_zenoh_cpp. The blackboard uses its negotiated intra-process
notification. The Buffer cases exercise the rclcpp intra-process path, composed
nodes with that optimization disabled, and separate processes on one host. In
the mixed case only the CPU subscriber asks for CPU storage. Each graph sends
24 ordered 512 KiB clouds at 10 Hz. Timing starts before message construction
and ends after the consumer kernel completes, before verification readback.
CPU observer timing ends at callback access before the verification copy.
The p50 and p95 are nearest ranks 10 and 19 of the last 20 frames. Scheduling,
allocation and stream synchronization are included. The blackboard producer
synchronizes its stream before publish as required by this API. The Buffer
writer releases its output handle to record readiness. GPU clocks were not fixed.

Both orders passed. All 240 GPU deliveries and all 48 additional CPU
deliveries matched. GPU-only split median moved from 0.57 to 3.33 ms.
With a CPU observer the GPU median moved from 4.40 to 1.05 ms. The
intra-process Buffer median crossed the blackboard median between rounds.
The measurements establish interoperability and composed device ownership,
with insufficient repeatability for a latency ranking. The ordinary CPU
fan-out can stage payload bytes. Timings do not isolate IPC cost.

Close this one-edge milestone and resume the Universe CPU synchronizer
publication experiment. Keep the CUDA Buffer backend independent of
rmw_agnocast. Reopen native CUDA descriptor forwarding only for a deployment
requiring same-host split GPU nodes under that RMW. Qualify ownership from
enqueue through delayed take, allocation reuse, dropped samples and endpoint
teardown before relying on that route. This fixture does not run the entire
Universe GPU component or compare Agnocast and Cyclone GPU transports.
[Two-round results](../../src/universe_gpu_edge_benchmark/results.csv)

## 2026-11-21 — Move the CPU synchronizer's finished output into a publisher loan

Followed the Universe synchronizer's final unique-pointer publish through
rclcpp to the host-cloud RMW's const-message publish. That route constructs
HostCloud by copying the output vector. Tested a short native ROS publisher
loan at the same call site. A negative control moved a privately allocated
message into the loan. The candidate also reserved the completed output's
point storage in the shared writer pool before the existing append loop.
Kept the input view, blocking executor, TF and component code identical
across the three RMW cases. The edits apply only to the fixture's build
copies. The tracked Universe and RMW implementations required no change.

The private-loan control reported 24 publisher loans and 24 private copies.
The shared-output case reported 24 loans and zero private copies. Both
validated every point, TF and source metadata on padded input, including
the 12 intentional timeout groups. Both observed release of all 60 retained
input owners. At the packed test load the writer eliminates 30 MiB of
publication payload copies per 24 groups. The concatenation append and the
ordinary CPU subscriber's receive copy still run.

An exploratory batch verified the payloads but encountered a shell parse
error at teardown after the running launcher was edited. Discarded that
batch. Rebuilt the fixture and reran the full matrix with the launcher held
fixed. Timing runs use no stage timestamps. Separate diagnostic runs
measure reserve, append and publication on the same blocking executor.
[Reproduction and measurements](../rmw-agnocast/README.md#transfer-the-universe-synchronizer-output-through-a-publisher-loan).

Four paired latency runs favored ordinary output publication. The shared
route eliminated the final writer copy but added 1.03–1.49 ms to the median
for complete groups. Two stage profiles in reverse order and four paired
writer/sink trace graphs put the larger delay before entry to the RMW
publish call. Keep ordinary publication as the reference while splitting
input arrival and collector scheduling from conversion and transformation.
The initial trace launcher inadvertently preloaded the heap hook in an
intermediate env executable and process registration failed. Moving the
trace environment assignment before the preloaded child restored clean
startup. The four archived trace runs each matched all 24 writer and reader
timestamps. Across the final 14 timing graphs, six writer profiles, four
paired trace graphs and two padded audits, the sink verified 624 clouds and
their corresponding metadata.

## 2026-11-21 — Native Agnocast on the three-LiDAR synchronizer

Built a native comparator in a separate colcon prefix. Selecting the native
build applies a fixture-only transport patch and links the installed Lyrical
Agnocast library and mixed CIE. The native point-cloud topics carry typed
PointCloud2 loans. The ordinary ROS metadata and TF paths and the 40 ms ROS
collector timer are present on both sides of the comparison. Timings include
source publication and sink dispatch. The native sink retains the output
loan while the ordinary RMW sink receives a CPU message. Keep this distinction
when interpreting end-to-end results and comparing them with the crop graph.

The copied node header uses relative sibling includes, so the isolated native
target needs the original concatenate_data header directory as a fallback.
The first functional native graph verified all 24 outputs, but the last
collector loans outlived the native input subscriptions. At teardown the
kernel rejected a subscriber reference release. In the native build copy,
declare subscriptions before the collector list so the collectors are
destroyed first. Release sink loans before destroying the output subscription.
On padded input the thread-local audit ran when the CIE worker stopped and
saw two inputs still held. A separate process-lifetime weak-owner audit
observes release after component destruction. The native wire uses CPU
vectors directly. The local input facade allows the unmodified collector to
call Lyrical Buffer methods safely.
[Build, graph design and validation](../rmw-agnocast/README.md#native-agnocast-comparator-for-the-three-lidar-synchronizer).

All 12 final timing graphs exited cleanly and verified 288 output clouds and
metadata records. The additional padded native audit verified 24 outputs and
all 60 retained input owners expired after component teardown. Complete-group
rmw_agnocast/native median and p95 were 0.974× and 1.271×, respectively.
Partial-group median was 41.625 ms for the RMW and 52.048 ms for the native
mixed CIE. The comparison supplies a direct native baseline for this graph.
Keep the retained-input implementation and defer further tuning. All runs
used one guarded graph at a time, 512 MiB runtime and 1,200 MiB build caps.

## 2026-11-21 — Launch three Universe crops and a synchronizer with runtime RMW selection

Built the Universe crop component directly from the tracked source with
ENABLE_AGNOCAST=0 and the crop fixture's Buffer patches disabled.
The older endpoint executable in the crop fixture initially failed to build:
it called allocate_output_message_unique directly on an ordinary rclcpp
publisher. Switched that fixture call to the existing wrapper macro, which
selects the correct allocator in either build. The crop implementation required
no change. Built two synchronizer prefixes, one from the original source and
one with the previously measured input view. The generated sensing type-support
libraries reside in the earlier Universe sync overlay; sourcing that overlay at
runtime resolved the initial component/fixture loader failure.

ROS launch loads all three crop plugins into the standard container. The
separate synchronizer process consumes the three cropped topics; a separate
consumer verifies the concatenated payload and metadata using the original
source stamps. Each crop retains a different 16,384-point range, so a swapped,
missing or bypassed filter can be detected in the final data. Dynamic load
services, TF and metadata remain on ordinary ROS. Inspect each rmw_agnocast
launch for a host-cloud reader and writer on each of the seven cloud topics
and check all five application processes terminate cleanly. Installed
build-marker, configured sources, commands and checks are recorded with
the results. Use the two unmodified component implementations as the
runtime-selection control; compare ordinary and scoped-runner execution
with the identical adapted component binary.
[Reproduction and deployment details](../rmw-agnocast/README.md#launch-three-universe-crops-into-the-three-lidar-synchronizer).

Ten timing launches verified all 240 synchronized outputs and metadata
records, with three successful dynamic component loads and five clean
application exits per launch. The ordinary unadapted rmw_agnocast graph
passed using normal ROS publishing, subscriptions and launch. With the
retained-input adapter the complete-group median/p95 were 2.404/3.359 ms
under stock launch and 2.407/3.132 ms under the scoped runner. Keep stock
launch for the next representative workload and retain the runner for
diagnostics. The timing run minimum host MemAvailable was 11,695 MiB.

An additional stock launch passed after adding explicit launch failure
propagation for the verifier. Rebuilt and reran the original synchronizer-only
fixture in retained-input and native Agnocast modes. Both verified all 24
groups with 60 retained inputs and clean shutdown. The native regression uses
the shorter graph and is excluded from the launch timing table.

### 2026-11-21 — Top LiDAR arrives on either side of the synchronizer timeout

Used the preceding installed three-crop Universe graph, laptop and 40 ms naive
synchronizer timeout. Changed only the source and sink fixture. Each 100 ms
group retains its acquisition stamps at 0/1/2 ms for left/right/top. On
alternate top publications the source waits until 15 or 50 ms from group
start. It logs when it enters publish, upstream of the crop. Odd groups still
have no top sample. The sink checks membership, source stamps, transformed
points and timeout metadata, including a top-only output if the pair has
already timed out. No Universe implementation changes were needed.

From the worktree with the previous milestone's overlays installed:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash -ec '
  source build/agnocast-lyrical/install/local_setup.bash
  source build/universe-sync/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  cmake --build build/universe-pipeline/obj/universe_sync_benchmark --target sync_fixture -j1
  cmake --install build/universe-pipeline/obj/universe_sync_benchmark'
bash experiments/rmw-agnocast/bench-arrival.sh build/universe-pipeline/new-arrival-pairs
SYNC_AUDIT=1 SYNC_TOP_JITTER=1 BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh \
  timeout -k 5 65 bash experiments/rmw-agnocast/run-pipeline.sh stock
```

Ran two rounds in reverse order, sequentially, with 512 MiB runtime scopes.
All 12 launches verified every cloud and metadata record. The 36 top inputs
released outside the timeout each produced a separate top-only output.
The independent stock audit recorded 60 loans, 60 releases and 15,728,640
bytes. Runtime peak memory across scopes was 329.67 MiB and minimum host
MemAvailable was 16,033 MiB. The [results and interpretation](../rmw-agnocast/README.md#top-lidar-arrival-jitter)
apply to this laptop, matcher and schedule. Next isolate slow-consumer
behavior with nominal sensor timing before combining both conditions.

### 2026-11-21 — Slow output consumer at nominal LiDAR timing

The question was whether a busy consumer changes synchronization, holds
Agnocast storage, or loses data before it can drain its queue. Kept the
previous launched graph, crop ranges, stock executors, 24 acquisition groups
and 10/10/5 Hz rates. Set SYNC_SLOW_SINK to make the existing single-threaded
sink sleep for 125 ms on every cloud callback, immediately after recording
entry latency. Metadata callbacks share the executor. The subscription depth
is 32. The baseline uses the same binary with the flag unset. Both conditions
retain delivered CPU cloud objects in the verifier until it checks every
transformed point, sensor membership, timestamp and metadata record.

On the laptop, reuse the previous milestone's working Lyrical root, kernel
module and colcon overlays. Build the fixture and experimental RMW sequentially:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash -ec '
  source build/agnocast-lyrical/install/local_setup.bash
  source build/universe-sync/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  cmake --build build/universe-pipeline/obj/universe_sync_benchmark --target sync_fixture -j1
  cmake --install build/universe-pipeline/obj/universe_sync_benchmark'
experiments/buffer/run-lyrical.sh bash -ec '
  source build/agnocast-lyrical/install/local_setup.bash
  colcon build --base-paths src/rmw_agnocast --build-base build/rmw-agnocast/load-obj \
    --install-base build/rmw-agnocast/load-install --executor sequential'
bash experiments/rmw-agnocast/bench-arrival.sh build/universe-pipeline/new-slow-pairs slow
```

A direct CMake install of rmw_agnocast overwrote the colcon library-path hook
registration. The first baseline failed to load librmw_agnocast.so. Installing
through colcon restored the hook. All timed trials used the repaired overlay.

For separate diagnostics, enable the existing synchronizer input audit and
send the sink's host-cloud handoff trace to a fresh file per launch. The
copying sink releases each pending loan when it copies the output and pops
the queue. The added trace timestamp is taken immediately after that pop.
Measure the interval from reader admission to that point, including copying
and scheduling. The sink's later sleep holds its local copy. These timestamps
do not measure the publisher's complete allocation lifetime.

```bash
mkdir -p build/universe-pipeline/slow-traces
for condition in baseline slow; do
  unset SYNC_SLOW_SINK SYNC_TOP_JITTER
  if [[ $condition == slow ]]; then export SYNC_SLOW_SINK=1; fi
  SYNC_AUDIT=1 SYNC_SINK_TRACE="/ws/build/universe-pipeline/slow-traces/$condition.trace" \
    BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout -k 5 65 \
    bash experiments/rmw-agnocast/run-pipeline.sh stock
done
python3 experiments/rmw-agnocast/summarize-queue.py build/universe-pipeline/slow-traces/*.trace
```

All 12 timing launches passed. The 720 raw clouds yielded 288 verified
outputs, each paired with metadata. The slow consumer drained all queued
clouds after the finite source stopped. Peak concurrent reader loans in the
separate stock traces rose from one to five. Both audits released all 60
synchronizer input views. The largest timed scope peak was 327.89 MiB and
minimum host MemAvailable was 14,174 MiB. Compare the two rounds per mode
and the queue lifetime evidence in the [results](../rmw-agnocast/README.md#slow-downstream-consumer).
Next combine the tested arrival jitter and slow callback and check that the
late top-only outputs and their metadata survive the larger backlog.

### 2026-11-21 — Combined top-LiDAR jitter and slow output callback

Reused the installed Lyrical overlay and the three-crop Universe graph from
the slow-consumer milestone. All timing launches used the same acquisition
stamps and 15/50 ms alternating top-LiDAR release schedule. The control sink
returned immediately. The combined sink slept for 125 ms on each output
cloud callback after recording its entry time. We paired those conditions so
both sinks would see the same expected sequence of 30 clouds. Alternating
middleware and condition order in the second round reduced run-order bias.

With the preceding milestone's binaries and kernel module installed, run
from the colcon worktree. The runner starts one 512 MiB, swap-disabled scope
at a time and monitors host memory. The two extra stock launches enable
tracing separately from the timed runs.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
base=build/universe-pipeline/combined-pairs
bash experiments/rmw-agnocast/bench-arrival.sh "$base" combined
for condition in jitter combined; do
  unset SYNC_SLOW_SINK
  if [[ $condition == combined ]]; then export SYNC_SLOW_SINK=1; fi
  SYNC_AUDIT=1 SYNC_TOP_JITTER=1 SYNC_SINK_TRACE="/ws/$base/$condition.trace" \
    BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout -k 5 65 \
    bash experiments/rmw-agnocast/run-pipeline.sh stock >"$base/$condition.audit.log" 2>&1
done
python3 experiments/rmw-agnocast/summarize-queue.py --outputs 30 "$base"/*.trace
```

All 12 timed launches verified 360 clouds and their metadata from 720 raw
inputs. In the six combined launches, each of the 36 top clouds released
outside the matching window formed a separate output with its original stamp.
All six last callbacks started 1.358–1.366 s after acquisition. The slow-only
reference had six fewer callbacks and finished its last callback entry at
0.605–0.614 s. The two separate stock audits each released 60 input views
and accounted for 30 output loans. Minimum timed host MemAvailable was
14,358 MiB. See the [latency and queue results](../rmw-agnocast/README.md#combined-lidar-jitter-and-slow-consumer).
The depth-32 queue absorbed this burst; sustained overload beyond that depth
and native Agnocast on this complete graph remain open.

### 2026-11-21 — Overflow and recovery of the depth-32 Universe output reader

Kept the combined milestone's three crops, synchronizer, 40 ms matcher
timeout, 10/10/5 Hz input, alternating 15/50 ms top release offsets, and
separate cloud and metadata topics. Extended the fixture to 104 acquisitions
(260 raw clouds and 130 expected outputs). Both long-run conditions prepare
one acquisition's inputs before its scheduled start and free them after
publication to bound source memory. The synthetic Y coordinate cycles within
the existing crop bounds; point intensity still identifies the acquisition.
In the overload condition, the single-threaded sink sleeps for 125 ms on
each of its first 68 cloud callbacks. It records the clock at the end of the
last sleep and resumes normal processing while the sensors continue. The
control runs the identical binary and traffic with a fast sink throughout.

The verifier records cloud and metadata presence independently for every
expected output stamp. It checks every surviving cloud's sensor segments
and transformed bytes, every surviving metadata record's membership/offsets,
and agreement for matched pairs. The last 12 acquisition groups must all
arrive paired. The analysis checks duplicates, the jitter pattern, source
rate and a preselected freshness criterion: time since the last sleep until
the first callback after which every remaining cloud is under 100 ms old at
callback entry. That includes normal matcher waiting. One extra stock run
traces output evictions at the reader and input loan release independently
of the timed runs. No production queue policy changes.

Reproduce from the preceding milestone's installed laptop overlays. The guard
caps builds at 1200 MiB and each sequential launch at 512 MiB with swap disabled.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash -ec '
  source build/agnocast-lyrical/install/local_setup.bash
  source build/universe-sync/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  cmake --build build/universe-pipeline/obj/universe_sync_benchmark --target sync_fixture -j1
  cmake --install build/universe-pipeline/obj/universe_sync_benchmark
  colcon build --base-paths src/rmw_agnocast --build-base build/rmw-agnocast/load-obj \
    --install-base build/rmw-agnocast/load-install --executor sequential'
mkdir -p build/universe-pipeline/overflow
bash experiments/rmw-agnocast/bench-arrival.sh build/universe-pipeline/overflow/timing overflow
```

Two opposite-order rounds completed for all three middleware implementations.
All six fast controls verified 130 pairs. The six overload runs yielded
738 clouds and 780 metadata
records out of 780 of each, with every recovered tail paired and fresh.
The separate stock audit matched every evicted stamp to a missing cloud.
All 260 synchronizer input loans were released. The original 24-group stock
baseline and combined fixtures also passed after updating the verifier.
[Timing, resource usage and exact loss records](../rmw-agnocast/README.md#depth-32-reader-overflow-and-recovery).
