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

## Isolate the receiver-thread handoff

Used the same optimized standalone writer, RMW, 10 Hz Path message and patched typed-loan dispatch for all ROS arms. The fixed-subscription controls both register an arrival callback. The handoff arm signals a condition variable and the waiting application thread executes the subscription. The inline arm executes the subscription on the native arrival thread before signalling. The main thread blocks in both cases. Inline bypasses ordinary executor selection and callback-group arbitration and runs application work in the middleware notification callback. It is a diagnostic for one subscription.

Four balanced rounds yielded 240 verified frames per arm. Independent six-round trials compared the general-purpose one-worker EventsCBGExecutor against the ordinary SingleThreadedExecutor and native. Each mode in that comparison verified 360 frames. Percentiles pool the last 50 frames per launch. CPU is median total child CPU seconds per run.

| Fixed-subscription comparison | Median µs | p95 µs | CPU s |
|---|---:|---:|---:|
| Native | 46.33 | 84.54 | 0.137 |
| Ordinary executor | 78.20 | 133.71 | 0.169 |
| Fixed subscription, blocking handoff | 74.51 | 121.26 | 0.168 |
| Fixed subscription, inline take and callback | 55.93 | 88.57 | 0.168 |

Inline had a lower median than the fixed handoff in each paired round. Its pooled median was 18.58 µs lower. Relative to the same-series native control it reached 1.21× median and 1.05× p95. This includes scheduling and signalling costs in both configurations and does not isolate a kernel context-switch time. A preliminary four-round series showed a similar fixed-handoff/inline difference (73.94/56.36 µs median). Its driver returned a shell error after completing all frame files because it was edited during execution; the reported final matrix was repeated successfully with the final binary and unmodified runner.

| Ordinary ROS executor comparison | Median µs | p95 µs | CPU s |
|---|---:|---:|---:|
| Native | 45.50 | 65.05 | 0.131 |
| SingleThreadedExecutor | 81.35 | 112.72 | 0.173 |
| EventsCBGExecutor, one worker | 76.81 | 107.28 | 0.172 |

The stock event executor still schedules work onto an executor thread. The smaller median improvement leaves it at 1.69× native on this workload. The next comparison should let the ordinary executor's blocking wait observe native subscription descriptors and acquire ready samples on that executor thread. Retain wakeups for guards, shutdown, services and other wait-set entities. Check callback-group behavior and simultaneous arrivals before considering that path for general use.

To reproduce on the provisioned laptop, build the probe in its existing Lyrical build directory, then run both matrices from the worktree root. All launches ran sequentially with the memory guard (minimum available host memory 11502 MiB).

```bash
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
experiments/buffer/run-lyrical.sh bash -ec 'source build/rmw-agnocast/install/local_setup.bash
cmake --build build/rmw-agnocast/load-obj/agnocast_rmw_loan_probe --target path_benchmark -j1'
bash experiments/rmw-agnocast/standalone/bench-path.sh build/standalone/fresh-fixed-handoff --compare-handoff
bash experiments/rmw-agnocast/standalone/bench-path.sh build/standalone/fresh-events --compare-events
```

[All 2,040 frames from the two final comparisons](path-handoff.csv)

## Replay the receiver handoff with plain Release

Plain CMake Release is the default for new colcon performance runs. Clang 21 chose -O3 -DNDEBUG for agnocastlib, rmw_agnocast, patched rclcpp and the Path benchmark. Reused the verified o3-install transport libraries and built rclcpp and the fixture in a fresh prefix. Each launch checked the Agnocast and rclcpp paths reported by ldd. The native arm used the direct Agnocast API with its customary ROS support environment. The standalone arms ran in the Cyclone-stripped distribution. All arms used the released 2.4.1 heap hook and the same kernel device. System packages retained their distribution builds.

| Fixed-subscription comparison | Median µs | p95 µs | Child CPU s |
|---|---:|---:|---:|
| Native Agnocast | 45.82 | 69.80 | 0.129 |
| SingleThreadedExecutor | 76.76 | 171.63 | 0.168 |
| Fixed subscription waking the application thread | 73.17 | 124.56 | 0.164 |
| Fixed subscription executing inline | 50.58 | 90.00 | 0.166 |

