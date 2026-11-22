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

### 2026-11-22 — Native Agnocast on the three-crop Universe graph

The earlier native synchronizer test omitted the crops. Reused the previous
five-process graph, input generator, distinct crop ranges, static transform,
synchronizer, verifier and sensor QoS. For the native case, build the wrapper
crop with ENABLE_AGNOCAST=1 and the existing native synchronizer adapter
with SYNC_NATIVE=ON in a separate overlay. Keep UNIVERSE_BUFFER_MATRIX=OFF
for both crop builds. The native crop host loads the same three component
classes into one Agnocast-only single-threaded executor, using the launch
file's common bounds, frames and remappings. The normal ROS host uses its
standard component container. Native synchronization and the sink use the
bundled mixed-capable CIE, with cloud topics on Agnocast and metadata and
TF on ROS. The sink materializes a local CPU payload before timestamping
callback entry, matching the copying RMW sink. The seven native cloud
routes must each report an actual reader and writer from the kernel graph
with bridges disabled. The fixture checks the transformed bytes, group
membership, acquisition stamps, metadata and recovery as in the preceding
experiment. This measures the deployed stacks, including executor costs.

Reproduce on the laptop with the preceding milestone installed:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash -ec '
  source build/agnocast-lyrical/install/local_setup.bash
  source build/universe-sync/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  export ENABLE_AGNOCAST=1 CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21
  colcon build --base-paths src/universe_crop_benchmark src/universe_sync_benchmark \
    --build-base build/universe-pipeline-native/obj --install-base build/universe-pipeline-native/install \
    --executor sequential --cmake-args -DSYNC_NATIVE=ON -DUNIVERSE_BUFFER_MATRIX=OFF \
    -DAUTOWARE_ENABLE_AGNOCAST_HOST_VIEW=ON -DBUILD_TESTING=OFF \
    "-DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15" \
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG"'
```

Rebuild the stock fixture from the same checkout with colcon and its existing
stock cache. A direct CMake install during diagnosis replaced the generated
package.dsv and removed the library-path hook. In that state an older
synchronizer library was selected and native endpoint discovery failed.
Restoring both hooks with colcon fixed discovery. Exclude those setup
attempts from timing. For reuse, preserve the colcon-generated environment
with the following commands.

```bash
experiments/buffer/run-lyrical.sh bash -ec '
  source build/agnocast-lyrical/install/local_setup.bash
  source build/universe-sync/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  export ENABLE_AGNOCAST=0
  colcon build --base-paths src/universe_sync_benchmark \
    --build-base build/universe-pipeline/obj --install-base build/universe-pipeline/install \
    --executor sequential'
bash experiments/rmw-agnocast/bench-native-graph.sh build/universe-pipeline-native/pairs
bash experiments/rmw-agnocast/bench-native-graph.sh build/universe-pipeline-native/pairs/confirmation confirmation
python3 experiments/rmw-agnocast/summarize-native-graph.py build/universe-pipeline-native/pairs
```

For the native executor A/B, set SYNC_EXTENDED=1, SYNC_TOP_JITTER=1,
SYNC_SLOW_SINK=1, SYNC_AUDIT=1 and BUFFER_RUNTIME_ONLY=1. Run
`experiments/buffer/run-lyrical.sh timeout -k 5 65 bash experiments/rmw-agnocast/run-pipeline.sh native`
once with SYNC_SHORT_WAIT unset and again with SYNC_SHORT_WAIT=1. Only the
bundled mixed CIE's constructor wait changes (50 or 1 ms, in the native
synchronizer and sink). The sensor offsets, matcher timeout, crop executor,
queue depth and slow callback are identical.

The native default-wait run with working native discovery reproduced a
semantic difference at group 2. Its left/right-stamped output contained
49,152 points, a full three-sensor cloud, even though the 50 ms top
publication was outside the 40 ms matching window. This fails the
configured-timeout expectation. The native nominal run passed with the
same default. With a 1 ms wait, the matched overload run passed the
membership checks. Mixed CIE groups invoke an Agnocast timed wait before
servicing a ROS callback; both the synchronizer and sink emitted the mixed
group warning. This A/B identifies sensitivity to the native executor
configuration. It does not isolate the precise timer dispatch time.

The separate 1 ms audit observed 260 retained synchronizer inputs. The
per-thread conversion audit saw 258 expired views at its own teardown;
the final native owner audit saw 260 of 260 released. Keep both counters
and their observation points in the record. The RMW comparison uses the
stock executor throughout. With slow callbacks, compare usable pairs and
maximum age together: earlier recovery can follow more aggressive loss.

All 26 measured launches passed their respective checks. Six alternating
nominal launches per primary implementation supplied 60 post-warm-up
complete-cloud samples each. Cyclone DDS, Zenoh and native default-wait
nominal controls contributed two launches per mode. Under overload both
timed 1 ms native runs delivered 57 matched pairs, with 96 clouds and 72
metadata records. Both stock RMW runs delivered 123 pairs, with 123 clouds
and 130 metadata records. All implementations paired the last 12 groups.
The [comparison table and per-launch measurements](../rmw-agnocast/README.md#native-agnocast-on-the-full-three-lidar-processing-graph)
record the engineering milestone and resource ranges.

### 2026-11-22 — Two downstream region filters on the synchronized cloud

Extended the five-process, three-LiDAR Universe graph with two separate
Universe CropBoxFilterComponent executables in the base_link frame. Both
subscribe to the entire synchronized /output, alongside the original sink,
and publish /region/near and /region/far. Each executable scans every point.
The synthetic regions keep X in [12000,18000) and [16000,23000) respectively.
These ranges are chosen to verify different overlapping subsets of the test
coordinates. They do not represent production vehicle distances. The sink
compares each region, in order, against the independently filtered bytes of
the already verified synchronized cloud. It checks frame, stamp, fields, row
stride, point count, and delivery of both outputs for every acquisition.
The end-to-end measurement is the later of the two region callback entry
times measured against the oldest contributing acquisition stamp. Data
validation runs after collection. The one verifier consumes three cloud
streams and metadata; its scheduling and receiver copies are included.

Reuse the preceding milestone's Lyrical overlays and Agnocast module. Rebuild
only the verification package, using each overlay's cached colcon configuration.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash -ec '
  source build/agnocast-lyrical/install/local_setup.bash
  source build/universe-sync/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  for ws in universe-pipeline universe-pipeline-native; do
    if [[ $ws == universe-pipeline-native ]]; then export ENABLE_AGNOCAST=1; else export ENABLE_AGNOCAST=0; fi
    colcon build --base-paths src/universe_sync_benchmark --build-base "build/$ws/obj" \
      --install-base "build/$ws/install" --executor sequential
  done'
mkdir -p build/universe-pipeline-regions
out=build/universe-pipeline-regions/pairs
bash experiments/rmw-agnocast/bench-native-graph.sh "$out" regions
bash experiments/rmw-agnocast/bench-native-graph.sh "$out/double-rate" regions-fast
python3 experiments/rmw-agnocast/summarize-regions.py "$out"
```

Use the native mixed-executor wait of 1 ms established by the preceding
native comparison. Native discovery must see one writer and three readers
on /output and one reader and writer on each region topic. Ordinary ROS
and native use the same crop implementations. The timed runner reverses
order, checks actual source cadence, and runs a single graph per 512 MiB
scope with swap disabled and host memory monitoring. Keep all cloud and
metadata streams local and the bridge off. The 20/20/10 Hz condition changes
only the inter-acquisition interval from 100 to 50 ms. Jitter is tested
separately at 10/10/5 Hz, using the previous 15/50 ms top release pattern.

