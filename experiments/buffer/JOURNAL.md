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
