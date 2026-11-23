# Standalone workload replay on the laptop

Middleware source at 5f72b6ad, using the staged Lyrical colcon overlays and Agnocast 2.4.1 hook/device. No transport, rclcpp or application source changes. The standalone arm used the distribution prefix with Cyclone DDS and its RMW removed. The separate native control used the original native endpoint API with its normal Cyclone-backed ROS environment. All workloads ran sequentially in 512 MiB capped scopes. Lowest observed host MemAvailable was 18072 MiB.

## Single-hop loaned Path at 10 Hz

This first run used the unoptimized standalone qualification build. The [optimized comparison below](#correcting-the-path-build-comparison) is the performance baseline.

The original fixture publishes 64 poses with an 80-character frame ID per pose. Both sides verify the payload, previous retained sample and three matching addresses. Latency spans the timestamp immediately before publish to callback entry. All 240 frames per mode passed. The table pools the final 50 of 60 frames in each of four runs per mode, with alternating launch order. CPU and RSS are medians over runs and include startup and teardown. RSS is the largest individual child peak.

| Transport | Median µs | p95 µs | Child CPU s | RSS MiB |
|---|---:|---:|---:|---:|
| Native Agnocast | 44.91 | 77.75 | 0.147 | 25.9 |
| Standalone rmw_agnocast | 145.54 | 199.71 | 0.187 | 29.9 |

Standalone/native was 3.24× median and 2.57× p95. In paired run order the median ratios were 3.26, 2.97, 2.36 and 3.27. [All Path frames](path.csv). The [original mixed-RMW Path fixture](../../README.md#receiver-handoff-timing) also exceeded 1.2× on its ordinary receiver. The historical near-native three-LiDAR result concerns a different workload. The standalone reader has a native receiver thread and a separate application executor. Timing alone cannot attribute the difference to a particular wake, lock or dispatch stage.

## Nominal Universe three-LiDAR graph

Two launches per control in reverse order in the second round. Every launch verified 24 clouds with matching metadata. Twelve were deliberate partial-input timeout outputs. Pool the last 20 groups of each launch, ten per kind, and use nearest-rank percentiles as in the original graph study. CPU covers the whole launch.

| Transport | Full median/p95 ms | Partial median/p95 ms | Launch CPU s |
|---|---:|---:|---:|
| Native Agnocast, 1 ms wait | 2.552 / 3.576 | 41.838 / 42.844 | 1.61–1.68 |
| Cyclone DDS | 2.728 / 3.432 | 42.144 / 42.380 | 1.63–1.64 |
| Zenoh | 3.052 / 3.347 | 42.054 / 42.455 | 2.50–2.62 |
| Standalone rmw_agnocast | Publisher creation failed | No samples | Unmeasured |

The standalone synchronizer reported `standalone Buffer layout not implemented` at publisher creation and exited 134. The 10/50 Hz crop, overflow, scheduling and downstream perception cases requiring that same cloud transport have no standalone latency result. [All completed control frames](lidar.csv).

## Ownership, pressure and ROS behavior

The existing two-process loan and copy cases passed for Point, Path and Autoware Trajectory. Each exercised a retained first sample, a throwing callback, a copied publish and a final release from another thread after node teardown. Under depth-two pressure the fast reader took 1–11, the stalled reader took 1, 8 and 9, and the restarted reader took 11. Both topics in that fixture used the standalone RMW. The standalone functional suite passed ordinary, loaned, batch and serialized operations, services, graph, expired rosout and deadline checks. The fixed-size take probe saw zero C++ new calls on its taking thread across 32 warmed takes. Publisher heap allocations were outside the measurement.

## Reproduction

Use the Lyrical laptop prerequisites in the standalone README, the previously built Path and loadable probes, and the Universe native and ordinary overlays. From the worktree root run:

```bash
bash experiments/rmw-agnocast/standalone/bench-path.sh build/standalone/path-fresh
```

In the stripped Lyrical shell run the following. The last command checks ownership first, then reproduces the expected nonzero cloud startup exit.

```bash
bash experiments/rmw-agnocast/standalone/replay-ownership-and-cloud.sh
```

For pressure use the same stripped environment, probe, hook and overlays initialized by that script, then run the pressure block of run-loadable.sh with ROS_DISABLE_LOANED_MESSAGES=0. For the standalone ROS checks run standalone/check.sh against build/standalone/qualified-probe/topic_check in the sourced standalone runtime with the released hook preloaded.

For the independent graph controls, in the host worktree:

```bash
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
export BUFFER_RUNTIME_ONLY=1 SYNC_SHORT_WAIT=1
for mode in native cyclone zenoh zenoh cyclone native; do
  experiments/buffer/run-lyrical.sh timeout -k 5 65 bash -ec '
    TIMEFORMAT="graph_user_s=%U graph_sys_s=%S"
    time bash experiments/rmw-agnocast/run-pipeline.sh "$1"
  ' bash "$mode"
done
```

## Correcting the Path build comparison

The standalone qualification build used Clang 21 with an empty CMAKE_BUILD_TYPE. The effective flags for both rmw_agnocast and agnocastlib omitted optimization. The historical native comparator used an optimized Agnocast library. Keep the qualification row above as a measurement of that binary. Use the following same-source Release build for conclusions about optimized standalone performance.

Four additional rounds alternated standalone qualification/native/standalone Release and their reverse. Same probe executable, released hook, 60 frames per arm, first ten excluded. The native comparator kept its original support stack. Release changed both standalone libraries to Clang -O2 -DNDEBUG. All 720 frames in the primary comparison passed content, retained-sample and address checks.

| Original Path fixture | Median µs | p95 µs | Median child CPU s |
|---|---:|---:|---:|
| Native Agnocast control | 44.43 | 63.07 | 0.139 |
| Standalone qualification build | 149.64 | 211.29 | 0.190 |
| Standalone Release | 77.15 | 132.62 | 0.166 |

Release/native was 1.74× median and 2.10× p95. Release reduced the median by 48% versus qualification. Two additional native runs loading the exact same Release agnocastlib and patched rclcpp as the standalone process gave 49.68/82.93 µs. Those two runs followed the main comparison. This remains a one-host, single-hop estimate.

Two extra reader timing runs per standalone build in opposite order instrumented rcl_wait and rcl_take_loaned_message. Publication and admission timestamps come from the RMW message info; take return samples both realtime and monotonic clocks to align the benchmark timestamps. Every traced sequence had ordered boundaries and the intervals summed to the original elapsed time. The table gives per-stage medians across the 100 post-warmup frames per arm. Marginal medians need not sum to the end-to-end median.

| Interval | Qualification µs | Release µs |
|---|---:|---:|
| Benchmark publish timestamp to RMW sample header | 2.62 | 1.72 |
| Header to receive queue timestamp | 89.93 | 48.57 |
| Receive queue timestamp to rcl_wait return | 20.67 | 23.61 |
| Wait return to loan take entry | 11.44 | 4.88 |
| Loan take call | 11.67 | 1.67 |
| Take return to callback entry | 3.07 | 1.58 |

The current implementation admits a sample on the native receiver thread and wakes an application executor waiting on a condition variable. The observed 23.61 µs includes notification, scheduling and the wait-set checks. This split cannot isolate scheduler cost. The earlier near-native Universe measurements included node computation and used the mixed implementation. The current single-hop workload answers a different question. PointCloud2 support remains the prerequisite to measuring the standalone version of the Universe graph.

[Frame records including optional stage durations](path-build-comparison.csv)

To reproduce, enter the stripped Lyrical shell, source build/migration-guide/install/local_setup.bash and set the compiler and one-job settings as in the standalone build instructions. Use a separate optimized prefix:

```bash
export RMW_IMPLEMENTATION=rmw_zenoh_cpp
export XML_CATALOG_FILES=/ws/build/agnocast-lyrical/catalog.xml
colcon --log-base build/standalone/o2-log build --build-base build/standalone/o2-obj --install-base build/standalone/o2-install --paths src/core/agnocast/src/agnocastlib src/rmw_agnocast --executor sequential --cmake-args -DRMW_IMPLEMENTATION=rmw_zenoh_cpp -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG' -DBUILD_TESTING=OFF
incs=$(sed -n 's/^CXX_INCLUDES = //p' build/rmw-agnocast/load-obj/agnocast_rmw_loan_probe/CMakeFiles/path_benchmark.dir/flags.make)
/usr/bin/clang++-21 --gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 -O2 -shared -fPIC $incs experiments/rmw-agnocast/standalone/path_timing.cpp -o build/standalone/path-timing.so
```

From the host worktree:

```bash
bash experiments/rmw-agnocast/standalone/bench-path.sh build/standalone/fresh-build-comparison --compare-builds
```

## Does default Release close the single-hop gap?

Built agnocastlib and rmw_agnocast at e835e729 in a fresh o3 prefix using the o2 colcon command above, substituting o3 for o2 in the three directory names and omitting CMAKE_CXX_FLAGS_RELEASE. Both targets' flags.make recorded Clang 21 with -O3 -DNDEBUG. Reused the same probe executable, patched rclcpp and matching heap hook. Both standalone arms used the stripped Lyrical runtime. The native control retained the original optimized native stack.

```bash
bash experiments/rmw-agnocast/standalone/bench-path.sh build/standalone/fresh-release-levels --compare-release
```

Each of six rounds used a different permutation of native, O2 and O3. All 360 frames per arm passed the content, retained-sample and shared-address checks. For the latency statistics, pool the last 50 frames of each run (300 per arm). CPU is median reader plus writer child CPU per launch, including startup and teardown.

| Arm | Median µs | Nearest-rank p95 µs | Median child CPU s |
|---|---:|---:|---:|
| Native Agnocast | 43.40 | 64.10 | 0.131 |
| Standalone -O2 | 77.85 | 125.67 | 0.170 |
| Standalone -O3 | 84.55 | 145.77 | 0.171 |

O3/native was 1.95× median and 2.27× p95. Every O3 run exceeded the paired native median and p95 by over 1.2×. O3 had a higher median than O2 in four rounds and a lower median in two. This laptop experiment establishes no consistent latency gain from switching to O3. Lowest observed host MemAvailable was 15238 MiB during the build and 13629 MiB during the trials. [All frame measurements](path-release-levels.csv)
