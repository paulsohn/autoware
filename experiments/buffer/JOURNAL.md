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