The fixed handoff to inline change saved 22.59 µs in pooled median and improved the median in each paired round. Inline/native reached 1.10× median and 1.29× p95. The previous inline diagnostic measured 55.93/88.57 µs against 46.33/84.54 µs native. Its absolute p95 was similar; the native p95 moved. These are separate laptop series with different builds and runtime conditions.

| General-purpose executor comparison | Median µs | p95 µs | Child CPU s |
|---|---:|---:|---:|
| Native Agnocast | 42.10 | 70.21 | 0.125 |
| SingleThreadedExecutor | 76.44 | 142.29 | 0.171 |
| EventsCBGExecutor, one worker | 75.58 | 157.44 | 0.170 |

Used the same 10 Hz workload, the same six balanced rounds for the general executors and four rounds for the fixed-reader study. All 2040 frames passed the retained-sample, nested-address and payload checks. Pool the final 50 frames per round and report median and nearest-rank p95. Child CPU includes startup and teardown. The memory guard recorded at least 13769 MiB available during the new build and 14134 MiB during the launches. The inline case remains a single-subscription diagnostic. Ordinary callback-group scheduling and non-subscription wakeups are still needed for a general executor-side receive path.

On the provisioned laptop prepare build/standalone/o3-install as in the default Release experiment above. Then run:

```bash
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/standalone/build-release-path.sh
bash experiments/rmw-agnocast/standalone/bench-path.sh build/standalone/fresh-release-events --compare-events
bash experiments/rmw-agnocast/standalone/bench-path.sh build/standalone/fresh-release-handoff --compare-handoff
```

To reproduce the historical O2 handoff experiment use the runner at 4bc5899c. [All plain-Release frame measurements](path-handoff-release.csv)

## Callback isolation on the single-hop Path workload

Compare the two CIE implementations against the Release native and SingleThreadedExecutor controls. The application subscription uses the default mutually exclusive ROS callback group. Standalone AWF CIE assigns it a child SingleThreadedExecutor; the bundled CIE also selects that executor for an ordinary ROS group. Both therefore keep the native receiver-to-ROS-worker handoff. No affinity, real-time priorities or competing workload were configured.

| Reader | Median µs | p95 µs | Child CPU s |
|---|---:|---:|---:|
| Native Agnocast control | 46.34 | 71.79 | 0.137 |
| Standalone RMW, SingleThreadedExecutor | 76.02 | 125.07 | 0.179 |
| Standalone RMW, standalone AWF CIE | 84.04 | 137.02 | 0.192 |
| Standalone RMW, Agnocast-bundled CIE | 77.93 | 120.54 | 0.188 |

Each reader passed all 240 frames, including loan addresses, payloads and retention. The table pools 200 frames per reader after ten warmups per launch in four balanced rounds. CPU is median combined child CPU per launch. Minimum available host memory across timed runs was 13856 MiB.

The standalone AWF CIE build uses subtree revision 06713baf plus the local Lyrical port. Unmodified announcement QoS failed at startup on this RMW: both CIE variants request reliable transient-local KeepAll for callback-group metadata; standalone rmw_agnocast supports KeepLast. Only in this diagnostic executable, replace history on those two announcement topics with KeepLast(16). Per-launch logs confirm one override for each CIE reader and none for control arms. Path QoS and loan semantics are unchanged. This does not qualify arbitrary late-starting configurators or deployments requiring unbounded announcement history.

On the provisioned laptop with the ordinary Release experiment built:

```bash
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/standalone/build-cie-path.sh
bash experiments/rmw-agnocast/standalone/bench-path.sh build/standalone/fresh-cie --compare-cie
```

[All 960 frames](path-cie.csv). For a separate question about scheduling under competing callbacks, see the [earlier Universe saturation experiment](../../pipeline-saturation-results/summary.txt).

## Blocking rmw_wait on native notification descriptors