All 48 timed launches passed, verifying 1,200 fused clouds and metadata
records plus 2,016 filtered outputs. The joint region completion stock/native
median / p95 ratio was 1.01× / 0.92× at 10 Hz and 1.01× / 0.90× at 20 Hz.
Peak scope memory across the timed runs was 471.3 MiB and minimum host
MemAvailable was 9,408 MiB. [Per-middleware results](../rmw-agnocast/README.md#two-region-filters-downstream-of-three-lidar-synchronization).

## 2026-11-22 — Overloading one downstream region crop

Can one slow region consumer delay the other or stop upstream fusion? Reuse
the preceding seven-process local graph, with left/right/top at 20/20/10 Hz,
32 acquisition groups per launch, 32,768 points per input and the 40 ms
synchronizer timeout. Build both the ordinary ROS and native Agnocast
versions. Keep the native mixed executor's 1 ms wait and disable bridges.

The launch sets BENCH_STALL_THIS_CROP only in the near region process for
the impaired condition. A build-directory copy of the Universe crop sleeps
150 ms inside each of its fifth through tenth filter callbacks and logs
the beginning and end using the same ROS system clock as the source.
This occupies the callback worker while using little CPU. The far crop,
synchronizer and shared verification process keep running. Both conditions
use identical binaries. The verifier matches each returned region by source
stamp and checks every selected point against the fused payload. It requires
all fused clouds, metadata and far outputs, and the first four and final six
near outputs. Missing intermediate near stamps are recorded individually.

From the preceding colcon milestone, with the matching host kernel module
loaded, rebuild the two benchmark packages in each existing overlay:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash -ec '
  source build/agnocast-lyrical/install/local_setup.bash
  source build/universe-sync/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  for ws in universe-pipeline universe-pipeline-native; do
    if [[ $ws == universe-pipeline-native ]]; then export ENABLE_AGNOCAST=1; else export ENABLE_AGNOCAST=0; fi
    colcon build --base-paths src/universe_sync_benchmark src/universe_crop_benchmark \
      --build-base "build/$ws/obj" --install-base "build/$ws/install" --executor sequential
  done'
mkdir -p build/universe-isolation
out=build/universe-isolation/pairs
bash experiments/rmw-agnocast/bench-native-graph.sh "$out" isolation
bash experiments/rmw-agnocast/bench-native-graph.sh "$out-repeat" isolation-repeat
cp "$out-repeat"/*.log "$out/"
python3 experiments/rmw-agnocast/summarize-isolation.py "$out"
```

The result directory must be fresh. Each paired launch changes only the
near stall, alternating order between rounds. For each impaired run, select
complete acquisitions whose source timestamps fall inside its measured
stall interval. Use those same sequence numbers from the paired healthy
launch to calculate the far branch's median and nearest-rank p95. This
selection includes slow arrivals even if they arrive after the stall ends.
The summary also reports the timed-out two-sensor acquisitions separately.
Recovery is the time from the final sleep ending to the first subsequent
near delivery after which all observed near deliveries are under 100 ms
old. Source timestamps identify losses independently of this freshness metric.

All 40 timed launches passed, as did a separate stock regression launch
at 20/20/10 Hz with the previous 24-group fixture. The first four pairs
put the impaired rmw_agnocast far p95 at 7.82 ms. Four additional pairs
with the same settings put the pooled estimate at 7.10 ms. The original
fourth impaired run was slow before, during and after the near stall.
All eight native and eight rmw_agnocast impaired runs reproduced their
respective near delivery patterns. Peak runtime scope memory was 444.9 MiB,
minimum runtime host MemAvailable was 9,188 MiB and the sequential colcon
builds reported at least 8,144 MiB available. See the
[comparison and interpretation](../rmw-agnocast/README.md#isolation-of-the-healthy-crop-when-its-sibling-stalls).

## 2026-11-22 — Fresh observations during CPU-busy and sleeping crop callbacks

Investigate whether the extra two near-region losses cost a consumer fresh
observations. Reanalyse the preceding 40 launches and run a separate
paired experiment on the same laptop, Lyrical overlays, kernel module and
seven-process graph. Select the same six near callbacks for 150 ms of work
each. With BENCH_BUSY_NEAR set, their benchmark-only hook repeatedly reads
the steady clock until each wall-time deadline. Measure thread CPU time
around each interval with CLOCK_THREAD_CPUTIME_ID. Ordinary control runs
leave the stall hook inactive. The scheduler can use all normally available
cores. This models a worker consuming one core, without forcing it to share
a core with the far branch. The Universe source snapshot is unchanged.

The verifier is the consumer. It accepts both complete and partial fused
groups. There is no detector or tracker deadline configured for these
synthetic region topics. Use 100 ms as an illustrative budget shared with
the previous recovery check. At each arrival, infer callback time from
source stamp plus logged age. A fresh delivery has age at most the budget.
Measure the longest interval between consecutive fresh deliveries spanning
any part of the stall, including the flanking deliveries. Separately, at
each instant the latest available observation is the newest acquisition
stamp received so far. Its age increases between arrivals. Integrate the
time that age exceeds the budget over the measured stall and record the
maximum just before an arrival or at stall end. Missing outputs never
refresh this clock. Align each control window to its own first acquisition.

Repeat the preceding journal entry's guarded colcon build command with
`--base-paths src/universe_crop_benchmark` in both cached overlays. The
existing source, sink, launch and synchronizer binaries need no changes.
With LYRICAL_ROOT exported as above, run:

```bash
report=experiments/rmw-agnocast
out=build/universe-isolation/busy
BENCH_BUSY_NEAR=1 bash "$report/bench-native-graph.sh" "$out" isolation
python3 "$report/summarize-isolation.py" "$out" --busy --freshness-ms 100
python3 "$report/summarize-isolation.py" "$report/pipeline-isolation-results" --freshness-ms 100
```

The runner alternates condition order and uses a fresh bounded scope per
launch. Each impaired run must contain six thread CPU readings. The
analyzer retains the previous route, payload verification marker, cadence
and shutdown checks. To evaluate a different downstream budget, supply it
in milliseconds. Each gap measurement requires a qualifying delivery on
both sides of the interval.

Both stock and native crop builds passed sequentially with at least 9,229 MiB
host memory available. All 24 runtime launches passed. Peak scope memory was
487.6 MiB under the 512 MiB limit, and minimum host MemAvailable was
8,410 MiB. Both impaired Agnocast stacks went about 915 ms between fresh
near arrivals, and the far branch received every expected cloud. Replaying
the archived sleep logs with the original analyzer arguments reproduced
the original summary exactly. A synthetic out-of-order arrival check
confirmed that an older arrival does not reset the latest observation age.
[CPU and freshness results](../rmw-agnocast/README.md#freshness-of-the-near-region-under-overload).

## 2026-11-22 — Replay near-cloud losses through Universe Euclidean clustering

Determine whether the extra near-cloud losses can change which objects
a real perception consumer detects. Build the unmodified EuclideanClusterNode,
EuclideanCluster and conversion sources from autoware_euclidean_cluster.
The small colcon package uses the same utility header layout as the crop
benchmark. It builds only this component from the perception package.
The node uses its production YAML (2D, tolerance 0.7 m, 10–1000 points
per cluster), subscribes to PointCloud2 and publishes DetectedObjectsWithFeature.
Use the source message packages from this checkout with the existing Lyrical
underlay. Run the builds sequentially under the laptop memory guard:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash -ec '
  colcon build --base-paths src/core/autoware_msgs/autoware_perception_msgs \
    src/universe/external/tier4_autoware_msgs/tier4_perception_msgs \
    --executor sequential --build-base build/perception-replay/obj \
    --install-base build/perception-replay/install --cmake-args -DBUILD_TESTING=OFF \
    -DCMAKE_CXX_STANDARD=17 -DCMAKE_POLICY_VERSION_MINIMUM=3.5'
experiments/buffer/run-lyrical.sh bash -ec '
  source build/perception-replay/install/local_setup.bash
  export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21
  colcon build --base-paths src/universe_detection_replay --executor sequential \
    --build-base build/perception-replay/obj --install-base build/perception-replay/install \
    --cmake-args -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15" \
    "-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG"'
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout -k 5 180 \
  bash experiments/rmw-agnocast/run-cluster-replay.sh
```

For each of 32 acquisitions, generate a persistent 12-point landmark and
12-point targets visible for three consecutive acquisitions. Sweep all
30 possible target onsets in the same scene. Targets are separated by
4 m. Replay only the acquisitions each near branch delivered, in recorded
arrival order. Every input retains its recorded stamp. Wait for each output
and verify its header, complete list of centroids and feature-cloud sizes.
All replays use Cyclone DDS to reach the same node. Delivery differences
come from the previously measured native, RMW, Cyclone and Zenoh traces.
There is no additional queue overflow during replay. The on-time estimate
uses recorded input arrival minus the target's first acquisition stamp and
excludes cluster computation and output transport. It is an upper bound
on the number of detections available within 100 ms in a live graph.

Both build steps passed, with at least 8,718 MiB host memory available.
All 64 traces and 1,816 inputs passed twice with identical per-trace results.
The 32 controls detected every target. With the near branch impaired, native
alone detected onset 9, while the three ordinary-ROS paths alone detected
onsets 13 and 14. All four missed onset 15. Exactly 13 of 30 targets had
a cluster supported by an input arriving within 100 ms of appearance for
every impaired trace. This replay establishes a geometric detection effect.
Tracking identity, motion prediction and live scheduling remain open.
[Per-trace results](../rmw-agnocast/cluster-replay-results.txt).

## 2026-11-22 — Track identity and confirmation after near-branch losses

Use the production Universe multi-object tracker to follow the cluster replay
one stage further. Start with the preceding Lyrical perception and Agnocast
overlays. Build the local message/utility dependencies and muSSP, then the
full tracker executable with ordinary ROS endpoints. The previous detector
was stateless. Here every trace starts a separate tracker process and
retains its state from acquisition 0 through 31.

The first muSSP compile failed to find the C++ standard header vector.
Its CMake clears CMAKE_CXX_FLAGS and discards Clang's GCC installation hint.
Build that dependency with GCC. Clang 21 rejected the geometry utility's
GCC-only diagnostic pragma under -Werror; allow unknown warning options
to remain warnings in this colcon experiment. The tracker then failed on
Boost hash lookup and deprecated TF headers. Apply the Boost include and
odometry TF changes from the main worktree's 019919f2, and select the
installed .hpp TF buffer/listener headers where .h wrappers emit errors.
The script builds a copy so the Universe source subtree stays pristine.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-track-replay.sh
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh \
  bash experiments/rmw-agnocast/run-track-replay.sh
```

The fixture runs the preceding Euclidean cluster node on all 32 source
scenes. It fits the measured rectangular XY extent of each planar cluster,
supplies a 0.1 m height, and preserves its label and centroid. This fixture
adapts cluster features to polygon-shaped DetectedObjects. Use a static
identity map-to-base_link transform and the tracker's shipped lidar_clustering
channel and association matrix. The other eleven inputs are disabled.
Prediction and confidence checks use the stock measurement-triggered
configuration. The sink reads confirmed TrackedObjects. A second stationary
landmark at y = 8.1 m has detections at groups 0–3, 9–11 and 25–31 to
exercise identity after occlusion. This visibility pattern deliberately
probes the observations only native retained. The original persistent
landmark and all 30 transient windows supply independent comparisons.

For each trace, shift the acquisition and arrival timestamps together to the
current epoch. Release detections in observed arrival order on local Cyclone
DDS, waiting for each matching tracker output before the next release.
Record any scheduling slip, first confirmed receipt relative to first
acquisition, UUID continuity and longest interval between outputs containing
the persistent landmark. These measurements include tracker processing and
replay transport. Cluster execution is outside the timed region. All paths
use the same tracker binary and replay transport.

Two runtime issues surfaced before the comparison. On the healthy trace,
checked libstdc++ aborted inside muSSP's topologicalSort_counter_order.
The backtrace passed through PolarAssociation::assign. The traversal recognizes
a missing parent on function entry but indexed the visited vector at that
parent before recursing. Check for the missing parent at the lookup as well;
keep the current vertex in the ordering. Rebuilding the solver copy with
this guard allowed the same healthy trace to finish with bounds checks active.

The healthy tracker then published two UUIDs at the same persistent landmark.
InputStream::getObjectsOlderThan exports through an inclusive timestamp but
only removed entries strictly older than that timestamp. An entry at the
boundary could be processed again with its old association. The baseline
published duplicates in the healthy, native-stalled and RMW-stalled controls.
Changing removal to use the same inclusive boundary eliminated those duplicate
outputs in the three matched runs. The baseline already includes the solver
fix. Use REPLAY_CONSUME_STAMP=0 for its build and the default for the corrected
comparison. The local correction has been exercised on a single ordered
channel. For the related upstream implementation see the
[AWF input manager](https://github.com/autowarefoundation/autoware_universe/blob/main/perception/autoware_multi_object_tracker/src/processor/input_manager.cpp).
Neither fix changes rmw_agnocast or the Bazel tree.

The corrected 4 m scene ran all 64 archived traces (1,816 matched tracker
outputs). Every trace retained one UUID on the continuously visible landmark
from initial confirmation through group 31. Every trace reacquired the
long-occluded landmark with a new UUID at or after group 25. Native stalled
traces put confirmed tracks at 25 transient positions, versus 23 for the
other three stacks; all four reached 12 within 100 ms. At this spacing,
UUIDs sometimes moved between different target positions, including in the
healthy control. Consequently these figures describe coverage of positions.

Repeat trial 1 at 8 m spacing to separate association from delivery. The
coordinate change translates each cluster-derived detection along X, keeping
the measured shape. All five replays had zero UUIDs shared across positions.
Native reached 25 of 30 distinct targets, the other impaired inputs 21,
and the healthy control 30. These counts match the number of visibility
windows with at least two delivered detections. Maximum release slip in
the full 4 m matrix was 7.63 ms; the 100 ms measure includes replay slip.
The full tracker build had at least 2,886 MiB host memory available.
The full matrix had at least 4,937 MiB available under the runtime guard.

Use the existing exported Lyrical root and run the following controls
sequentially. The trace arguments select the inputs independently of the
tracker's shared Cyclone DDS runtime.

```bash
r=experiments/rmw-agnocast
export BUFFER_RUNTIME_ONLY=1 REPLAY_TARGET_SPACING_M=8
for group in 18 20 22 24 25; do
  REPLAY_RETURN_GROUP=$group experiments/buffer/run-lyrical.sh bash "$r/run-track-replay.sh" \
    "$r"/pipeline-busy-results/1-{stock-healthy,stock-slow-near,native-slow-near}.log
done
for hide in 0 1; do
  REPLAY_RETURN_GROUP=18 REPLAY_HIDE_GLIMPSE=$hide experiments/buffer/run-lyrical.sh \
    bash "$r/run-track-replay.sh" "$r"/pipeline-{busy,isolation}-results/2-{stock,native}-slow-near.log
done
```

For groups 20, 22 and 24, hiding the glimpse also changed native's UUID,
while rmw_agnocast reacquired with a new UUID in both scenes. Repeat the
second command with REPLAY_RETURN_GROUP=20 for the matched group-20
confirmation on trial 2 from each stall workload. At group 18 native kept
the ID even with the glimpse hidden. Thus the schedule difference alone
can affect identity at some return times. The causal observation benefit
was reproduced at group 20 on both workloads. The continuously visible
landmark remained confirmed throughout each run after its initial
confirmation. See the [comparison and archived evidence](../rmw-agnocast/README.md#delayed-observations-and-multi-object-track-identity).

## 2026-11-22 — Near reader history depth and track identity

Test whether retaining more queued clouds on the near branch recovers the
established track in the preceding group-20 occlusion replay. The production
crop normally uses max_queue_size for both its input and output. To isolate
the reader, build a second source file (filter.cpp) into the benchmark's
existing non-Buffer-matrix build copy. Only the near process has
BENCH_STALL_THIS_CROP. When BENCH_NEAR_DEPTH is also set, the ordinary
point-cloud subscription requests KEEP_LAST depth 5, 8 or 16. The near
publisher still requests 5. Each log records both settings. The fusion
publisher, far branch, tracker and middleware implementations are unchanged.

Repeat the preceding guarded crop build, using its cached configurations:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash -ec '
  source build/agnocast-lyrical/install/local_setup.bash
  source build/universe-sync/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  for ws in universe-pipeline universe-pipeline-native; do
    if [[ $ws == universe-pipeline-native ]]; then export ENABLE_AGNOCAST=1; else export ENABLE_AGNOCAST=0; fi
    colcon build --base-paths src/universe_crop_benchmark --build-base "build/$ws/obj" \
      --install-base "build/$ws/install" --executor sequential
  done'
mkdir -p build/universe-depth
out=build/universe-depth/pairs
r=experiments/rmw-agnocast
bash "$r/bench-native-graph.sh" "$out" depth
REPLAY_TARGET_SPACING_M=8 REPLAY_RETURN_GROUP=20 BUFFER_RUNTIME_ONLY=1 \
  experiments/buffer/run-lyrical.sh bash "$r/run-track-replay.sh" "$out"/depth-*/*.log > "$out/tracker.log"
rg '^trace=' "$out/tracker.log" > "$out/tracker.txt"
python3 "$r/summarize-depth.py" "$out"
```

Use a fresh output directory, with the preceding corrected tracker installed.
All four transports get the same six 150 ms CPU-busy near callbacks, at
20/20/10 Hz. Run one graph at a time, inverting middleware and depth order
in round two. Use the wider transient-target spacing, fixed acquisition and
arrival offsets, and the three-frame glimpse at 9–11. Temporal outcomes are
specific to this visibility window. Near maximum delivered age measures age
on receipt. Peak latest-observation age measures the age a consumer sees
between receipts. The fresh gap uses a 100 ms budget. Far latency covers
complete acquisitions made while the near callback was impaired.

Across both rounds, rmw_agnocast, Cyclone DDS and Zenoh delivered 24, 27
and 32 of 32 near clouds at depths 5, 8 and 16 respectively. They retained
zero, two (9 and 11), and three glimpse observations. Tracker replay lost
the returning object's UUID only at depth 5. Native delivered 26/32 and
retained all three glimpses and the UUID at every requested depth. At depth
8 native and rmw_agnocast both confirmed 25/30 transient targets, but
native missed targets 12–16 and rmw_agnocast missed 8 and 10–13.

To isolate the effect of seeing the returning object during the glimpse,
repeat the six rmw_agnocast replays with only that object's detections
removed from groups 9–11:

```bash
REPLAY_HIDE_GLIMPSE=1 REPLAY_TARGET_SPACING_M=8 REPLAY_RETURN_GROUP=20 BUFFER_RUNTIME_ONLY=1 \
  experiments/buffer/run-lyrical.sh bash "$r/run-track-replay.sh" "$out"/depth-*/*-stock-slow-near.log > "$out/hidden.log"
rg '^trace=' "$out/hidden.log" > "$out/hidden.txt"
```

All six controls assigned a new UUID after return, including the two
depth-16 runs with complete cloud delivery. Retaining observations during
this visibility window explains the identity recovery in this fixture.
The oldest delivered near cloud on rmw_agnocast rose from 407–408 ms to
556–557 ms and then 655–656 ms. All 24 replays confirmed 12/30 transient
targets within 100 ms. Depth 8 was the smallest tested setting to preserve
identity. Depth 16 recovered the entire bounded burst. Neither improved
timely confirmation. Each far branch delivered all 32 groups. Maximum
measured graph memory was 485.7 MiB and minimum available host memory
during the graph matrix was 3737 MiB.

[Per-run transport and tracker measurements](../rmw-agnocast/pipeline-depth-results/summary.txt)
and [hidden-glimpse controls](../rmw-agnocast/pipeline-depth-results/hidden.txt).

## 2026-11-22 — Rejecting stale near-crop inputs at depth 8

Keep the three-LiDAR CPU overload, reader depth 8, writer depth 5, and
100 ms freshness budget from the preceding experiment. Compare processing
every callback with dropping clouds whose acquisition stamp is already
more than 100 ms old at callback entry. Both policies sample age at the
same point. Both run the six 150 ms busy intervals on near callbacks 5–10,
including rejected callbacks, before invoking the real filter or returning.
This placement models unavoidable interference on the callback thread.
It avoids crediting the policy with eliminating the injected overload.
An input that was fresh when admitted can still leave the crop stale.

Repeat the two guarded sequential colcon builds above. With the same
Lyrical root and overlays, run:

```bash
r=experiments/rmw-agnocast
out=build/universe-freshness
bash "$r/bench-native-graph.sh" "$out" freshness
REPLAY_TARGET_SPACING_M=8 REPLAY_RETURN_GROUP=20 BUFFER_RUNTIME_ONLY=1 \
  experiments/buffer/run-lyrical.sh bash "$r/run-track-replay.sh" "$out"/*/*.log > "$out/tracker.log" 2>&1
rg '^trace=' "$out/tracker.log" > "$out/tracker.txt"
python3 "$r/summarize-freshness.py" "$out"
```

Run both orders serially. Recovery is elapsed wall time from the end of the
sixth busy interval to the first received near cloud aged at most 100 ms.
The fixture checks fused clouds, metadata, and far-branch payloads. Replay
uses the same tracker and timed releases for each recorded graph trace.
The summary also cross-checks explicit discards against the missing input
stamps to distinguish application drops from queue loss.

All 16 graphs passed. All 512 fused cloud/metadata pairs and 512 far
outputs were present. Near-thread busy CPU totaled 898.7–899.9 ms per
graph. The guard recorded up to 492.2 MiB for a graph, with at least
3034 MiB available on the host during graph runs and 3046 MiB during builds.

On rmw_agnocast the first fresh delivery came 2.27–2.33 ms after the end of
the stall with keep and 1.10–1.15 ms with reject. Oldest received age fell
from 555 ms to 153 ms, while delivered near frames fell from 27 to 16.
Native fell from 26 to 16, Cyclone DDS and Zenoh from 27 to 16. In every
transport transient confirmations fell from 25 to 14 out of 30. Timely
confirmations stayed at 12. Reject missed all transient onsets 4–19.

The returning object retained its UUID in all 16 replays. Under rejection
there were no delivered inputs from groups 5–20, including the glimpse.
This differs from hiding the object in otherwise delivered inputs in the
preceding experiment. UUID continuity across that silence is compatible
with a long interval between tracker outputs. For the continuously visible
landmark on rmw_agnocast the largest gap between reported observations rose
from 161 ms to 751 ms. This replay does not establish tracker-internal
lifecycle behavior under other inputs.

Keep the depth-8 backlog policy for this fixture. The paired reduction in
recovery time buys no additional timely confirmations. A subsequent
executor-isolation experiment should distinguish competing callbacks from
work that must execute on the cloud callback's own thread.

[Per-run metrics and missed targets](../rmw-agnocast/pipeline-freshness-results/summary.txt) ·
[Tracker timing and identities](../rmw-agnocast/pipeline-freshness-results/tracker.txt).

## 2026-11-22 — Competing callback on the three-LiDAR near crop

Run the depth-8 near crop on the same 20/20/10 Hz graph with an independent
timer in a separate mutually exclusive callback group. Create the timer and
group before spin, keep both alive with the subscription, and arm the 5 ms
timer after the fifth near callback enters. Its first armed invocation burns
900 ms of wall time once. It logs its actual thread CPU time and the start
and end of the window. The crop logs each subscription callback entry. This
single contiguous interval models another task occupying the executor.
It differs from the previous six delays inside the cloud callback. Keep
the queued inputs in both conditions.

For ordinary ROS endpoints compare the stock single-threaded crop with the
same component in the pinned Agnocast library's mixed-capable CIE. Change
only the near executable, leaving the far branch and upstream executors
alone. Include native Agnocast using its existing Agnocast-only CIE as a
reference. Build both cached overlays sequentially under the memory guard:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash -ec '
  source build/agnocast-lyrical/install/local_setup.bash
  source build/universe-sync/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  for ws in universe-pipeline universe-pipeline-native; do
    if [[ $ws == universe-pipeline-native ]]; then export ENABLE_AGNOCAST=1; else export ENABLE_AGNOCAST=0; fi
    colcon build --base-paths src/universe_crop_benchmark src/universe_sync_benchmark \
      --build-base "build/$ws/obj" --install-base "build/$ws/install" --executor sequential
  done'
r=experiments/rmw-agnocast
out=build/universe-competitor
bash "$r/bench-native-graph.sh" "$out" competitor
REPLAY_TARGET_SPACING_M=8 REPLAY_RETURN_GROUP=20 BUFFER_RUNTIME_ONLY=1 \
  experiments/buffer/run-lyrical.sh bash "$r/run-track-replay.sh" "$out"/*/*.log > "$out/tracker.log" 2>&1
rg '^trace=' "$out/tracker.log" > "$out/tracker.txt"
python3 "$r/summarize-competitor.py" "$out"
```

Lyrical's rclcpp timer function traits rejected a mutable lambda during the
initial build. Give the timer a const-callable closure and put its one-shot
flag in captured storage. The timer callback group's mutual exclusion
serializes access to that flag. An atomic counter connects the two groups.

As an attribution control, the ordinary ROS build also registers the same
crop in stock rclcpp MultiThreadedExecutor with exactly two workers. Run
the commands above again with out=build/universe-workers and pass workers
as the graph runner's second argument. This runs two new single/two-worker
pairs on rmw_agnocast, reversing order in the second pair. Each graph
uses the same timer, callback groups, source rates, queue and verification.

All 18 live graphs delivered the full 32 fused cloud/metadata pairs and all
32 far outputs. The competing timer consumed 899.5–900.0 ms of thread CPU.
All eight single-threaded runs had zero near callback entries inside the
busy interval and delivered 22/32 near outputs. The tracker confirmed 20/30
transient targets, 13/30 within 100 ms, and assigned a new UUID on return.
All ten concurrent runs recorded 18 near callback entries during the interval,
delivered 32/32 and kept the returning UUID. Tracker replay confirmed 30/30
within 100 ms. Maximum graph scope peak was 474.6 MiB, and minimum available
host memory during the graphs was 8369 MiB.

Across the four paired single-threaded rmw_agnocast runs, near receipt-age
p95 was 304.34–308.59 ms. With the bundled CIE it was 42.23–42.52 ms,
and with two rclcpp workers 42.47–43.10 ms. Within the mixed full/partial
fixture, the partial groups include a 40 ms collector timeout. Two workers
were sufficient on this laptop with one independent busy callback. This
leaves open the behavior under multiple runnable callbacks, CPU contention,
and competing work that holds a lock required by the hot path.

The graph runner was edited while the first invocation was finishing. Bash
reported a trailing parse error after writing all 14 complete graph logs.
Validated every recorded graph independently, checked the final runner with
bash -n, and ran tracker replay on all 14 traces. The separate four-graph
worker invocation and its tracker replays completed end to end.

[Executor matrix](../rmw-agnocast/pipeline-competitor-results/summary.txt) ·
[Two-worker control](../rmw-agnocast/pipeline-workers-results/summary.txt).

## 2026-11-22 — Saturating two executor workers on the near crop

Use the same three-LiDAR graph, depth-8 reader, and paced tracker fixture
as the preceding competing-callback test. Give each of two 5 ms timers its
own mutually exclusive group and one 900 ms busy invocation after the fifth
cloud callback. The two ordinary executor configurations use exactly two
or three workers. The bundled CIE assigns a worker per callback group.
Native uses its existing Agnocast-only CIE. No priority tuning was applied.
Compare rmw_agnocast, Cyclone DDS and Zenoh in both run orders.

Use the preceding two-overlay build recipe (crop and sync packages).
Check lscpu -e=CPU,CORE,SOCKET and select three allowed logical CPUs on
distinct physical cores. Apply the mask only to the near crop at launch.
Both timer callbacks report their effective masks. Upstream nodes and the
far reference keep their ordinary placement. A first launch inherited
the Agnocast heap hook in taskset, then initialized it again after exec
in the crop and failed process registration. Set the preload only on the
final crop executable. Rerun the entire matrix in a fresh output directory.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
export BENCH_NEAR_CPUS=2,3,4
r=experiments/rmw-agnocast
out=build/universe-saturation-paired
bash "$r/bench-native-graph.sh" "$out" saturation
REPLAY_TARGET_SPACING_M=8 REPLAY_RETURN_GROUP=20 BUFFER_RUNTIME_ONLY=1 \
  experiments/buffer/run-lyrical.sh bash "$r/run-track-replay.sh" "$out"/*/*.log > "$out/tracker.log" 2>&1
rg '^trace=' "$out/tracker.log" > "$out/tracker.txt"
python3 "$r/summarize-competitor.py" "$out"
```

Count cloud entries strictly inside the intersection of the two busy
windows. Require more than 800 ms of common time and identical three-CPU
masks on both timers in every run. Compare frame deliveries, near and far
receipt ages, longest gap between fresh near arrivals, transient-target
confirmations within 100 ms, and continuity of the returning object's UUID.

All 20 graphs passed, each with 32 fused cloud/metadata pairs and 32 far
outputs. The two busy windows overlapped for 896.2–900.0 ms, and both
timers reported CPUs 2,3,4 throughout the matrix. The CPU values measure
per-thread execution over wall-time bursts (852.4–900.0 ms each). The
highest graph scope peak was 493.5 MiB and minimum graph host headroom
was 5251 MiB. The heavier two-overlay build retained at least 6497 MiB.

With rmw_agnocast, two workers executed zero cloud callbacks during the
overlap, delivered 22/32 near clouds and had near receipt-age p95 of
305.43–306.60 ms. Three workers and bundled CIE both executed 18 callbacks
during the overlap, delivered 32/32, and had p95 of 42.84–43.00 ms and
42.15–44.90 ms respectively. The tracker confirmed 13/30 transient targets
within 100 ms for two workers and 30/30 for either concurrent configuration.
The returning UUID changed only with two workers. Cyclone DDS and Zenoh
had the same delivery and tracker counts. Native Agnocast CIE also delivered
32/32 and confirmed 30/30 on time. One Cyclone CIE run processed 17 cloud
callbacks strictly within the intersection while delivering all frames.

For this workload the third available worker explains the observed recovery.
Keep ordinary rclcpp pools as the baseline. CIE remains optional for
per-group scheduling. This experiment did not reserve cores, set thread
priorities or validate standalone CIE. The local selection decision and
upstream compatibility check are in the executor-selection section of the
rmw_agnocast README.

[Per-run measurements](../rmw-agnocast/pipeline-saturation-results/summary.txt) ·
[Tracker replays](../rmw-agnocast/pipeline-saturation-results/tracker.txt).

## 2026-11-22 — Local retained and copying PointCloud2 readers with a late DDS recorder

Qualify one live publisher, one ordinary CPU reader, one local reader retaining
a whole payload through an Agnocast host view, and an outbound DDS recorder.
Use the preceding Lyrical overlays, /dev/agnocast, heap hook and publisher
loan support. Add a fixture to agnocast_rmw_loan_probe. Its writer publishes
256 KiB clouds at 20 Hz with one UINT32 intensity field and a sequence-dependent
payload. Every callback checks the shape and all payload bytes. Compare the
producer's data address with the CPU and retained reader addresses for the
same sequence. Retain the first view through at least two recorder lifetimes
and verify its bytes again on each subsequent callback. All readers use a
single executor thread per process; this experiment does not inject CPU load.

A fixture-only environment flag instruments the existing host-cloud writer
deleter. It reports the sample stamp after destroying the shared vector and
freeing the host-cloud object. The publisher's loan/private-copy counters
and reader backend/address checks describe payload ownership at the native
edges. They do not count DDS serialization or storage-plugin copies.

Keep one writer and CPU reader alive. Start the existing outbound route bridge
after the baseline. Start four successive view readers. For each reader join,
stop, then restart an unhooked Cyclone recorder on /mirror while the producer
continues. A separate DDS observer measures callback latency in the same
20-frame window as each local reader. In the bag run, launch rosbag2 with
SQLite3 and zero recorder cache and deserialize every stored record, checking
its full payload, source timestamp and consecutive sequence number. Stop the
holder gracefully in odd cycles and send SIGKILL in even cycles. After 35
more CPU-reader frames check reclamation of the held sample, all old sample
stamps, outstanding payload count and publisher shared-mapping RSS.

Run builds and experiments sequentially. Use a fresh output directory and
run each Python invocation from the repository root:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout 180 bash -ec '
  source build/agnocast-lyrical/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  out=build/mixed-reader-reproduction
  mkdir "$out"
  r=experiments/rmw-agnocast/mixed-readers.py
  for mode in native cyclone; do python3 "$r" "$out/$mode" "$mode"; done
  /opt/ros/lyrical/lib/rmw_zenoh_cpp/rmw_zenohd > "$out/router.log" 2>&1 &
  router=$!
  trap "kill $router; wait $router || true" EXIT
  sleep 1
  python3 "$r" "$out/zenoh" zenoh
  kill "$router"
  wait "$router" || true
  trap - EXIT
  python3 "$r" "$out/mixed" rmw --bags'
```

The native, Cyclone and Zenoh controls use one producer and one reader at
the same rate and payload size. Native uses its typed message loan and
Agnocast-only single-threaded executor; the ROS references use the ordinary
CPU path with loaning disabled. These single-reader latency samples are
context for the mixed case, rather than matched native bridge trials.

Two independent disk-recording runs delivered 550/550 clouds each to the
always-on local CPU reader. The host-view payload addresses matched the
publisher's addresses for every received sequence; the default CPU reader
always had a distinct address. The writer reported 550 loans and zero
private payload copies in each run. All 16 SQLite bags contained consecutive
sequences within their recording intervals (204 + 206 stored clouds),
with verified source stamps and bytes. Gaps between recording intervals
are expected for this volatile route. First-run single-reader controls each
delivered 45/45; after five warmups their median/p95 in milliseconds were
native 0.050/0.062, Cyclone 0.433/0.576, Zenoh 0.305/0.389, rmw_agnocast
CPU reader 0.112/0.147 (repeat CPU baseline 0.103/0.118).

Across the 16 simultaneous 20-frame windows, pooled publication-to-callback
median/p95 were 0.107/0.137 ms for the local CPU copy, 0.088/0.121 ms for
the retained view, and 0.581/0.811 ms for the ordinary DDS timing observer.
Record time is taken at callback entry before verification. DDS observer
timing excludes SQLite persistence. Reference runs have a different graph
and native avoids the local CPU materialization.

All eight retained sample stamps were absent from publisher reclamation
events while held, then present after reader exit. Both graceful shutdown
and SIGKILL occurred four times. Every drain checkpoint found five live
payloads (the writer history depth) with zero old outstanding samples.
Publisher payload-mapping RSS at the four checkpoints was 3092 KiB in
trial one, and 3092, 3348, 3348, 3348 KiB in trial two. These count live
payloads and one mapping, not every allocation in the process. Minimum
available host memory in the two guarded disk runs was 5643 and 5613 MiB.

Repeated the same four-cycle test first with a DDS timing observer alone
(476/476 local), then added actual rosbag2 persistence. Raw logs, final
SQLite bag metadata and both disk trial summaries are archived. Full
generated bag files stay in the ignored build directory; the runner reads
each database back before reporting success. The experiment exercises
reader death while the publisher survives. Publisher or bridge failure,
dynamic component unloading and multiple hosts remain distinct cases.

[Trial one](../rmw-agnocast/mixed-reader-results/trial-1/summary.txt) ·
[Trial two](../rmw-agnocast/mixed-reader-results/trial-2/summary.txt).

## 2026-11-22 — Materializing non-CPU PointCloud2 input on the Agnocast host route

Lyrical's generated C++ byte buffer provides CPU vector access and an
explicit to_vector() operation. For a non-CPU implementation the latter
calls the backend's to_cpu() method. The host-cloud writer previously
rejected such inputs on both rmw_publish and rmw_publish_loaned_message.
This is a local transport decision. Lyrical release notes list Fast DDS
and Zenoh for Buffer-aware transport; the preceding Cyclone 4.1.5 results
document a failed non-CPU shmem publication even though CPU data works.

Use to_vector() when constructing a copied HostCloud. Retain the direct
move for loaned CPU storage already owned by the writer. For converted
buffers, check the resulting vector against the publisher's shared mapping.
If the backend returned private storage, copy into a shared vector and
verify ownership before handing the frame to another process. The writer
counts successful backend materializations and additional host copies
separately from the existing CPU publisher-loan/private-copy counters.
Ordinary readers receive CPU storage and an opted-in reader retains the
host view. The publisher uses the buffer interface without linking a CUDA
backend into rmw_agnocast or transmitting device descriptors.

Use the existing built Lyrical Buffer-matrix crop/endpoint from
build-universe-lyrical.sh, the GPU device, the Agnocast kernel module,
and the patched rclcpp/probe overlays. Run the guarded build once and
the six runtime cells sequentially in a fresh directory:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
out=build/host-fallback-check
mkdir "$out"
for mode in mixed fixed; do
  for backend in cpu shmem cuda; do
    BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout 65 bash -ec '
      source build/agnocast-lyrical/install/local_setup.bash
      source build/rmw-agnocast/install/local_setup.bash
      source build/rmw-agnocast/load-install/local_setup.bash
      export CYCLONEDDS_URI="<CycloneDDS><Domain><General><Interfaces><NetworkInterface name=\"lo\"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address=\"127.0.0.1\"/></Peers></Discovery></Domain></CycloneDDS>"
      flags=(--host-cloud)
      if [[ $1 == fixed ]]; then flags=(--payload-scope); fi
      if [[ $2 != cpu ]]; then flags+=(--source-buffer "$2"); fi
      python3 src/universe_crop_benchmark/run_case.py "$1" "$3/$1-$2.csv" "${flags[@]}"' \
      bash "$mode" "$backend" "$out" > "$out/$mode-$backend.log" 2>&1 || exit 1
  done
done
```

Only the source receives the non-CPU backend selection. Each receiver
checks the crop result, including point bytes and order. The mixed row uses
ordinary publication and CPU consumers. The fixed row uses scoped publisher
loans and host-view subscribers on both edges. This checks fallback on the
first edge and normal CPU output on the second.

All six cases delivered 60/60 verified frames. Each CUDA and shmem source
reported 60 backend materializations and zero extra copies to relocate the
result into the shared pool. The scoped CPU controls reported 60 loans and
zero private payload copies on both edges. These counters exclude copies
made by a backend internally, CPU-reader materialization and crop processing.
The lowest available host memory in the final runtime matrix was 22202 MiB.
The experiment covers the optimized PointCloud2 route. Generic Buffer
messages, CUDA descriptor forwarding and compatibility in Cyclone's own
serialization path are separate work.
[Raw runs](../rmw-agnocast/host-fallback-results/).

## 2026-11-22 — Component load, unload, reload and shutdown with a blocked executor

Use the actual Universe CropBoxFilterComponent and its existing benchmark
package registration. Keep the writer, independent raw reader and independent
crop-output reader alive throughout each run. The stock Lyrical component
container uses ordinary LoadNode, UnloadNode and ListNodes services. For
rmw_agnocast these control services are delegated to Cyclone. Only /input
and /output are selected for Agnocast transport. Compare default notification,
per-call direct mixed wait, the separately scoped fixed wait, Cyclone DDS,
and Zenoh. Use the same ordinary CPU cloud representation at the application
interface, QoS, crop bounds and single-threaded container executor in each row.

Extend the endpoint fixture with a file-controlled publication limit. An
atomic rename advances the limit without introducing a DDS command that could
wake the crop. Each cloud contains 32,768 16-byte points, with a sequence in
the intensity field. The independent input and output readers validate all
point bytes against the fixture. The driver waits for endpoint discovery,
samples the container main thread's sleeping state and wait channel, publishes
one frame and waits for both readers before allowing any further data. It then
runs at 20 Hz and loads three successive instances on the same topics. Each
of two unloads must return success and remove the instance from the manager
and the ROS graph. Traffic continues through unload and reload. Check a
contiguous window after each load and an output-silent window after each
unload, allowing transitions around service completion. Finally stop input,
signal the still-loaded container, require clean exit within five seconds,
check graph cleanup and publish five more witness frames without crop output.

For the RMW rows the persistent writer samples the Agnocast kernel's topic
counts for the input best-effort/reliable routes and the output best-effort
route. This separately checks transport membership after reload, unload and
process shutdown. A best-effort RMW subscription listens on both reliability
routes in this prototype.

Only the scoped row uses lifecycle_container. It uses the same ROS component
manager and executor; a test ListNodes call after warmup starts a fixed wait
scope. The fixture stops and joins the helper before adding/removing a node
from the executor, and before teardown after spin returns. Each generation
must show a separate scope with actual native wakes and one stop interrupt.
This is an explicit lifecycle boundary. It does not make arbitrary timers,
DDS entities or subscriptions safe to mutate within a live fixed scope.

Using the same Lyrical root, Agnocast module and overlays as the preceding
Universe runs, build serially:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash -ec '
  bash experiments/rmw-agnocast/build-loadable.sh
  source build/agnocast-lyrical/install/local_setup.bash
  source build/universe-sync/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  colcon build --base-paths src/universe_crop_benchmark --build-base build/universe-pipeline/obj \
    --install-base build/universe-pipeline/install --executor sequential'
out=build/lifecycle-reproduction
mkdir "$out"
for mode in stock direct scoped cyclone zenoh; do
  BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout -k 5 60 bash -ec '
    source build/agnocast-lyrical/install/local_setup.bash
    source build/rmw-agnocast/load-install/local_setup.bash
    source build/universe-sync/install/local_setup.bash
    source build/universe-pipeline/install/local_setup.bash
    export CYCLONEDDS_URI="<CycloneDDS><Domain><General><Interfaces><NetworkInterface name=\"lo\"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address=\"127.0.0.1\"/></Peers></Discovery></Domain></CycloneDDS>"
    if [[ $1 == zenoh ]]; then export RMW_IMPLEMENTATION=rmw_zenoh_cpp; fi
    python3 experiments/rmw-agnocast/dynamic-graph.py "$1" "$2/$1"' \
    bash "$mode" "$out" >"$out/$mode.txt" 2>&1 || exit 1
done
```

The driver starts its own Zenoh router for the Zenoh row. Repeat with a
fresh output directory and reversed mode order.

Both run orders passed in all five modes. In each of the ten runs the raw
witness received 78/78 unique frames in order, including five after container
exit. Each crop verifier received 59 frames, including the isolated frame 0
and every frame in the three steady windows (2–18, 27–43, 52–68). The crop
was intentionally absent for part of the stream. All six Agnocast runs
recorded native counts of 2/2/1 with the crop loaded and 1/1/0 after removal.
Each scoped run recorded three helper completions and one stop interrupt per
scope. Process shutdown measured 263.8–264.0 ms for the Agnocast modes,
263.8–263.9 ms for Cyclone and 113.5 ms in both Zenoh runs. These times
include the driver's process-exit observation interval. The lowest sampled
host MemAvailable was 19,926 MiB.

The normal manager with direct wait handled these live graph changes. Keep
that configuration in the Universe pilot. Fixed wait passed with manager
cooperation at each executor change. This fixture does not establish safety
for arbitrary graph mutation inside a fixed scope. The experiment covers
orderly unload and shutdown on one laptop with one writer. It does not
measure callback latency or recovery from a killed container.

[Run summaries, frame sequences and endpoint logs](../rmw-agnocast/pipeline-lifecycle-results/)

## 2026-11-22 — Restart the explicit outbound Cyclone bridge under live crop traffic

Determine whether restarting one failed relay restores an already recording
DDS reader while a standard Universe crop continues to process native local
traffic. Use the previous lifecycle overlay, kernel module and endpoint.
Keep one ordinary host-cloud writer (512 KiB PointCloud2 at 20 Hz), a direct
wait crop, its independent verifying sink, and a stock Cyclone SQLite bag
recorder alive for 150 frames. Mirror /input to /mirror with route_bridge.
Select SensorDataQoS and loopback discovery. Register both recording and
local endpoints before publication. At sequences 35 and 95 send SIGKILL to
the relay, observe twelve more frames, and spawn a fresh relay with identical
arguments. Compare against the same graph with an uninterrupted relay.
Repeat the pair in reverse order. The driver acts as the supervisor.

Share the existing host-cloud destruction callback between ordinary and
loaned publication to audit reclamation on the ordinary publisher path.
Enable stamp logging in the endpoint fixture and release logging only in the
writer. Correlate every live publication with its destruction stamp at six
checkpoints per run, including during both outages. The writer samples the
kernel's best-effort/reliable input reader counts and output writer count.
Validate every cropped point in the local sink. After recorder shutdown,
deserialize every SQLite record, compare all 32,768 points with the original
sequence-dependent pattern and compare its header stamp to the writer log.
Use the bag receive timestamp for recovery time, referenced to the driver's
wall clock immediately before spawning the new process. Reconstruct local
sink callback arrival time from the source stamp and sink latency. This
measures the interval between verified crop outputs at the sink.

Run from the colcon worktree with its exported Lyrical root, loaded Agnocast
module and existing Universe and loadable RMW overlays:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash -ec '
  bash experiments/rmw-agnocast/build-loadable.sh
  source build/agnocast-lyrical/install/local_setup.bash
  source build/universe-sync/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  colcon build --base-paths src/universe_crop_benchmark --build-base build/universe-pipeline/obj \
    --install-base build/universe-pipeline/install --executor sequential'
out=build/reproduce-bridge
mkdir "$out"
for trial in healthy-1 restart-1 restart-2 healthy-2; do
  BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout -k 5 55 bash -ec '
    source build/agnocast-lyrical/install/local_setup.bash
    source build/rmw-agnocast/load-install/local_setup.bash
    source build/universe-sync/install/local_setup.bash
    source build/universe-pipeline/install/local_setup.bash
    export CYCLONEDDS_URI="<CycloneDDS><Domain><General><Interfaces><NetworkInterface name=\"lo\"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address=\"127.0.0.1\"/></Peers></Discovery></Domain></CycloneDDS>"
    python3 experiments/rmw-agnocast/bridge-restart.py "$1" "$2"' \
    bash "${trial%-*}" "$out/$trial" >"$out/$trial.txt" 2>&1 || exit 1
done
```

Both restart trials reproduced the same two recording gaps, 35→49 and
95→109, losing 13 frames per kill. The unchanged recorder stored 124/150
frames per run. Both healthy controls stored all 150. Four relaunch-to-first-
record intervals were 98.674, 91.361, 99.247 and 97.546 ms after a deliberate
roughly 0.6 s outage. Verified local deliveries were 150/150 in all four
runs. Maximum local output intervals were 50.502 and 50.304 ms during the
two restart trials and 52.094 and 50.920 ms in the controls. Each input
reader count went from two to one during the outage and back to two, with
one local output writer throughout the checkpoints. All 24 checkpoints had
five recent live writer payloads and no older outstanding payload. Lowest
sampled host MemAvailable during runtime was 19,088 MiB.

A supervisor can restore this same-host outbound recording route by
restarting only the relay. This best-effort profile permits a recording gap.
The experiment exercises neither automatic failure detection nor the reverse
DDS-to-native direction. The comparison uses the same Cyclone recorder in
both rows to isolate the restart. The prior lifecycle matrix covers Zenoh.
Raw SQLite bags stay in the ignored build tree. Archived bag sequence and
receive timestamps, crop samples, writer release and route logs permit
independent inspection of all reported timing and count results.

[Per-run evidence](../rmw-agnocast/bridge-restart-results/)

## 2026-11-22 — Replace the only native writer on an inbound bridge

Keep the stock Cyclone writer and its independent stock Cyclone witness on
/mirror. Forward that topic to /input with the existing r2a route_bridge
and run an ordinary Universe crop with the direct blocking wait. Keep the
crop and its /output verifier alive through both relay deaths. Use the
outbound experiment's Lyrical overlays, memory guard, CPU point layout,
crop bounds, 20 Hz publisher and SensorDataQoS. The DDS witness checks
all raw point bytes. The local sink checks all surviving cropped bytes.

First stream frames 0–79. Kill the bridge after crop frame 30 and relaunch
after the DDS witness gets frame 42. Check for one gap, ordered unique
deliveries and recovery before the source pauses at 79. Measure relaunch
to first valid crop output from the driver's wall-clock time before spawning
the bridge to the sink callback time (source stamp plus sink latency).
Then kill the bridge while the source is paused, observe zero native writers,
and launch the identical route. Wait for one native writer on the best-effort
host-cloud input route and a new DDS subscription endpoint on /mirror.
Sample the crop main thread sleeping in poll three times. Release only frame
80. Require both witness and crop output before permitting frame 81. Hold
another 250 ms and check for extra outputs. Finish at sequence 99. The
healthy control follows the same publication schedule with one uninterrupted
bridge. Run both orders serially.

Inspect the native publisher count with the vendored topic_info.cpp ioctl
wrapper, independently of DDS graph expiry after SIGKILL. Its full package
CMake build failed at ament_target_dependencies on Lyrical. The single
translation unit used for this observation builds directly in the guarded
root with no ROS API dependency. No middleware or Universe change was
needed. On the first diagnostic run, a cross-process equality assertion on
Cyclone ingress message-info GID bytes failed despite a continuous writer
and correct deliveries. The three relay processes saw three different byte
strings. The final driver checks the DDS writer from the same live graph
observer before and after replacement and keeps the bridge logs for inspection.

Reproduce from the experimental worktree with the same overlays and loaded
kernel module as the preceding outbound experiment:

```bash
mkdir -p build/inbound
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh g++ -shared -fPIC \
  src/core/agnocast/src/agnocast_ioctl_wrapper/src/topic_info.cpp \
  -o build/inbound/libagnocast_ioctl_wrapper.so
out=build/reproduce-inbound
mkdir "$out"
for trial in healthy-1 restart-1 restart-2 healthy-2; do
 BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout -k 5 55 bash -ec '
 source build/agnocast-lyrical/install/local_setup.bash
 source build/rmw-agnocast/load-install/local_setup.bash
 source build/universe-sync/install/local_setup.bash
 source build/universe-pipeline/install/local_setup.bash
 export LD_LIBRARY_PATH=/ws/build/inbound:$LD_LIBRARY_PATH
 export CYCLONEDDS_URI="<CycloneDDS><Domain><General><Interfaces><NetworkInterface name=\"lo\"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address=\"127.0.0.1\"/></Peers></Discovery></Domain></CycloneDDS>"
 python3 experiments/rmw-agnocast/bridge-restart.py "$1" "$2" --inbound' bash "${trial%-*}" "$out/$trial" >"$out/$trial.txt" 2>&1 || exit 1
done
```

All four DDS witness traces contain frames 0–99 exactly once. Both healthy
controls delivered 100/100 locally. Both restart trials delivered 87/100.
The only gap was 31–43 inclusive, all during the continuous restart
interval. Frame 44 reached the verified crop sink 101.056 and 100.080 ms
after spawn. During the idle restart the native writer count again fell to
zero and returned to one. The already running crop processed isolated
sequence 80 with 4.063 and 4.190 ms from DDS source stamp to local output
callback. Healthy controls measured 1.942 and 3.079 ms on that frame. Two
observations per condition do not establish a latency distribution.
The next nineteen frames were complete in every run. Minimum sampled host
MemAvailable during the four runs was 19,306 MiB.

This supplies the missing writer-replacement result for same-host best-effort
PointCloud2. Together with outbound restart, it supports implementing a
supervisor around the explicit relay. Automatic restart policy, topic
selection, multi-host behavior and reliable/durable QoS remain open.

[Frame sequences, stamps and relay logs](../rmw-agnocast/bridge-ingress-results/)

The unchanged outbound healthy mode of the shared driver also passed its
regression run with 150/150 verified local and recorded frames.

## 2026-11-22 — Supervise a configured bridge with ROS launch

Use the standard Lyrical LaunchService and ExecuteProcess to own a single
explicit PointCloud2 route. Set respawn=True, respawn_delay=0.25 s and
respawn_max_retries=3. These are experiment settings. The operator selects
the direction and topics at startup. Only the relay child receives the
Agnocast allocator preload and middleware selection. The driver starts the
deployment once, kills specific child PIDs, and stops the launch service
with SIGINT. ROS launch handles child exit, replacement and shutdown. Its
ProcessStarted and ProcessExited handler timestamps and PIDs provide an
independent record of each generation. An exit timestamp denotes observation
of the exit by launch, and a start timestamp denotes its start event.

Use the prior inbound/outbound colcon overlays, kernel module, standalone
ioctl observation library and unchanged Universe crop. A writer supplies
100 numbered 512 KiB PointCloud2 messages at nominal 20 Hz. An independent
DDS endpoint validates the raw cloud and a local sink validates the cropped
cloud. On ingress the DDS endpoint witnesses the source and the crop output
measures recovery. On egress the local crop witnesses the source and the DDS
endpoint measures recovery. The sensor-data QoS DDS endpoint checks every
point and records callback time. The preceding outbound study separately
qualified SQLite bag recording. Keep all application endpoints running
through shutdown to expose accidental respawns.

Kill the first relay after frame 10 and kill its replacement four frame
periods into recovery. Observe the kernel route count return to baseline
and subsequently to baseline plus one on each crash. In the active stop
case signal the supervisor after verified frame 60. In the pending stop
case first kill its third child, observe the exit and endpoint removal,
then signal the supervisor before its retry delay expires. Three retries
permit a fourth generation if cancellation fails. Require three distinct
PIDs, ordered supervisor events, no surviving child, native counts back at
baseline, no bridge endpoint in the persistent DDS graph and a complete
witness through frame 99. Allow up to 30 s for DDS discovery to clear
dead participants. Check sequence and content throughout, including that
the first forwarded frame after each restart was generated after spawn.

Reproduce after preparing build/inbound/libagnocast_ioctl_wrapper.so as in
the preceding entry. From the experimental worktree run:

```bash
out=build/reproduce-supervision
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
mkdir "$out"
for trial in outbound-active inbound-pending inbound-active outbound-pending; do
 extra=()
 if [[ $trial == inbound-* ]]; then extra=(--inbound); fi
 BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout -k 5 65 bash -ec '
 source build/agnocast-lyrical/install/local_setup.bash
 source build/rmw-agnocast/load-install/local_setup.bash
 source build/universe-sync/install/local_setup.bash
 source build/universe-pipeline/install/local_setup.bash
 export LD_LIBRARY_PATH=/ws/build/inbound:$LD_LIBRARY_PATH
 export CYCLONEDDS_URI="<CycloneDDS><Domain><General><Interfaces><NetworkInterface name=\"lo\"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address=\"127.0.0.1\"/></Peers></Discovery></Domain></CycloneDDS>"
 python3 experiments/rmw-agnocast/bridge-restart.py "$@"' bash "${trial#*-}" "$out/$trial" "${extra[@]}" >"$out/$trial.txt" 2>&1 || exit 1
done
```

Repeat in reverse order for round B. The second round additionally records
the kill-to-launch-exit-event interval for every crash. Across both rounds
all 16 automatic replacements succeeded. Each crash lost six frames at the
bridged consumer, with no duplicate, stale or reordered delivery. The
independent witness verified 100/100 in every run. The bridged consumer
verified 49 unique frames through sequence 60 before intentional shutdown.
All four active stops had a clean relay exit. All four pending stops
cancelled the scheduled fourth generation, including both directions.
Native endpoint counts returned to baseline and the DDS graph cleared
the bridge endpoint in each case.

Round B ranges across eight replacements were 2.526–3.656 ms from sending
SIGKILL to the observed exit event, 251.080–251.517 ms from exit event to
replacement start event, and 86.738–93.490 ms from start event to the
first valid consumer callback. Exit event to first frame was 337.899–344.656
ms. Last-good to first-recovered interarrival was 349.900–351.072 ms.
These are single-laptop observations with one explicit route and a fixed
delay. Process respawn handles observed crashes. Hung but live relays,
automatic topic selection, cross-host recovery and retry-budget exhaustion
remain outside this test. Use ordinary deployment supervision for the
explicit-route pilot; revisit route selection separately.

[Both run orders and raw evidence](../rmw-agnocast/bridge-supervision-results/)

## 2026-11-23 — Simultaneous ingress and egress on the launched three-LiDAR graph

Exercise two explicit supervised routes together with the existing stock
rclcpp three-crop container, Universe synchronizer, local sink and static TF.
Use the retained-input build with ENABLE_AGNOCAST=0 and runtime rmw_agnocast.
For the dual-route cases remap the source's top publisher to /dds/top.
This topic is outside AGNOCAST_RMW_TOPIC, so the existing source creates
a delegated Cyclone DDS publisher in the same process that publishes the
two local sensors. An r2a relay forwards it to /raw/sync/top. An a2r relay
mirrors /output to /dds/synchronized. A stock Cyclone SQLite rosbag2
recorder subscribes to both DDS topics. The local-only control publishes
all three streams over the native host-cloud route. Cyclone and Zenoh
local-only comparison measurements are in pipeline-launch-results.

Keep the original 24 acquisition groups, with left/right at 10 Hz and top
at 5 Hz on even sequences. The collector timeout is 40 ms. Wait for sink
readiness and the recorder subscriptions. After the seventh local output,
kill only the selected relay. Its own launch supervisor respawns it with
a 250 ms delay. Check child start/exit histories for both routes. First
run local-only, both healthy, ingress fault and egress fault, then reverse
that order. Use the same pilot sink instrumentation in the control.

For an ingress outage the verifier permits a left/right-only group on an
even sequence. It derives group size from the local cloud, then independently
checks the exact transformed point content, sequence of every point, sensor
identity, source offsets, timestamps, OK and TIMEOUT statuses and collector
success flag. Other conditions keep the scheduled source membership. Store
each verified local cloud in CDR after collection. After clean recorder
shutdown, read every bag record, verify all 12 top input sequences and stamps,
and compare each entire deserialized mirrored PointCloud2 with its local
counterpart. Reject gaps outside the failure interval, duplicates and
reordering. Match the first recovered complete group's source timestamps
against the source log. The two sides retain the original +1 and +2 ms
fixture stamp offsets for right and top. Local CDR files and bags stay under
the ignored build directory.

Reproduce on the reference laptop from the colcon worktree with the
Agnocast module loaded and the existing Lyrical overlays:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-pipeline.sh
out=build/reproduce-dual
modes="local healthy ingress egress"
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
mkdir "$out"
for mode in $modes; do
 BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout -k 5 75 bash -ec '
 source build/agnocast-lyrical/install/local_setup.bash
 source build/rmw-agnocast/install/local_setup.bash
 source build/rmw-agnocast/load-install/local_setup.bash
 source build/universe-sync/install/local_setup.bash
 source build/universe-pipeline/install/local_setup.bash
 export CYCLONEDDS_URI="<CycloneDDS><Domain><General><Interfaces><NetworkInterface name=\"lo\"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address=\"127.0.0.1\"/></Peers></Discovery></Domain></CycloneDDS>"
 python3 experiments/rmw-agnocast/dual-route.py "$@"' bash "$mode" "$out/$mode" >"$out/$mode.txt" 2>&1 || exit 1
done
```

Repeat with a fresh output directory and reversed modes. The guarded runs
use a 512 MiB runtime memory limit and no cgroup swap. The driver reports
per-trial median and nearest-rank p95 including cold samples. For comparison
against the earlier pipeline-launch-results convention, pool the two rounds
after dropping groups 0–3 of each. CPU is child user plus system seconds
through shutdown including graph verification, local snapshots, supervisors
and recording where active; offline bag readback is excluded. These are
whole-run costs, including startup, and do not isolate relay CPU.

Both run orders reproduced the same sequences. Healthy local and recorded
outputs were 24/24. Killing egress left all 24 local groups intact with the
usual 12 complete and 12 partial; the bag missed only groups 7 and 8. Its
separate top-input topic contained all 12 frames. Killing ingress left
all 24 synchronized outputs present on both paths. Group 8 then contained
only left and right with top marked TIMEOUT. Group 9 was its scheduled
two-sensor group. Group 10 was the first complete group after respawn.
Its three metadata stamps matched the group-10 source base time plus 0,
1 and 2 ms, and every transformed point carried sequence 10. The retained
synchronizer logged 59 input views for the ingress fault and 60 in every
other condition, with zero converter copies. Check the full per-trial
scope before interpreting these converter counts as pipeline copy costs.

For healthy operation, pooled post-warmup complete-cloud median/p95 at
the local sink were 2.629/3.646 ms local-only and 3.274/5.016 ms with both
routes and recording. Partial-cloud times were 41.793/42.389 and
41.756/42.642 ms respectively. At the recorder they were 3.790/5.693 ms
complete and 42.449/43.328 ms partial. CPU seconds for local-only were
1.719/1.560 and for both healthy routes 2.481/2.485. These small paired runs
show a measurable deployment cost and give limited precision on p95.
As earlier local-only historical controls, the same graph under Cyclone
measured 2.868/3.270 ms complete and Zenoh 2.765/3.594 ms; see the linked
baseline for configuration and CPU. The current study targets the explicit
Cyclone-DDS bridge. The ordinary stock launch without pilot variables also
passed its original 24-group verification after rebuilding the fixtures.

Use the explicit supervised route profile for this colcon pilot. The
ingress outage caused a valid partial observation rather than a stale
three-sensor result. The egress outage affected only the exported stream.
This covers one same-host graph and one failed route at a time. Cross-host
behavior, simultaneous relay failure and a downstream consumer's response
to a missing LiDAR remain separate questions.

[Dual-route samples and process logs](../rmw-agnocast/dual-route-results/) ·
[Earlier local-only control matrix](../rmw-agnocast/pipeline-launch-results/summary.txt)

## 2026-11-23 — Match a top-LiDAR omission against actual ingress failure through tracking

Use each previous dual-route round's healthy and ingress-fault SQLite bags.
Deserialize the synchronized outputs. Check encoded source identity, sequence,
channel and transformed xyz in the selected sensor segments. Map 12 selected
channels per sensor to a small shared rectangular landmark and another 12
top channels to a landmark eight metres away. This is a controlled visibility
scene constructed from the recorded clouds. The comparison control removes
only the healthy top segment at group 8 locally in replay. Compare projected
cloud bytes and sensor lists against the fault before clustering.

Use the existing built Universe Euclidean cluster and tracker overlays,
the fixture's rectangular shape estimates and static map transform. For
each group use the recorded fault stamp intervals shifted once to current
time; all three trackers receive precisely the same header stamps. A first
attempt at simulated time hit negative time during tracker startup. Three
independent tracker instances under a single guarded scope avoided that
startup artifact. Clustering is shared and requests are processed sequentially.
Random track UUIDs are checked within each tracker, with cross-condition
comparison by landmark and group. Reproduce both after the dual-route runs:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
for round in A B; do
  BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout -k 5 90 \
    bash experiments/rmw-agnocast/run-track-replay.sh coverage "build/dual-$round" || exit 1
done
```

Both trials had the same per-frame result. The shared landmark was detected
in all 24 groups. The top-only landmark was normally detected on 12 even
groups and was missed at group 8 in both omission and ingress replays.
First track confirmation remained at groups 1 and 2. In both affected
conditions the top track disappeared from output at groups 8–9 and returned
with its previous UUID at group 10, about 200 ms after group 8. The healthy
track remained present across these frames. Neither target switched identity.
This result concerns these two established static landmarks and the deployed
tracker parameters. [Per-frame results](../rmw-agnocast/coverage-replay-results/)

A second control exercised the actual local-only graph. Set SYNC_OMIT_TOP=1
for the earlier dual-route driver's local case and select a fresh directory.
The source omits its top publication only at group 8. The local sink still
checks every point and timeout status. Repeat the replay with the local
capture directory as its final argument, e.g.
`run-track-replay.sh coverage build/dual-A build/coverage-omit-A` under the
same Lyrical guard. For each group compare the entire per-sensor payload
segments of the local control with the fault bag, independent of segment
order and absolute run time. Verified equality in both rounds. The physical
source omission, replay omission and real bridge failure had the same
group-level result and per-target tracking histories. Source graph samples
and repeated tracker traces are in the same archive.

## 2026-11-23 — Sustained three-LiDAR perception with concurrent full-cloud recording

Use 600 acquisitions per run (one minute at 10 Hz, top at 5 Hz). Generate
32,768 XYZI/return/channel points per active sensor on demand. Run the three
real Universe crop filters and synchronizer including the 40 ms timeout.
The sustained sink checks every transformed point against its original sensor,
sequence and channel, then republishes the completed PointCloud2 on
/perception/cloud. The ongoing cluster adapter and SQLite recorder both
subscribe to that topic. Retain only the current output in the sink. This
avoids the short verification fixture's unbounded-for-this-purpose map of
completed groups. All cases verify 600 bag timestamps and sequences in
record order after shutdown. Acquisition stamps and callback timing are
also saved separately from the bag.

Use the same shared and top-only synthetic landmarks established in the
coverage replay. The adapter extracts small fixed patches from each existing
sensor segment and projects them to the two rectangular landmarks. Real
Universe Euclidean clustering and multi-object tracking run on the resulting
scene concurrently with the LiDAR graph. Check detections against group
membership; track both landmarks, checking within-run UUID continuity
and output stamp. The Python adapter and its request/response callbacks
contribute to end-to-end timing. Full-size clouds travel through crops,
synchronization, the fan-out topic and recording. Clustering operates on the
selected patches, not a full-density driving scene. All cases use the same
shape fixture. The native arm converts the shared output into a local CPU
message at the verifying sink. All arms forward an ordinary ROS cloud to
the common observation/recording leg.

Use the earlier native and non-native colcon overlays and the cluster/tracker
builds from the preceding replay. The native implementation uses bundled
CIE with the existing 1 ms wait control and Agnocast's native reliability
behavior. The stock arm uses the retained-input synchronizer adaptation
on rmw_agnocast with ordinary ROS executors; Cyclone and Zenoh use the same
ordinary graph. SYNC_AUDIT retains only weak owners for the teardown checks.
A first native preflight timed out: the executable came from the native
overlay but its shared synchronizer implementation resolved from another
overlay after a direct CMake install replaced a colcon environment hook list. Select the active universe_sync_benchmark package's own lib
directory first in LD_LIBRARY_PATH. Confirm actual native callback creation
and the final loan counters from the launched processes. This is an overlay
selection correction in the experiment runner.

From the worktree, with the Lyrical root, native kmod and the two previously
configured colcon build directories, rebuild the changed fixture in each:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash -ec '
 source build/agnocast-lyrical/install/local_setup.bash
 source build/rmw-agnocast/load-install/local_setup.bash
 source build/universe-sync/install/local_setup.bash
 export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21
 for name in universe-pipeline universe-pipeline-native; do
  export ENABLE_AGNOCAST=0
  if [[ $name == *-native ]]; then export ENABLE_AGNOCAST=1; fi
  colcon --log-base "build/$name/log" build --base-paths src/universe_sync_benchmark \
    --executor sequential --build-base "build/$name/obj" --install-base "build/$name/install"
 done'
for round in 1 2; do
 out="build/reproduce-sustained-$round"
 mkdir "$out"
 modes="native stock cyclone zenoh"
 if [[ $round == 2 ]]; then modes="zenoh cyclone stock native"; fi
 for mode in $modes; do
  BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout -k 6 140 \
    bash experiments/rmw-agnocast/run-sustained.sh "$mode" "$out/$mode" >"$out/$mode.txt" 2>&1 || exit 1
 done
done
```

For each run exclude the first twenty acquisitions from the latency summary;
report median and nearest-rank p95 separately for complete and timeout groups.
Time from the left source stamp to the live track callback. CPU seconds sum
child user/system time through shutdown and measured driver CPU during the
stream, excluding bag readback. Sample scope anonymous bytes every 50 groups
and at the last group; the whole-scope peak includes reclaimable recording
file cache and is influenced by the 512 MiB cap. It is not an RSS measurement.

Both trials for all four transports delivered 600/600 outputs to clustering
and tracking. Both landmarks retained their IDs once established. Native
Agnocast, rmw_agnocast and Zenoh also recorded 600/600 in both trials. The
second Cyclone bag retained 537/600, missing 188–222 and 519–546, ordered
and with valid retained content. The live perception reader received those
groups. The recorder log did not identify a cause. The initial bag verifier
used the row position as the expected sequence and stopped on a width
mismatch at the first gap. Read back independently by looking up each bag
header stamp in samples.csv, then verify actual sequence and width. Treat
this trial as a failed recording check and retain it in the control matrix.
A normal run with missing bag frames exits nonzero after reporting them.
After such a run, continue the remaining independent modes in fresh dirs.
The archived original failure and the readback result are both preserved.

Complete-group source-to-track pooled median/p95 were 4.606/5.433 ms native,
4.976/6.838 ms rmw_agnocast, 6.689/8.360 ms Cyclone and 5.316/7.492 ms
Zenoh. Partial groups include the collector timeout and were 43.748/44.605
ms native and 43.954/45.233 ms rmw_agnocast. The paired complete-group
rmw/native ratios were 1.072/1.097 and 1.088/2.556. In the second stock
run, the slowest 16 full groups all fell inside 222–254. In that window
both source-to-cloud and subsequent perception latency contributed. There
is no causal attribution to transport, scheduling or recording from these
timestamps. The bag gap in the Cyclone arm occurred in a separate run.
Whole-run CPU was 8.40–9.12 s native, 8.37–8.51 s rmw, 9.90–10.67 s
Cyclone and 10.97–11.42 s Zenoh. After group 100 sampled anonymous memory
was 213.5–216.3 MiB native and 226.6–229.7 MiB rmw. Lowest sampled host
MemAvailable in the matrix was 12,588 MiB. All scopes used the same cap;
file cache from recording brought peaks close to 512 MiB.

Stock reported 1,500 input views and all 1,500 owners released in each run.
Native also reported 1,500 input views, with 1,498 released at its converter
thread audit and all 1,500 released at its native owner audit on teardown.
Cyclone and Zenoh each reported 1,500 CPU inputs. These counters measure
ownership and conversion at synchronizer input, not downstream copy count.
See the [sample-level archive and summary](../rmw-agnocast/sustained-results/summary.txt).

After rebuilding both fixture packages with colcon, the original stock and
native launch modes each passed their 24-cloud and metadata regression
with sustained mode disabled. Open an opt-in Bazel pilot milestone in the
experimental roadmap; keep a clean-environment build and the variable tail
visible as follow-up work. No Bazel files were changed.