Can the ordinary SingleThreadedExecutor keep its callback scheduling and avoid waking a second thread for each Path? In the experimental RMW, selecting a subscription by exact ROS topic with `AGNOCAST_RMW_POLL_TOPIC` leaves its receiver thread unstarted. A mixed wait set blocks in Linux ppoll on each selected native route and a private eventfd for context notifications. After a native wake the waiting thread drains the native executor, queues the loan and reports subscription readiness. rclcpp then selects the callback group and takes the loan normally. A take issued without a preceding wait can also acquire data. The opt-in is limited to wait-set executors. Registration of an asynchronous new-message callback on the selected subscription returns unsupported. Other subscriptions and internal service endpoints retain their receiver threads.

| 10 Hz Path reader | Median µs | p95 µs | Child CPU s | Peak child RSS MiB |
|---|---:|---:|---:|---:|
| Native Agnocast | 45.60 | 80.02 | 0.130 | 26.0 |
| Standalone RMW, receiver thread | 85.47 | 132.02 | 0.173 | 25.9 |
| Standalone RMW, blocking direct wait | 78.90 | 116.42 | 0.169 | 26.0 |

Both RMW arms loaded the same new Release library in the Cyclone-stripped distribution. Only the selected topic differed. All six launch orders ran serially. Each arm passed 360/360 frames with the payload, retained loan and all three addresses checked. The table pools 300 frames per arm after ten warmups per launch and uses nearest-rank p95. CPU and RSS are medians over six launches. Minimum available host memory across timed launches was 12124 MiB.

Direct wait improved the median and p95 in each paired round. Pooled latency was still 1.73× native median and 1.45× native p95. Native per-round p95 ranged from 62.64 to 206.79 µs. This measures the whole intervention on one laptop. It cannot assign the remaining time to ROS scheduling alone. The direct arm still enters a second nonblocking native epoll wait after ppoll identifies a ready descriptor, updates a queue, and walks the ordinary ROS executor path. A follow-up can time these stages and compare dispatch by the already-ready route. Keep the mode experimental pending multiple active readers, competing callback groups, and larger graph measurements.

From the worktree on the provisioned laptop, reuse the Release Path dependencies above. The build uses Clang 21, one job and the default Release optimization flags. The functional probe blocks with a selected subscription present and exercises timeout, guard, simultaneous guard and data, service request, deadline and shutdown. It also takes a second sample without calling wait. An external timeout bounds each test.

```bash
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/standalone/build-poll.sh
bash experiments/rmw-agnocast/standalone/bench-path.sh build/standalone/fresh-poll --compare-poll
experiments/buffer/run-lyrical.sh bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws bash experiments/rmw-agnocast/standalone/check-poll.sh
```

[All frame latencies](path-poll.csv) and [child resources by launch](path-poll-resources.csv).

## Cyclone-free Universe cloud transport

At faefefe5 the standalone RMW rejected Buffer-bearing types at endpoint creation. Reused the existing HostCloud representation for CPU PointCloud2. The writer copies metadata and point bytes into an Agnocast allocation. A typed reader restores a process-local message with its own CPU Buffer. The synchronizer also needs ConcatenatedPointCloudInfo, whose matching_strategy_config is a byte sequence. Its endpoint uses serialized CDR bytes in an Agnocast allocation. Each representation has a distinct route suffix. The two types use ordinary ROS publish/take and serialized publish/take. Neither advertises application loans or content filtering. Other Buffer-bearing types retain their existing restrictions. No Universe node source changes were required.

Two laptop rounds in forward and reverse order used the nominal Universe three-crop graph, including the static transform, synchronizer, and a sink checking point contents, frame, shape, timestamps, source metadata and timeout status. Each launch emitted 24 groups from 1,966,080 input points. The sink verified all 240 outputs across the ten launches. Drop the first four groups per launch, then pool 20 complete and 20 partial groups per arm. Latency runs from the source timestamp to sink callback. Report median and nearest-rank p95. Native kept the historical 1 ms wait. Standalone used the ordinary ROS executor with shared-memory transport throughout, including the component loader. The poll arm selected only the sink topic /output. The other graph edges kept the receiver-thread implementation.

| Three-LiDAR transport | Complete median / p95 ms | Partial median / p95 ms |
|---|---:|---:|
| Native Agnocast | 2.745 / 3.644 | 42.078 / 42.551 |
| Standalone RMW | 5.772 / 7.613 | 41.601 / 41.990 |
| Standalone RMW, direct wait on /output | 5.524 / 6.309 | 41.543 / 41.801 |
| Cyclone DDS | 3.182 / 3.638 | 42.120 / 42.383 |
| Zenoh | 2.637 / 2.932 | 41.967 / 42.136 |

The full-input standalone/native median ratios were 2.10× and 2.01×. The configured 40 ms timeout dominates partial ages. Native synchronizer instrumentation reported 60 input views per launch and zero copies. Both standalone arms reported 60 copies and zero views. These counters describe the synchronizer conversion path only. They motivate restoring retained views; the timing does not isolate their cost. Minimum sampled host MemAvailable across this matrix was 12504 MiB. [All three-LiDAR samples](universe-standalone-lidar.csv)

On the provisioned laptop, reuse the Universe and Release Path colcon overlays documented above. The new RMW builds in the poll overlay with plain Release. Both launchers run one graph at a time in a 512 MiB runtime scope. The standalone Lyrical distro excludes Cyclone DDS and its RMW. The standalone three-LiDAR runner selects the existing launch file's ordinary ROS graph (its mode argument is named cyclone) and explicitly selects rmw_agnocast for the launcher and child processes.

```bash
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/standalone/build-poll.sh
bash experiments/rmw-agnocast/standalone/bench-universe.sh build/standalone/fresh-universe-cloud
bash experiments/rmw-agnocast/standalone/bench-crop.sh build/standalone/fresh-crop-cloud
export BUFFER_RUNTIME_ONLY=1
experiments/buffer/run-lyrical.sh bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws timeout -k 8 65 bash experiments/rmw-agnocast/standalone/run-universe.sh --check
```

### Replay the original two-edge crop fixture

The original 10 Hz Universe crop-box workload isolates source → crop → sink. Reused the historical crop executable and input geometry. The new standalone arm uses its ordinary ROS subscriptions and CPU cloud copies on both edges. The historical 1.22× arm borrowed both publisher outputs and retained host views at both readers. Those ownership optimizations still need qualification in the standalone RMW. The native and standalone arms here used the same Release Agnocast userspace and released heap hook. Two rounds ran in opposite orders. All 480 frames passed. Pool the last 50 frames per launch for 100 per transport. Combined CPU covers all children from start to exit.

| Two-edge crop transport | Median / p95 ms | Child CPU s per launch |
|---|---:|---:|
| Native Agnocast | 0.345 / 0.632 | 0.559–0.560 |
| Standalone RMW | 0.912 / 1.137 | 0.725–0.747 |
| Cyclone DDS | 1.375 / 1.611 | 0.725–0.725 |
| Zenoh | 1.063 / 1.396 | 1.406–1.563 |

Current standalone/native was 2.64× median and 1.80× p95. Compare to the historical mixed loan/view setup as a separate configuration. The current replay cannot isolate the effect of removing the fallback. Minimum host availability during these launches was 12814 MiB. The additional standalone checks passed typed and serialized publish/take for both new types from rclpy, followed by the existing standalone regression suite. [Crop samples](universe-standalone-crop.csv) and [child resources](universe-standalone-crop-resources.csv).

## Retained cloud views and publisher loans in the standalone RMW

The receiver opt-in requires a C++ PointCloud2 subscription that accepts `agnocast_host_view` or `any`, plus `AGNOCAST_RMW_HOST_VIEW=1`. Metadata is reader-local. The read-only Buffer aliases the point vector in the native loan. Its owner keeps the native registrations and node alive through final release, while the ROS subscription and executor can shut down. A copy of the view yields independent, mutable CPU storage. CPU-only and serialized subscribers keep their previous behavior.

A CPU PointCloud2 publisher may opt into the standard borrow/fill/publish API with `AGNOCAST_RMW_HOST_LOAN=1` and ROS loans enabled. On publication, metadata is rebuilt in the writer pool and an existing payload is moved only when its vector allocation belongs to the writer's shared mapping. Other CPU allocations are copied under the loan. Abandoned loans destroy the original ROS message. These experiments exercised same-thread borrowing and publication, with cross-thread final release on the reader.

The two-process contract covered both ordinary publication and borrowed publication. It compared payload addresses at the two endpoints, retained a cloud beyond depth-four history eviction and subscription teardown, shut down ROS, then released the view on another thread. The same publisher served a CPU-only typed reader and a serialized reader. The borrowed arm abandoned one loan, published eight clouds and detected exactly one intentionally private vector requiring a copy. The standalone cloud codec and regression suites passed.

### Two-edge crop at 10 Hz

Four position-balanced rounds on the laptop. All standalone readers used the ordinary ROS executor and explicit typed subscriptions. The copy arm accepted CPU storage, and the view arms requested the retained view. The loan arm borrowed on both edges after the first frame; each publisher reported 59 loans and zero private payload copies in every round. Pool the final 50 frames of each launch for 200 timed frames per arm. All 960 frames passed. CPU and peak RSS are launch medians. Minimum host MemAvailable was 12927 MiB in the timed runs.

| Transport | Median ms | p95 ms | Child CPU s | Peak child RSS MiB |
|---|---:|---:|---:|---:|
| Native Agnocast | 0.357 | 0.595 | 0.557 | 138.6 |
| Standalone, CPU copies | 0.913 | 1.153 | 0.737 | 134.1 |
| Standalone, retained views | 0.479 | 0.661 | 0.713 | 134.8 |
| Standalone, publisher loans and retained views | 0.442 | 0.881 | 0.702 | 138.0 |

Views brought standalone/native to 1.34× median and 1.11× p95. Adding publisher loans changed those to 1.24× and 1.48×. Median improved and p95 increased against views alone in each of the four rounds. The results establish the tradeoff for this fixture; attributing the tail to allocation, scheduling or another publishing stage requires timing that stage. [All crop frames](crop-ownership.csv) and [resource measurements](crop-ownership-resources.csv).

From the provisioned worktree use the same colcon overlays and memory guards as the CPU copy baseline. Rebuild the RMW, verify ownership in the stripped distribution, then run the comparisons sequentially.

```bash
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/standalone/build-poll.sh
export BUFFER_RUNTIME_ONLY=1
experiments/buffer/run-lyrical.sh bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws timeout -k 3 65 bash experiments/rmw-agnocast/standalone/run-universe.sh --ownership
bash experiments/rmw-agnocast/standalone/bench-crop.sh build/standalone/fresh-ownership-crop --ownership
bash experiments/rmw-agnocast/standalone/bench-universe.sh build/standalone/fresh-ownership-lidar --ownership
```

### Three-LiDAR synchronizer ownership

Only the synchronizer opts in. Its three cloud subscriptions request retained input views. The loan arm also reserves its output payload in shared storage and borrows its output message for publication. Source, crops and sink retain the ordinary standalone copy path. Each standalone launch used a stock ROS executor. Each view launch verified 60 views and zero input copies, and each loan launch confirmed 24 published loans and zero private payload copies. Two opposite-order rounds verified all 288 output clouds and associated metadata. Four warmup groups per launch were excluded, leaving 20 timed complete groups and 20 partial groups per transport. The 40 ms synchronization timeout dominates partial output ages.

| Transport | Complete median / p95 ms | Partial median / p95 ms |
|---|---:|---:|
| Native Agnocast | 2.437 / 3.461 | 41.737 / 42.429 |
| Standalone, CPU copies | 6.331 / 7.645 | 41.618 / 42.255 |
| Standalone, synchronizer views | 5.596 / 6.878 | 41.679 / 42.100 |
| Standalone, synchronizer views and output loans | 6.254 / 7.883 | 42.073 / 42.397 |
| Cyclone DDS | 2.905 / 3.351 | 42.032 / 42.388 |
| Zenoh | 2.921 / 3.160 | 42.044 / 44.806 |

Minimum host MemAvailable across launches was 12573 MiB. [All three-LiDAR samples](lidar-ownership.csv).
