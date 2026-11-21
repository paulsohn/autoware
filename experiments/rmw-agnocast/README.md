# Whole-message loans with Agnocast storage

The current pilot decision and latency milestone are recorded in
[Pilot bridge choice and interpretation of performance](#pilot-bridge-choice-and-interpretation-of-performance).

The [bounded Universe CUDA edge milestone](../buffer/README.md#bounded-cuda-lidar-edge-milestone) tracks GPU transport separately from the CPU synchronizer output experiment.

The target deployment uses standard ROS message types and rclcpp publishers and
subscriptions, selects an rmw_agnocast library through RMW_IMPLEMENTATION, and
preloads a matching Agnocast heap hook on hosts with its kernel module. This
first experiment exercises the loan boundary in isolation. The probe interposes
seven RMW entry points inside the test executable. Cyclone supplies discovery,
ordinary entities and an empty wakeup message for its wait set. A private native
Agnocast topic carries the complete Autoware trajectory. The bridge is off.

## First experiment

The Lyrical userspace contains ros-lyrical-rclcpp
32.0.3-1resolute.20260915.061505. The initial squashed import at src/rclcpp is
ros2/rclcpp tag 32.0.3, commit 9a1e199abeea7ac07363d5c890bdf9078d2d3b3d.
To update deliberately, select a tested release and run
`git subtree pull --prefix=src/rclcpp https://github.com/ros2/rclcpp.git <tag> --squash`
from the experimental branch. The original behavior is also tracked by
[ros2/rclcpp#2401](https://github.com/ros2/rclcpp/issues/2401).

The executor gives a successfully taken sample one shared owner. Its deleter
returns the loan through the original subscription handle, which keeps the
node handle and its context alive. Synchronous raw loan dispatch remains
available through the existing virtual function. The rebuilt typed
subscription picks up that owner during dispatch. The executor's scoped
thread-local slot restores the previous owner on nested calls and exceptions.
The application-visible API and the base-class layout are unchanged.
Typed subscriptions must be rebuilt against the patched headers for retained
ownership. The opt-in for subscriber loans is ROS_DISABLE_LOANED_MESSAGES=0.

The fixture uses the generated autoware_planning_msgs/Trajectory from the
worktree. A standard publisher borrows a message, grows its points vector past
capacity, fills 129 points and a 96-character frame ID, and publishes the loan.
A standard ConstSharedPtr callback checks every point and compares addresses for
the message, reallocated vector data and string data against the writer. Address
records and acknowledgements in a private temporary directory synchronize the
test. No ROS Buffer is involved.

The stock control counts returns and avoids reading A after the executor has
returned it. The patched run retains A while receiving B and a third sample C
whose callback throws. After removing the node from the executor and resetting
the subscription and node, a different thread releases A. The fixture checks
the return count and the lifetime of the underlying RCL subscription handle.

## Reproduction

Use the exported Lyrical root, matching native module, heaphook, Agnocast
colcon install and local XML schema catalog from the preceding
[buffer experiments](../buffer/JOURNAL.md). Build and run serially:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-rclcpp.sh
for mode in stock patched; do
  experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-probe.sh "$mode"
  for run in 1 2 3; do
    BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh bash \
      experiments/rmw-agnocast/run-probe.sh "$mode"
  done
done
```

The stock fixture is built against installed ROS headers, and the patched one
against the subtree overlay. The runner prints the resolved librclcpp before
each invocation. Both builds pass the original upstream subscription-loan
opt-in through to RCL. The probe exposes one native writer and one native
reader for one topic and message type.

## Next decision

Replace executable symbol interposition with a loadable RMW and exercise
native readiness through a ROS wait set. Compare the same whole-message loan
and ordinary-copy paths through standard application entry points. Further
coverage includes retained history under pressure, multiple subscriptions,
callback types, teardown races, shutdown with outstanding loans, and serialized
clients. The existing Agnocast allocator has thread-local borrow state; moving
a publisher loan to another thread requires a separate test. Lyrical types
with polymorphic Buffer members still require separate representation work.
The trajectory probe supplies no performance result or general message-type
coverage. Keep Bazel adoption separate until the transport is validated.

## Results and upstream checks

All three stock runs returned A at callback exit while the application held it.
All three patched runs preserved A across B and C, returned C on exception,
and returned A exactly once on release by another thread after the ROS
subscription and node were reset. The stock and patched builds used the same
generated Autoware messages and native kernel module. Builds ran with one job;
the lowest recorded host MemAvailable across the final builds and probe runs
was 5,216 MiB. [Run observations and test counts](ownership-results.txt).

For the focused upstream suites, make the matching Lyrical packages test-msgs,
performance-test-fixture, mimick-vendor and ament-cmake-google-benchmark and
their dependencies available inside the root. In this experiment their eight
apt archives were verified against the root's Packages index SHA-256 values
and extracted using `dpkg-deb -x` into `build/rmw-agnocast/test-deps`, keeping
the original /usr and /opt layout. With LYRICAL_ROOT set as above, run:

```bash
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/test-rclcpp.sh
```

The upstream loaned-message, subscription and executor test binaries all
passed with Cyclone DDS. An initial subscription test picked up the stock
shared library at runtime and failed symbol resolution for share_dispatched_loan.
The runner now puts the built library first in LD_LIBRARY_PATH. Full upstream
suite qualification, other RMWs and performance remain outside this first
ownership result.

## Loadable local transport and migration plan

The second experiment adds a selectable rmw_agnocast shared library. Select
the exact resolved topic with AGNOCAST_RMW_TOPIC. Its native path currently
recognizes the C++ Trajectory type support handle and volatile KeepLast QoS.
Each native subscriber runs an Agnocast executor thread that queues loans and
signals a Cyclone guard condition. Cyclone supplies graph handles, the
underlying wait set and delegated RMW operations. The client uses standard
rclcpp calls, and only the Agnocast heaphook is explicitly preloaded.

Native payload delivery requires both endpoints to run on the same host with
access to the same /dev/agnocast device instance and compatible userspace.
This is also the requirement for containers. A remote or ordinary DDS peer
requires a bridge for a native topic. The prototype creates DDS graph entities
for native endpoints, so a match count can include an unreachable peer.
Automatic versus operator-managed bridge creation remains an open decision.
All native baseline runs explicitly disable the bridge.

Using the exported Lyrical root and the message, Agnocast and patched rclcpp
overlays from the first experiment, reproduce the loadable probe with:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
for n in 1 2 3; do
  BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout 50s bash experiments/rmw-agnocast/run-loadable.sh
done
```

Remaining work, in order:

1. The [two-reader experiment](#two-reader-pressure-and-restart) covers slow
   consumption, queue depth, graceful restart, shutdown with an outstanding
   loan and mixed waits. Outstanding RMW contract work includes metadata,
   error paths and explicit handling of unsupported operations.
2. Replace the Trajectory specialization with a type support mechanism. Test
   flat and nested dynamic messages first. Qualify types containing Buffer
   separately. Verify QoS matching and graph counts against actual delivery.
3. Test the boundary with separate processes sharing the device, an isolated
   device environment and a second host. Verify actual callbacks with bridging
   disabled, then exercise an explicitly launched bridge in both directions.
   Evaluate automatic startup against manual startup for duplicate delivery,
   bridge restart, discovery and cost. Decide the default from that evidence.
4. Benchmark the existing Universe pipeline against native Lyrical Agnocast.
   Retain Cyclone DDS, Zenoh and applicable Buffer rows, including the documented
   Cyclone shared-memory caveat. Separate CIE and bridge configurations.
   Require both median and p95 within 1.20 times native and compare CPU, memory
   and drops directly before considering a Bazel milestone.

## Two-reader pressure and restart

Use the same Lyrical root, module, heap hook and patched rclcpp overlay as
the loadable probe above. Rebuild with build-loadable.sh. The 'pressure'
argument starts the writer, a fast reader and a slow reader in separate
processes. Both readers use volatile KeepLast(2). The writer waits for each
fast callback. The slow application retains sample 1 and pauses its executor
until after 9, allowing 100 ms for its native receiver to catch up. This
controls queue pressure independently of the publication rate.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
for n in 1 2 3; do
  BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout 60s bash experiments/rmw-agnocast/run-loadable.sh pressure
done
```

The fast reader received 1 through 11. The slow reader received 1, 8 and 9.
All native callbacks checked object, vector and string addresses plus every
point. After removing its ROS subscription and node and shutting down its ROS
context, the slow reader rechecked 1 and released it on a different thread.
The writer observed the graph return to one reader and published 10. The fast
reader also received a delegated Cyclone control topic in its native executor
wait set. A fresh process with the original slow node name then received 11
as its first sample. This tests graceful restart and a fixed-depth queue.
The checks do not distinguish where in the receive path samples 2–7 were
discarded. An abrupt process exit and sustained throughput remain untested.

To observe the kernel boundary, run a compatible strace inside the Lyrical
root. On this laptop the host binary could use the root's installed libraries:

```bash
w=build/rmw-agnocast
cp /usr/bin/strace "$w/strace"
trace=$(mktemp -d "$w/pressure-trace.XXXXXX")
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout 60s "$w/strace" \
  -ff -e trace=openat,ioctl -o "/ws/$trace/thread" bash experiments/rmw-agnocast/run-loadable.sh pressure
awk '/"final_release_begin"/,/"final_release_end"/' "$trace"/thread.*
```

On the final-release thread the interval contained exactly two successful
ioctls, in order. Command 0x06 released the subscriber reference and command
0x11 removed the subscriber (Agnocast ioctl group 0xa6). This observes the
subscriber reference return directly. Publisher-side heap reclamation timing
is a separate measurement.

## Type support and the Universe decision

The current shared library accepts an exact colon-separated list of resolved
topic names in AGNOCAST_RMW_TOPIC. Point, Path and Trajectory use the installed
C++ introspection callbacks with Agnocast's type-erased publisher and reader.
An application builds its usual C++ message and introspection support and uses
standard rclcpp calls. Applications retaining a received loan still need the
patched rclcpp headers. The ordinary-copy and recorder paths use Cyclone's CDR
serializer. Generic serialized publication, batch take and native message
metadata remain unqualified. The contract probe found a reliable subscriber
receiving a best-effort publication despite an incompatible DDS graph match.

The opt-in AGNOCAST_RMW_BUFFER_CDR=1 path handles Buffer-containing types
by serializing at the writer, copying the bytes into one Agnocast allocation
on a private topic, and deserializing into a local object at the reader.
It disables object loans on these entities. The source/crop/sink binaries use
their existing ROS path. Both input and output were selected. Each candidate
round logged one CDR reader and writer per topic. Only CPU PointCloud2 was
qualified with this representation. The ordinary native Agnocast reference
uses its existing CPU-vector view and Agnocast-only CIE.

With the Lyrical root and overlays above, run the sequential benchmark:

```bash
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh bash \
  experiments/buffer/bench-universe-lyrical.sh build/rmw-agnocast/fresh-matrix --compare-rmw
```

Three process groups per timed row sent 60 frames each at 10 Hz. All 180
frames per row passed the sink's point/order/frame checks. Latency pools the
last 50 frames per group. CPU is median aggregate child user plus system
seconds per run, including startup and teardown. RSS is the median largest
child peak, in MiB. Zenoh includes its router. All bridges were disabled.

| Universe path | Median ms | p95 ms | CPU s | Peak RSS MiB |
|---|---:|---:|---:|---:|
| Native Agnocast | 0.372 | 0.633 | 0.550 | 138.2 |
| rmw_agnocast, CDR over Agnocast | 0.972 | 1.293 | 0.683 | 137.2 |
| Cyclone DDS | 1.281 | 1.819 | 0.718 | 132.8 |
| Zenoh | 1.114 | 1.783 | 1.481 | 288.3 |
| Zenoh + shmem Buffer | 0.823 | 1.204 | 1.446 | 287.7 |
| Zenoh + CUDA Buffer | 1.225 | 1.543 | 1.449 | 287.9 |
| Cyclone DDS + shmem Buffer | — | — | — | — |

The final row retains the earlier incompatible Core result with loopback
and iceoryx PSMX (0/60 in each). It has no Universe timing.
[Previous compatibility evidence](../../src/crop_pipeline_benchmark/two-edge-results/cyclone-matrix.tsv).

The limits derived from unrounded native latencies were 0.447 ms median and
0.760 ms p95. The serialized candidate measured 2.61× and 2.04× native with
about 24% more child CPU. Keep the Bazel migration milestone closed. These
results measure the cost of the entire deployed paths, including different
executors and copies. They do not isolate zero-copy RMW overhead for Path or
Trajectory. A useful next control would hold message representation constant
and compare native and RMW object loans on a non-Buffer payload.
[Per-frame samples and per-process-group resource records](../../src/universe_crop_benchmark/results-rmw-agnocast/).

## Explicit bridge boundary on the laptop

After build-loadable.sh, use the same guarded run-loadable.sh invocation with
the bridge argument. The reader and writer share a ROS domain and use the same
resolved topic. One uses rmw_agnocast, the other ordinary rmw_cyclonedds_cpp
without the hook. Test both directions. Each time verify graph discovery before
publishing and check the nested Path in the receiver. With automatic bridging
disabled, both directions had zero callbacks during the one-second window.
Explicitly launching the tracked manual_bridge runner produced exactly one
callback in each direction during the two-second observation window.

The runner calls the Agnocast library's BridgeLoader with the requested
direction and adds its callback group to the library CIE. This exercises the
upstream bridge for the native Path object layout. It does not establish
compatibility with the private PointCloud2 CDR layout. Automatic startup
policy, bridge restart and concurrent bidirectional forwarding remain open.
Second-container and cross-host experiments are deferred by the operator.

## Which part of the latency comes from Cyclone?

Retain the same rmw_agnocast library, heap hook, CPU PointCloud2 endpoints and
loopback configuration. The rmw-delegated control clears the selected topic
list. This routes both payloads through Cyclone DDS. Three alternating-order
pairs produced pooled median/p95 of 1.115/1.625 ms with Agnocast CDR and
1.307/1.727 ms with Cyclone delegation. All six runs verified 60 frames.
These measurements are a fresh control series; the native 1.20× decision
above uses the separate, interleaved six-transport series.

In a separate strace run per case, count successful Agnocast publish ioctl
calls (group 0xa6, command 7) and sum successful IPv4 sendmsg/sendto return
sizes across all benchmark descendants. No sendmmsg calls were observed.
Both traced cases delivered 60 verified frames. Agnocast CDR made 120
publish calls, exactly two per frame, and sent 198,240 IPv4 bytes. Delegation
made zero native publish calls and sent 47,610,580 IPv4 bytes. These are
process socket byte counts including control traffic. Traced latencies are
excluded from the performance comparison.

With AGNOCAST_RMW_TRACE_CODEC enabled in a separate candidate run, each
frame made two Cyclone serialization calls and two deserialization calls.
Across 50 post-warmup frames, their combined median elapsed time per frame
was 0.493 ms, during a median 1.049 ms end-to-end measurement. The median
per-frame fraction was 48.7%. This measures calls including dispatch and
allocation inside the codec. The remaining time includes shared-memory
copying, executor scheduling and the crop itself. It has no attribution
to any one component. Reproduce with fresh directories:

```bash
cp /usr/bin/strace build/rmw-agnocast/strace
BUFFER_RUNTIME_ONLY=1 TRACE_BIN=/ws/build/rmw-agnocast/strace experiments/buffer/run-lyrical.sh \
  bash experiments/rmw-agnocast/crop-control.sh build/rmw-agnocast/fresh-control
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh \
  bash experiments/rmw-agnocast/crop-control.sh build/rmw-agnocast/fresh-codec --codec
```

Publisher identity follows a different path. In the Path boundary probe,
the rmw_agnocast writer's get_gid() reported rmw_cyclonedds_cpp from its
graph handle. A bridged Cyclone reader received a Cyclone GID and nonzero
timestamps. A native reader received a null GID provider and zero timestamps
because the prototype clears sample metadata at native take.
[Matched samples and diagnostic counts](crop-control-results/).

## Matched Path object loans

The laptop control uses one writer and one reader in separate processes on
the same Agnocast device. The same executable builds each Path from 64
PoseStamped elements with 80-character frame IDs. Both paths borrow, fill
and move the publisher loan, use volatile reliable KeepLast(4), and retain
each received loan until the next callback. The reader verifies the sequence,
every nested string and every assigned position, then rechecks the previous
sample. Writer and reader log the object, pose-array and final string
addresses in memory. The harness compares all three for every frame after
both processes exit. No per-frame file I/O or acknowledgement is timed.

The native reader uses the Agnocast-only single-threaded executor. The RMW
reader uses the standard rclcpp single-threaded executor and the backend's
native receiver thread. The measurement uses the Linux steady clock from
just before publication to the start of the common callback body. Loan
construction, validation and callback completion are outside the interval.
Both processes preload the same heaphook; bridging and the discovery agent
are off. There is no Buffer or CDR conversion on this workload.

From the experimental worktree with its restored Lyrical root and overlays:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout 180s bash \
  experiments/rmw-agnocast/bench-path.sh build/rmw-agnocast/fresh-path
```

Four rounds of 60 publications at 10 Hz alternate which transport runs first.
Each path verified 240/240 frames. Pool the last 50 of each round for the
median and nearest-rank p95 (200 samples per path). CPU is median child user
plus system seconds per two-process run, including startup and teardown. RSS
is the median largest individual child peak. Latencies are in microseconds.

| Path | Median µs | p95 µs | CPU s | Peak RSS MiB |
|---|---:|---:|---:|---:|
| Native Agnocast | 41.86 | 56.25 | 0.136 | 26.3 |
| rmw_agnocast | 79.04 | 116.61 | 0.197 | 29.2 |

The 1.20× limits were 50.23 µs and 67.50 µs. RMW/native was 1.89× median
and 2.07× p95. Both gates were missed in each round. An earlier series
without child resource accounting gave 44.18/66.70 µs native versus
80.32/107.52 µs RMW and verified all 480 frames. The two series are separate.

The candidate adds about 37 µs at the median on this single hop even with
verified shared addresses. Measure time from native callback entry to guard
signal, wait return, take and rclcpp callback entry before changing the wait
integration. These end-to-end numbers include publication and dispatch and
cannot assign the excess to a particular stage. The Universe crop has a
different representation and two hops; its measured codec cost remains a
separate optimization target.
[Frame and resource records](path-results/).

## Receiver handoff timing

Six rounds ran all six orders of native Agnocast, ordinary rmw_agnocast and
instrumented rmw_agnocast with the matched Path fixture above. Each mode
verified 360/360 retained, address-matched samples. Dropping the first ten
frames from each round leaves 300 per mode. Resource columns use the same
whole-run accounting as the matched Path study.

| Mode | Median µs | p95 µs | CPU s | Peak RSS MiB |
|---|---:|---:|---:|---:|
| Native Agnocast | 45.58 | 79.92 | 0.125 | 25.8 |
| rmw_agnocast, tracing off | 77.87 | 139.32 | 0.168 | 28.9 |
| rmw_agnocast, tracing on | 91.70 | 146.61 | 0.166 | 28.9 |

The untraced RMW measured 1.71× native median and 1.74×
native p95. Traced minus untraced pooled latency was +13.84 µs median
and +7.29 µs p95. These differences include between-run variation.

| Traced interval | Median µs | p95 µs |
|---|---:|---:|
| Publish to native callback entry | 46.05 | 82.52 |
| Native callback entry to guard trigger start | 0.25 | 0.99 |
| Guard trigger start to delegated wait return | 26.59 | 44.11 |
| Delegated wait return to subscription marked ready | 0.44 | 1.00 |
| Ready to loan take entry | 10.68 | 24.11 |
| Loan take body | 0.63 | 1.51 |
| Loan taken to application callback entry | 3.07 | 6.65 |

The guard trigger call itself took 10.00 µs median, overlapping the
signal-to-wait-return interval across the two threads. For 300/300
post-warmup frames the delegated wait was already blocking when the guard
trigger started and returned afterwards. Native callback entry to application
callback entry took 43.90 µs median, calculated per frame. Each row's
percentiles are calculated independently.

The opt-in reader stores timestamps in reserved memory under its existing
queue mutex and writes its trace only at teardown. Its loan entry ID and
object address identify the sample, including when the allocator reuses an
address. The harness rejects missing or duplicate entries, mismatched loan
addresses and chronologically inconsistent traces. The ready timestamp is
inside the final native-readiness check in rmw_wait. Take entry is after
the RMW registry lookup and before acquiring the queue mutex. Ready-to-take
includes the wait epilogue, rcl/rclcpp dispatch and that lookup. The
signal-to-wait-return interval includes guard work and thread scheduling;
the measurements cannot separate their individual costs.

With LYRICAL_ROOT and overlays set as above, build once and run serially:

```bash
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout 240s bash \
  experiments/rmw-agnocast/bench-path.sh build/rmw-agnocast/fresh-handoff --handoff
python3 experiments/rmw-agnocast/summarize-handoff.py build/rmw-agnocast/fresh-handoff
```

The first optimization to test is a wait path in which the ROS executor
consumes native readiness directly. Preserve the existing mixed Cyclone/native
wait test, queue-depth and retained-loan tests when evaluating it. These Path
diagnostics do not quantify PointCloud2 codec cost.
[Per-frame timings and resource samples](handoff-results/).

## Direct native readiness in a mixed ROS executor

Set `AGNOCAST_RMW_DIRECT_WAIT=1` in the subscribing process before ROS
initialization to try this path. The selected topic list, loan API, heaphook,
kernel module and bridge policy are the same as in the existing RMW probe.
The default still uses one native receiver thread per selected subscription.

The ROS executor polls each selected subscription's Agnocast notification fd
and takes native batches on its own thread. A blocking mixed wait also needs
a wakeup from delegated DDS entities. The prototype uses one persistent helper
per participating wait set. That helper blocks in Cyclone's public RMW wait
and signals an eventfd on completion. On native wake, the executor interrupts
the helper using a private Cyclone guard and acquires native loans while the
helper finishes. Both threads synchronize before returning the caller's DDS
entity arrays. Zero-duration waits and already queued loans use a synchronous
DDS readiness check. There is no timer-based polling. The helper still costs
a scheduling rendezvous on a blocking native wake. The adapter depends on
protected Agnocast executor/subscription internals at this branch's revision.

On the laptop with the existing Lyrical overlays and /dev/agnocast, reproduce
the functional checks and matched Path comparison serially:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
for case in path pressure contract bridge; do
  AGNOCAST_RMW_DIRECT_WAIT=1 BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh \
    timeout 60s bash experiments/rmw-agnocast/run-loadable.sh "$case" || break
done
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh \
  timeout 60s bash experiments/rmw-agnocast/run-loadable.sh pressure
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout 240s bash \
  experiments/rmw-agnocast/bench-path.sh build/rmw-agnocast/fresh-direct --direct
python3 experiments/rmw-agnocast/summarize-handoff.py build/rmw-agnocast/fresh-direct
```

Each of the six rounds runs native, default RMW and direct RMW in a different
order. Only the direct reader has the new flag. All three cases use the same
dynamic Path and check the object, pose vector and string addresses at both
endpoints. The aggregate excludes ten warmup frames per run. CPU covers both
child processes including startup and teardown. RSS is the larger child peak.
The idle DDS check sends a control on an ordinary topic after a 100 ms delay
with native publishing paused. The reader allows a 3 s spin and requires the
verified control callback within 2 s, including that delay.

Both direct and default readers passed the mixed DDS wake, depth-two pressure,
retained-loan teardown and restarted-reader checks. Direct mode passed Path
loan/copy and exception handling, recovered the killed reader's held address,
and passed both manual bridge directions with their bridge-off controls.
The contract probe still observed zero native timestamps, an empty GID and a
callback on the incompatible reliability pair. Those ROS semantics remain open.

The existing Universe crop verified 60/60 PointCloud2 frames on the two-edge
CDR-over-Agnocast path with the flag set. This single run checks integration.
Use the [Universe matrix](#type-support-and-the-universe-decision)
for the prior Cyclone, Zenoh, Buffer and native comparisons. Reproduce the smoke
run from the same worktree with a fresh CSV path:

```bash
BUFFER_RUNTIME_ONLY=1 AGNOCAST_RMW_DIRECT_WAIT=1 experiments/buffer/run-lyrical.sh bash -ec '
  source build/agnocast-lyrical/install/local_setup.bash
  source build/rmw-agnocast/install/local_setup.bash
  source build/rmw-agnocast/load-install/local_setup.bash
  python3 src/universe_crop_benchmark/run_case.py rmw-agnocast build/rmw-agnocast/fresh-direct-crop.csv'
```

| Final Path series | Median µs | p95 µs | CPU s | Peak RSS MiB |
|---|---:|---:|---:|---:|
| Native Agnocast | 40.74 | 62.01 | 0.154 | 26.2 |
| Existing rmw_agnocast | 80.42 | 113.86 | 0.204 | 29.6 |
| Direct-wait rmw_agnocast | 82.38 | 119.38 | 0.203 | 29.5 |

Direct wait measured 2.02× native median and 1.93× native p95. The 1.20×
ceilings were 48.89 and 74.41 µs. The preliminary series gave the same
decision (native 39.80/61.58, existing RMW 80.00/127.45, direct RMW
83.57/126.68 µs). The final series followed guard-allocation and error-path
cleanup and used the same workload. Each series verified 1,080 deliveries,
with 300 post-warmup observations per mode. Both are laptop observations.

Keep direct wait opt-in and the Bazel milestone closed. The native notification
now reaches the ROS executor directly, but the executor still waits for the DDS
helper to return the caller's entity arrays. Time native-fd wake, loan
acquisition, helper completion and callback entry per sample before deciding
whether to change the Cyclone wait integration. This experiment measures
end-to-end latency and does not establish how much the rendezvous costs.
[Raw Path and crop samples](direct-wait-results/).

## Timing the direct-wait DDS helper

The direct-wait reader optionally records a steady-clock timestamp at each
boundary below. Only the reader in the traced case receives
`AGNOCAST_RMW_DIRECT_TRACE`, a private output file path. A reserved trace
buffer is written at teardown. The helper writes its return timestamp before
signalling completion. The executor reads it after synchronizing with the
helper. Existing loan entry IDs and object addresses link each blocking wake
to the application callback. The harness rejects unmatched entries, missing
blocking wakes, address mismatches and inconsistent timestamps.

* **Wake** is immediately after the executor's poll returns with the native
  fd readable. Publish-to-wake includes publisher work and scheduling.
* **Interrupted** follows the check and conditional trigger of the private
  DDS guard. **Acquired** is entry to the native callback holding the loan.
  **Drained** is the executor's return from native batch handling.
* **Helper return** is the helper's return from Cyclone's RMW wait.
  **Synchronized** follows the executor's completion-eventfd read and mutex
  synchronization. Helper return can precede or follow native drain.
* **Ready** is the final native readiness check. **Callback** is application
  callback entry in the Path reader.

Use the same exported Lyrical root and laptop memory guard as above:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout 240s bash \
  experiments/rmw-agnocast/bench-path.sh build/rmw-agnocast/fresh-rendezvous --rendezvous
python3 experiments/rmw-agnocast/summarize-handoff.py build/rmw-agnocast/fresh-rendezvous
```

Six rounds rotate through every ordering of native Agnocast, direct RMW and
traced direct RMW. Each run checks 60 frames. Latency and stage distributions
pool frames 10–59 from each round. CPU and RSS include startup and teardown.

| Path on the Lyrical laptop | Median µs | p95 µs | Child CPU s | Peak child RSS MiB |
|---|---:|---:|---:|---:|
| Native Agnocast | 44.56 | 69.42 | 0.123 | 26.3 |
| Direct RMW | 89.82 | 130.97 | 0.199 | 29.5 |
| Direct RMW with tracing | 97.23 | 143.89 | 0.199 | 29.7 |

All 1,080 deliveries passed the cross-process loan checks. All 360 traced
frames had a complete blocking native wake. The untraced direct path measured
2.02× and 1.89× native at median and p95. The 1.20× thresholds in this series
were 53.47 and 83.30 µs. The traced row measured 7.41 µs higher median and
12.92 µs higher p95 than the untraced control. Use the untraced row for the gate.

| Within the traced Path run | Median µs | p95 µs |
|---|---:|---:|
| Publish to poll return | 30.56 | 48.65 |
| Poll return through DDS interrupt check | 11.61 | 15.89 |
| After interrupt to native callback holding the loan | 24.19 | 38.42 |
| Native callback entry to end of native drain | 1.59 | 2.48 |
| End of drain through helper synchronization | 9.93 | 24.92 |
| Synchronization to RMW ready | 0.27 | 0.53 |
| RMW ready to application callback | 19.51 | 25.50 |

Cyclone had already returned on the helper thread by the end of native drain
in 168/300 post-warmup frames. The executor still had to consume the eventfd
and synchronize. For each frame, adding the interrupt interval and the
post-drain synchronization interval gave 20.26 µs median and 35.92 µs p95.
Subtracting those two intervals from that same frame left 76.23/114.57 µs
median/p95, above both targets. This arithmetic describes the recorded path.
Changing the wake architecture can also change scheduling and native acquisition.

Keep direct wait opt-in. Before patching Cyclone, trace native Agnocast's own
poll-to-loan and loan-to-user-callback times with the same payload. That
comparison can identify how much of the 24.19 µs acquisition and 19.51 µs ROS
dispatch intervals is incremental. The [Universe matrix](#type-support-and-the-universe-decision)
retains the separate Cyclone DDS, Zenoh and Buffer pipeline measurements.
[Per-frame stages, latency and process resources](rendezvous-results/).

## Comparing the native executor and direct RMW at loan creation

This experiment uses one instrumented Agnocast userspace library for both
traced readers. The first probe fires after a successful subscription
`epoll_wait`, once Agnocast has decoded the event type. The second fires
immediately after `message_creator` constructs the subscriber loan, before
the library dispatches its callable. Both run on the receiving executor
thread. The application timestamps entry before checking the message. For
each Path frame at 10 Hz, the harness checks the native subscription ID,
object address and frame order. It also pairs the two RMW traces by entry ID.
Matching nested-vector and string addresses and checking the previously
retained sample remain mandatory.

The RMW reader additionally records its outer blocking poll, DDS guard and
wait-set synchronization. Its inner Agnocast epoll is nonblocking. Its stage
CSV uses `acquired` for entry to the RMW's native callback; its core CSV uses
`acquired` for the earlier loan-creation probe shared with native Agnocast.
The harness checks the ordering of both sets of timestamps for each sample.

Use the existing Release colcon build of agnocastlib, its installed overlay,
and the exported Lyrical root. The build script copies the instrumented
library to a private directory and restores the CMake option to OFF. Only the
two traced readers search that directory. All writers and both untraced
controls load the installed library.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash -ec '
  bash experiments/rmw-agnocast/build-path-trace.sh
  bash experiments/rmw-agnocast/build-loadable.sh'
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout 240s bash \
  experiments/rmw-agnocast/bench-path.sh build/rmw-agnocast/fresh-native-stages --native-stages
python3 experiments/rmw-agnocast/summarize-handoff.py build/rmw-agnocast/fresh-native-stages
```

Four rounds use a balanced order for native and direct RMW, each with and
without tracing. Every treatment occupies each position once and follows
every other treatment once within the four rounds. Each process pair sends
60 frames. The summary drops the first ten per run and reports the median and
nearest-rank p95 over 200 samples per treatment.

| Path on the Lyrical laptop | Median µs | p95 µs | Child CPU s | Largest child peak MiB |
|---|---:|---:|---:|---:|
| Native Agnocast | 51.90 | 94.28 | 0.126 | 26.2 |
| Native with library probes | 56.62 | 120.64 | 0.193 | 26.9 |
| Direct RMW | 111.21 | 186.62 | 0.234 | 29.4 |
| Direct RMW with library and helper probes | 98.22 | 180.98 | 0.253 | 29.7 |

| Matched stage, median / p95 in µs | Native | Direct RMW |
|---|---:|---:|
| Publish to Agnocast epoll return | 34.03 / 70.53 | 45.49 / 80.56 |
| Agnocast epoll return to loan creation | 25.08 / 50.48 | 21.21 / 34.20 |
| Loan creation to application callback | 0.68 / 1.34 | 33.03 / 70.28 |

The direct executor first woke from its outer poll at 29.44 µs median from
publish. Handling the DDS interrupt took another 12.26 µs median and reaching
the inner Agnocast epoll timestamp took 4.74 µs after that. Once both readers
were in the native acquisition code, direct RMW had comparable acquisition
time. The post-loan difference held in every round: per-round medians were
0.19–0.74 µs native and 26.55–39.29 µs direct. On direct RMW, loan creation
to RMW readiness measured 13.02/40.59 µs median/p95 and readiness to the
application callback measured 19.55/36.34 µs. Percentiles are computed per
interval. The two readers ran in distinct processes and trials.

All 960 frames passed the address, ordering and retained-payload checks, and
all 480 traced frames had matching loan records. The untraced RMW median/p95
were 2.14×/1.98× native, above the 1.20× ceilings of 62.28/113.13 µs. Timings
varied between trials, including a faster traced direct median. Use the
untraced rows for the performance gate. The [Universe matrix](#type-support-and-the-universe-decision)
covers the separate PointCloud2 pipeline with Cyclone DDS, Zenoh and Buffer.

Keep direct wait opt-in. The next diagnostic should hold the RMW wait and
native loan path fixed and compare normal ROS executor dispatch with a
single-ready-subscription control that reuses the same loan handler. Measure
RMW readiness to application entry and retain the ownership checks. This
isolates the portion of the post-loan interval that an executor change could
recover before attempting a production change to mixed DDS wake handling.
[Raw frames, stage timestamps and resource measurements](native-stage-results/).

## Dispatching a single ready Path subscription

The probe's executor control calls the inherited rclcpp wait_for_work, checks
the wait result for a ready subscription, verifies its identity, and calls
the inherited execute_subscription. This preserves the RMW wait set, DDS
helper, native queue, typed loan take, callback and retained-loan deleter.
The ordinary executor does its usual entity selection and callback-group
handling. The control is specific to a fixed subscription on one thread. It
omits general scheduling of timers, services and callback groups.

Run in the existing exported Lyrical environment with the previous overlays:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh timeout 240s bash \
  experiments/rmw-agnocast/bench-path.sh build/rmw-agnocast/fresh-dispatch --dispatch
python3 experiments/rmw-agnocast/summarize-handoff.py build/rmw-agnocast/fresh-dispatch
```

Five rounds rotate native Agnocast, direct RMW with normal selection, the
single-subscription control, and traced versions of both RMW readers. Each
occupies each run position once. Each pair sends 60 Path frames at 10 Hz.
The benchmark checks sequence, nested content, three cross-process addresses
and the previous retained loan. The latency summary uses the last 50 frames
per run. RMW readiness is timestamped at the final native-readiness check
inside rmw_wait, before rclcpp resumes.

| Path reader | Median µs | p95 µs | Median child CPU s | Largest child peak MiB |
|---|---:|---:|---:|---:|
| Native Agnocast | 53.96 | 86.03 | 0.213 | 25.9 |
| Normal direct RMW | 97.98 | 151.35 | 0.182 | 29.3 |
| Single ready subscription | 93.11 | 154.88 | 0.182 | 29.4 |
| Traced normal direct RMW | 102.46 | 160.32 | 0.191 | 29.4 |
| Traced single ready subscription | 99.47 | 150.67 | 0.276 | 29.3 |

All 1,500 frames passed, including 600 matched blocking-wake traces. Traced
RMW-ready to callback median/p95 measured 20.04/35.37 µs for normal dispatch
and 18.18/31.22 µs for single-subscription dispatch. The untraced control
improved median by 4.87 µs while p95 increased by 3.53 µs. Relative to native,
the control measured 1.73× median and 1.80× p95. These are single-host pooled
observations. General entity selection accounts for little of the observed
dispatch delay in this fixture. Keep the standard executor and the migration
gate. Next isolate the unavoidable work on the mixed wait path by comparing
against a diagnostic native-only wake with an idle DDS helper removed, keeping
standard ROS callback dispatch and measuring loss of mixed wake capability.
[Per-frame timings and resources](dispatch-results/).

## Removing the DDS helper from the direct wait

The helper-free Path reader sets both `AGNOCAST_RMW_DIRECT_WAIT=1` and
`AGNOCAST_RMW_PROBE_NATIVE_ONLY=1`. For a wait set containing a selected native
subscription, the standard ROS executor now blocks on only the native
notification fds, honoring the supplied timeout. The control skips the DDS
wait, helper thread, guard trigger, completion eventfd and synchronization.
It still consumes native notifications through the same Agnocast executor and
uses the same rclcpp callback dispatch and loan ownership. A mixed wait
reports only native subscriptions as ready. An idle DDS sample or shutdown
guard cannot wake an indefinite native-only wait. Use this flag solely for
the bounded experiment.

Run from the colcon worktree with the previously built Lyrical overlays:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
export BUFFER_RUNTIME_ONLY=1
experiments/buffer/run-lyrical.sh timeout 240s bash \
  experiments/rmw-agnocast/bench-path.sh build/rmw-agnocast/fresh-helper-free --helper-free
python3 experiments/rmw-agnocast/summarize-handoff.py build/rmw-agnocast/fresh-helper-free
AGNOCAST_RMW_DIRECT_WAIT=1 experiments/buffer/run-lyrical.sh timeout 45s \
  bash experiments/rmw-agnocast/run-loadable.sh pressure
AGNOCAST_RMW_DIRECT_WAIT=1 AGNOCAST_RMW_PROBE_NATIVE_ONLY=1 \
  experiments/buffer/run-lyrical.sh timeout 45s bash experiments/rmw-agnocast/run-loadable.sh pressure
```

The last command is a negative control. Look for the reader's specific
`DDS wake timed out` error. Six Path rounds run the three untraced treatments
in all six orders. Each verifies 60 frames. The latency summary pools the
last 50 frames per round, and CPU/RSS summarize whole child processes.

| Untraced Path reader | Median µs | p95 µs | Median child CPU s | Largest child peak MiB |
|---|---:|---:|---:|---:|
| Native Agnocast | 41.03 | 58.69 | 0.149 | 26.0 |
| Direct RMW with mixed DDS wait | 83.91 | 121.37 | 0.201 | 29.3 |
| Direct RMW with native-only wait | 66.39 | 105.53 | 0.197 | 29.4 |

All 1,080 frames passed the content, retained loan and address checks. Each
round's median favored the helper-free control over mixed wait. Pooled median
and p95 fell by 17.52 and 15.84 µs. The helper-free path still measured
1.62×/1.80× native against the 1.20× gate. The mixed pressure check passed;
the native-only reader reported DDS wake timed out and its writer timed out
waiting for acknowledgement. The control trades away mixed wake semantics.

Keep the existing executor and mixed wait. Defer a production Cyclone wait
redesign. Return to the Universe PointCloud2 pipeline and test avoiding the
CDR payload round trip against the prior native, Cyclone DDS and Zenoh/Buffer
rows. That is a separate hypothesis about pipeline cost.
[Raw samples and process resource measurements](helper-free-results/).

## Fixed-topology blocking mixed wait

The experimental reader holds its entities alive across all waits. Its probe
scope ends before subscription destruction, including on exceptions and
before the retained-loan teardown test removes its node. The helper uses a
private Cyclone wait set and its own arrays of handles. The executor validates
the handle lists against the fixed snapshot. Reconfiguration during the
scope is unsupported. This scope is a fixture-only contract, not a new ROS API.

The executor blocks in ppoll on the native notification fds and a DDS
completion eventfd. A native fd can be consumed immediately, while the
helper stays blocked in Cyclone. A DDS completion publishes readiness once.
The worker waits for the executor to consume that result, then rearms on
the next RMW wait. When both sources are ready the executor merges the masks.
Stopping the scope signals the worker's private guard, joins the thread and
destroys the private DDS wait set. This protocol assumes one executor thread
and stable entities, including event handlers and guard conditions.

From the colcon worktree on the laptop with its exported Lyrical overlays:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
export BUFFER_RUNTIME_ONLY=1
AGNOCAST_RMW_DIRECT_WAIT=1 AGNOCAST_FIXED_PROBE=1 experiments/buffer/run-lyrical.sh \
  timeout 45s bash experiments/rmw-agnocast/run-loadable.sh pressure
experiments/buffer/run-lyrical.sh timeout 240s bash \
  experiments/rmw-agnocast/bench-path.sh build/rmw-agnocast/fresh-fixed --fixed
python3 experiments/rmw-agnocast/summarize-handoff.py build/rmw-agnocast/fresh-fixed
```

The pressure fixture pauses native publishing to verify an independent DDS
wake. It then parks the executor until both the final native sample and a
second DDS sample have been sent. The reader must verify both, and the
`both` counter records co-readiness observed by poll. Finally a separate
thread cancels an idle executor. Queue depth, retained loans, reader restart
and cross-thread final release use the existing checks. The matrix uses four
balanced orders, 60 frames per run and 50 timed frames after warmup.
All RMW readers include the same idle ordinary DDS subscription. Native
Agnocast retains its standalone reference executor.

The idle context-shutdown variant sets `AGNOCAST_PROBE_CONTEXT_SHUTDOWN=1`
in the pressure command above. Also setting `AGNOCAST_PROBE_REBIND=1`
stops and joins the helper after the first DDS sample, removes the node,
replaces the ordinary DDS subscription, reattaches the node and starts a
fresh scope on the same executor. Three independent runs passed both
payload checks after replacement and shut down the idle ROS context.
This requires a coordinated pause. Arbitrary entity destruction concurrent
with a live wait, automatic topology changes, multiple executors and full
RMW wait conformance remain outside this experiment. In particular, a
zero-time wait can precede the helper's observation of new DDS readiness.

In the fixed Path reader the helper's private interrupt guard was used
only to stop the scope. Native publications generated no helper interrupts.
The pressure reader observed native and DDS fds simultaneously ready and
delivered both samples, then woke for executor cancel or context shutdown.
[Per-run frames, resource measurements and wake counters](fixed-wait-results/).

| Reader, median / p95 µs | Series 1 | Series 2 | Series 3 | All post-warmup samples |
|---|---:|---:|---:|---:|
| Native Agnocast | 44.48 / 62.12 | 46.98 / 236.24 | 41.32 / 61.19 | 43.86 / 78.32 |
| Original mixed RMW | 89.55 / 131.74 | 90.03 / 130.73 | 81.54 / 124.08 | 86.64 / 130.73 |
| Helper-free control | 68.58 / 98.57 | 68.03 / 100.84 | 69.33 / 96.96 | 68.74 / 98.57 |
| Fixed mixed RMW | 70.50 / 90.85 | 68.67 / 95.85 | 69.34 / 128.62 | 69.44 / 101.55 |

All 2,880 frames passed. Each of the 12 fixed readers reported 60 native
wakes, zero native-triggered interruptions and one stop interrupt. The pooled
column combines 600 post-warmup samples per reader. Native round four of
series two reached 548.36 µs p95, so the unusually high native tail in that
series is retained explicitly. Fixed mixed wait matched the helper-free
median within 2 µs in all three series; its p95 exceeded the original mixed
p95 in series three. Pooled fixed latency remained 1.58×/1.30× native. Keep
the fast path experimental. Across series the median CPU seconds per run
were 0.162–0.201 for fixed versus 0.167–0.206 for original mixed, and
largest-child peak RSS ranged from 29.5–29.6 versus 29.2–29.4 MiB.
The first series preceded teardown cleanup; series two and three use that
cleanup, and the final fixture also validates coordinated rebinding.

## Fixed mixed wait in the Universe PointCloud2 pipeline

The fixture runs the existing Universe crop-box component, source and verifier
in three processes. For the matched mixed/fixed cases, a colcon-built component
runner loads the same filter and sink libraries under the standard rclcpp
single-threaded executor. The source uses the existing wrapper launcher. Native
Agnocast retains its production crop CIE. Source and sink check a CPU crop of
the same PointXYZIRC fixture as the previous Universe matrix.

Starting fixed wait before the first callback failed. The crop's DDS event
list grew from 10 handles to 18 after its first native wake. Both matched
runners therefore start in ordinary mixed mode, wait for the sink's first
verified CSV row, drain currently ready work and then spin. The fixed runner
holds its fixed-topology scope until spin returns and closes it before node
removal. The harness checks each receiving process's wake counters at
shutdown. The first ten frames per run are warmup. This sequencing requires
the graph to stay stable during the fixed phase.

With the exported Lyrical root, Universe buffer-matrix libraries and RMW
overlays installed in this worktree, reproduce sequentially:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
export BUFFER_RUNTIME_ONLY=1
experiments/buffer/run-lyrical.sh timeout 420s bash \
  experiments/buffer/bench-universe-lyrical.sh build/rmw-agnocast/fresh-universe --fixed
experiments/buffer/run-lyrical.sh timeout 45s bash experiments/rmw-agnocast/run-loadable.sh host-cloud
experiments/buffer/run-lyrical.sh timeout 180s bash \
  experiments/rmw-agnocast/bench-host-cloud.sh build/rmw-agnocast/fresh-cloud
```

The optional host-cloud representation copies the CPU point vector and all
PointCloud2 metadata into typed Agnocast storage. The receiver reconstructs
a local standard PointCloud2 and CPU Buffer. Reader and writer must both opt
in; a dedicated internal topic keeps its representation distinct from CDR
storage. Typed reads skip the CDR round trip. A serialized reader requests
CDR from the reconstructed local message. This same-host CPU control still
copies payload bytes on publication and reception at each edge.

| Universe pipeline, three runs | Median ms | p95 ms | Child CPU s | Largest child peak MiB |
|---|---:|---:|---:|---:|
| Native Agnocast | 0.365 | 0.545 | 0.564 | 139.0 |
| Default rmw_agnocast, CDR | 0.986 | 1.189 | 0.701 | 137.9 |
| Direct mixed wait, CDR | 0.998 | 1.328 | 0.752 | 135.6 |
| Fixed mixed wait, CDR | 0.970 | 1.120 | 0.657 | 135.3 |
| Cyclone DDS | 1.303 | 1.540 | 0.793 | 134.0 |
| Zenoh | 1.063 | 1.304 | 1.547 | 290.4 |
| Zenoh + shmem Buffer | 0.744 | 0.955 | 1.542 | 288.1 |
| Zenoh + CUDA Buffer | 1.110 | 1.918 | 1.506 | 288.9 |
| Cyclone DDS + shmem Buffer | — | — | — | — |

The last row carries the [previous incompatibility evidence](../../src/crop_pipeline_benchmark/two-edge-results/cyclone-matrix.tsv);
it has no Universe latency measurement. All 1,440 timed-run frames passed.
The fixed crop and sink each recorded 59 native wakes in every run, zero
native-triggered interruptions and one teardown interruption. CPU is median
aggregate child user plus system seconds per run, including startup and
teardown. The Zenoh rows include the router. Latency uses 150 post-warmup
samples per row.

The host-cloud contract sends an empty cloud and a two-point fixture
with padding. It exercises a long frame ID and field name, byte order,
density, field type/count/offset, stamp and payload bytes. Both a typed
subscriber and a generic subscriber deserializing the recorded CDR compare
the complete ROS message with the expected value. The Universe verifier
separately checks every retained point and sequence at the sink.

In the first focused three-round comparison, native median/p95 were
0.399/1.325 ms, fixed CDR 1.000/1.138 ms, and fixed host-cloud 0.571/1.050 ms.
Native round two reached 1.375 ms p95; host-cloud round three reached
1.122 ms. These rounds are included in the reported distributions.

The repeat measured 0.386/0.683 ms native, 0.991/1.115 ms fixed CDR, and
0.539/0.639 ms fixed host-cloud. Both series verified 540 frames each. The
combined latency distributions use 300 post-warmup frames per case. CPU and
RSS summarize six complete process groups per case.

| Focused comparison, pooled | Median ms | p95 ms | Child CPU s | Largest child peak MiB |
|---|---:|---:|---:|---:|
| Native Agnocast | 0.395 | 0.876 | 0.588 | 137.9 |
| Fixed mixed RMW, CDR | 0.994 | 1.126 | 0.737 | 135.9 |
| Fixed mixed RMW, CPU host-cloud | 0.554 | 0.961 | 0.724 | 137.6 |

The host-cloud representation reduced median latency by 44% versus CDR under
the same fixed wait. Native-relative median/p95 were 1.40×/1.10×. Both focused
series passed the p95 criterion and missed the median criterion; the 1.20×
gate requires both. Keep the representation opt-in. Next isolate reception
copies using a receiver-local Buffer view whose lifetime owns the Agnocast
loan. Check retained samples, mutation/copy behavior and final release before
timing it. CPU-only field reconstruction still leaves shmem and CUDA Buffer
ownership as separate integration work.
[Raw matrix and both focused series](universe-fixed-results/).

## Receiver-local Buffer view of an Agnocast point payload

The CPU host-cloud writer and its private wire representation stay the same.
In the opt-in typed take path, the receiver copies the ROS metadata and builds
a local Buffer implementation referencing the shared point vector. Its owner
holds both the Agnocast sample loan and the native reader; destruction returns
the loan before releasing the reader. The crop and sink obtain a const byte
span from this receiver-local implementation. They never invoke a Buffer
implementation constructed in another process.

This view uses the existing Lyrical Buffer extension interface. It is an
explicit read-only backend. The stock CPU vector accessors reject mutation
of this backend. Copying the message or calling to_vector materializes
independent CPU storage. The crop still allocates its output normally, and
each publisher still copies the point payload into Agnocast storage. Generic
serialized subscribers use the existing reconstruction and CDR path.
A consumer using the CPU-vector interface must materialize the view or keep
the copying host-cloud mode. The prototype requires both a receiver process
opt-in and the Lyrical subscription option `acceptable_buffer_backends="any"`.
A CPU-only subscription keeps its normal Buffer within that same process.
The view and copy controls use explicit typed rclcpp subscriptions and the
same callbacks. This is a paired same-host RMW experiment with a private
representation. Publication still expects CPU-backed input, so a relay must
materialize a view before republishing it through this writer.

Reproduce serially from the worktree using the existing overlays:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
experiments/buffer/run-lyrical.sh bash experiments/buffer/build-universe-lyrical.sh
export BUFFER_RUNTIME_ONLY=1
for test in host-view host-cloud pressure; do
  AGNOCAST_RMW_DIRECT_WAIT=1 experiments/buffer/run-lyrical.sh timeout 45s \
    bash experiments/rmw-agnocast/run-loadable.sh "$test" || exit 1
done
experiments/buffer/run-lyrical.sh timeout 180s bash \
  experiments/rmw-agnocast/bench-host-cloud.sh build/rmw-agnocast/fresh-view-forward --view
experiments/buffer/run-lyrical.sh timeout 180s bash \
  experiments/rmw-agnocast/bench-host-cloud.sh build/rmw-agnocast/fresh-view-reverse --view --reverse
```

The view contract uses two processes and three subscriptions. The writer records
the address of the vector's payload **inside its Agnocast publication**.
The typed reader checks that address and complete field values, including an
empty sample and sample-specific padded payloads. It holds frame 1 across
eight publications at queue depth four, subscription destruction and ROS
shutdown. It rejects a direct write, mutates a deep copy independently and
checks expiry of the view owner on final release from another thread. The
CPU-only subscriber checks its backend and full message. A generic subscriber
decodes a serialized take and checks the same ROS values.
Address logging is disabled in latency runs.

The two three-round series use all six orderings of native Agnocast,
fixed mixed RMW with receive copies, and fixed mixed RMW with receive views.
All cases run the same Universe two-edge crop fixture. Ten warmup frames
per run are excluded. See the [preceding matrix](#fixed-mixed-wait-in-the-universe-pointcloud2-pipeline)
for Cyclone DDS and Zenoh CPU, shmem and CUDA measurements.

| Six runs per case, pooled | Median ms | p95 ms | Median / native | p95 / native | Child CPU s | Largest child peak MiB |
|---|---:|---:|---:|---:|---:|---:|
| Native Agnocast | 0.387 | 0.602 | 1.00× | 1.00× | 0.615 | 135.2 |
| Fixed mixed RMW, receive copy | 0.510 | 0.659 | 1.32× | 1.09× | 0.652 | 133.9 |
| Fixed mixed RMW, receive view | 0.475 | 0.595 | 1.23× | 0.99× | 0.710 | 133.5 |

The view cut the matched receive-copy median by 7%. The two series
individually measured view/native median ratios of 1.24× and 1.21×, with
p95 ratios of 1.07× and 0.91×. The median missed the gate in both.
CPU time includes all children through startup and shutdown. It increased
for the view despite the lower median latency. Each row contains 300
post-warmup frames, and CPU and peak memory are medians over six runs.
All 1,080 frames passed validation. Both fixed receivers in every run recorded
59 native wakes, zero native-triggered interrupts and one shutdown interrupt.
[Raw frames, per-run resource usage and checks](host-view-results/).

During integration the non-component contract passed while the dynamically
loaded crop threw `std::bad_cast`. The input carried the expected backend
name. A terminate backtrace located the throw in the crop's InputBytes
constructor. Disassembly showed Clang comparing the vtable address of the
header-defined final class with the crop library's local copy. Keeping the
view class non-final allowed RTTI lookup across the loaded libraries. The
complete repeat used that change. The ownership checks cover direct native
wait on this host. A production backend still needs a stable exported API,
endpoint discovery and validation with independent builds.

## Transfer CPU point payloads through a publisher loan

This experiment changes the publisher on both edges of the previous
receiver-view fixture. A unique-output ROS wrapper borrows a standard
rclcpp PointCloud2 before filling it. The wrapper holds the rclcpp loan and its
publisher until publication or cancellation. The RMW copies metadata and
transfers the CPU vector into the existing
host-cloud representation in the borrowed allocation. The reader continues
to receive a local read-only Buffer backed by the Agnocast subscriber loan.
The point bytes retain their allocation address when allocated in the writer pool.
The wrapper adaptation is applied to the colcon benchmark's private build
copy. It exercises unique ownership. The RMW checks the vector against the
shared writable Linux mapping containing its native loan and falls back to
copying private storage. A missing mapping also selects the copy path. Shared or const-reference publications keep
their copy semantics. A node using rclcpp directly uses its ordinary borrow/fill/publish API.

Use the Lyrical root, Agnocast device and previously built overlays from
the receiver-view experiment. Build and run serially:

```bash
set -e
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
experiments/buffer/run-lyrical.sh bash experiments/buffer/build-universe-lyrical.sh
export BUFFER_RUNTIME_ONLY=1
for test in host-loan host-view host-cloud pressure; do
  AGNOCAST_RMW_DIRECT_WAIT=1 experiments/buffer/run-lyrical.sh timeout 45s \
    bash experiments/rmw-agnocast/run-loadable.sh "$test"
done
for order in forward reverse; do
  opts=()
  if [[ $order == reverse ]]; then opts+=(--reverse); fi
  experiments/buffer/run-lyrical.sh timeout 180s bash \
    experiments/rmw-agnocast/bench-host-cloud.sh "build/rmw-agnocast/fresh-publoan-$order" \
    --loan "${opts[@]}"
done
```

The two-process contract abandons a populated loan, then sends eight frames
and compares the address recorded by the writer before publishing to the
address read through the receiving Buffer. One frame deliberately moves in a
vector allocated before borrowing. The reader requires a different address
and identical content for that frame. The probe also checks a CPU-only reader,
serialized delivery, independence on copy and retention through teardown.
The first publication at each edge uses the conventional copy path while the
crop initializes its DDS diagnostics. The timed loan case requires both the
source and the crop to report 59 native publisher loans and zero private
payload copies for those loans. The common ten-frame warmup excludes the
startup copy from latency statistics. The copy control uses the same fixed blocking wait and
read-only views at both receivers. Native Agnocast uses its existing CIE.

Two failures shaped the test. Moving a completed private message into a loan
issued at publish time crashed the receiving crop while copying the PointField
vector. The heap hook selects shared allocation only between borrow and
publish on the borrowing thread. The corrected writer borrows before filling,
rebuilds metadata under the loan and transfers the point vector only when the
writer owns its shared storage. The deliberate private-vector contract case
exercises the fallback.

Borrowing for the entire first crop callback exposed a second failure. The sink
received output, then the crop aborted in Cyclone DDS while creating the lazy
PublishedTime publisher. A diagnostic backtrace reached
`dds_create_topic_sertype` from `ensure_publisher_exists`. Using ordinary
publication during the first callback allowed the same diagnostic endpoints
to initialize and all subsequent callbacks to use loans. This startup sequence
is specific to the fixture. Dynamic DDS entity creation while a loan is active
and publisher loans passed between threads remain outside the validated path.

| Six runs per case, pooled | Median ms | p95 ms | Median / native | p95 / native | Child CPU s | Largest child peak MiB |
|---|---:|---:|---:|---:|---:|---:|
| Native Agnocast | 0.317 | 0.523 | 1.00× | 1.00× | 0.536 | 135.0 |
| Receive view with ordinary publication | 0.424 | 0.553 | 1.34× | 1.06× | 0.573 | 133.5 |
| Publisher loan and receive view | 0.386 | 0.582 | 1.22× | 1.11× | 0.603 | 136.6 |

Forward ordering measured loan/native median and p95 of 1.19× and 1.04×.
Reverse ordering measured 1.23× and 1.15×. Each row pools 300 frames after
discarding the first ten per process group. The sink validated all 1,080 frames,
including warmup. The loan reduced median latency by 9% against the paired
receive-view control. Its p95 increased by 5%, and its aggregate child CPU
increased by 5%. The pooled median misses the 1.20× criterion by about 6 µs.
All 24 fixed receivers reported 59 native wakes and zero native-triggered
interruptions. All 12 loan-enabled writers reported 59 loans and zero private
payload copies after warmup. The previous [broader matrix](#fixed-mixed-wait-in-the-universe-pointcloud2-pipeline)
contains the Cyclone and Zenoh controls.
[Frames, resource measurements and check output](publisher-loan-results/).

## Scope the Agnocast allocation to point storage

On both CPU PointCloud2 edges, the application builds a normal message. The
benchmark brackets the first point-vector resize with Agnocast's thread-local
allocation counter and restores it on scope exit, including exceptions.
The crop shrink fits within existing capacity. The publishing wrapper then
borrows a ROS message, moves the completed output into it and publishes.
The existing RMW copies the metadata inside this short publisher loan and
transfers a vector that belongs to the publisher's shared pool. Filling the
points, executing crop diagnostics and creating DDS entities take place
outside the allocation scope. This prototype uses Agnocast's internal
allocator-selection functions at the two resize sites. It does not require
a new kernel module, heap hook or RMW implementation change.

Rebuild only the Universe fixture on top of the previous publisher-loan
overlays. Run the cold-start check and both three-round series sequentially:

```bash
set -e
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/buffer/build-universe-lyrical.sh
export BUFFER_RUNTIME_ONLY=1
experiments/buffer/run-lyrical.sh timeout 60s bash experiments/rmw-agnocast/bench-host-cloud.sh --check-cold
for order in forward reverse; do
  opts=()
  if [[ $order == reverse ]]; then opts+=(--reverse); fi
  experiments/buffer/run-lyrical.sh timeout 180s bash \
    experiments/rmw-agnocast/bench-host-cloud.sh "build/rmw-agnocast/fresh-scope-$order" \
    --scope "${opts[@]}"
done
```

The paired controls are native Agnocast and the previous longer publisher
loan with a first-frame copy. Every process group starts fresh. The scoped
case requires 60 loans and zero private payload copies at each writer. The
longer-loan control requires 59 and zero respectively. The first ten
frames are excluded only from steady-state latency. The sink validates all
60 frames. The separate diagnostic observer joins before the crop starts
and receives typed PublishedTime messages over Cyclone DDS.

| Laptop, six runs per case | Median ms | p95 ms | Median / native | p95 / native | Child CPU s | Largest child peak MiB |
|---|---:|---:|---:|---:|---:|---:|
| Native Agnocast | 0.357 | 0.616 | 1.00× | 1.00× | 0.619 | 136.7 |
| Longer loan with startup copy | 0.421 | 0.641 | 1.18× | 1.04× | 0.647 | 138.6 |
| Scoped point allocation from frame one | 0.424 | 0.671 | 1.19× | 1.09× | 0.668 | 138.2 |

The scoped case met the pooled 1.20× median and p95 targets. Forward order
gave 1.30×/1.14× and reverse gave 1.17×/1.03×. The matched longer loan also
passed on pooled latency. Restricting the allocation window enabled cold
start. The measured median stayed within 3 µs of the longer loan and the p95
increased by 30 µs. Pooled latency uses 300 post-warmup frames per row.
CPU and largest child peak RSS are medians over six complete process groups.
All 1,080 timed-run frames passed. The scoped writers completed 720
publications, all with zero private payload copies. Every fixed reader
reported 59 native wakes, zero native interrupts and one shutdown interrupt.

In the separate cold-start run both writers used 60 loans each. A Cyclone
subscriber received 60 distinct stamped messages from the previously absent
/output/debug/published_time publisher while the sink verified another 60
frames. These results apply to fresh CPU point vectors on the same host.
Reusing sufficient private vector capacity can still select the RMW copy
fallback. The fixed receiver topology and backend-aware input access remain
prerequisites. [Raw frames, child resource use and checks](scoped-payload-results/).

## Pilot bridge choice and interpretation of performance

Use 1.20× native Agnocast median and p95 as an engineering milestone. Earlier
references to a gate record how the measurements were originally evaluated.
The scoped-loan median changed from 1.30× to 1.17× across run orders. The
roughly 4 µs between its pooled median and the target is within the observed
variation. Evaluate changes using paired, rotated runs and report within-pair
differences, spread, CPU, RSS, drops, duplicates and payload copies. Small
latency differences can remain unresolved. Correct delivery, ownership and
QoS matching have independently testable expectations.

If the Autoware pilot selects one deployment, prefer rmw_agnocast with one
supervised Cyclone bridge and statically configured topics and directions.
Start routes at launch, expose readiness and allow an empty native-only
profile. A late recorder uses DDS discovery on an existing outbound route.
An explicit operator-launched relay remains useful for ad hoc routes and can
reuse the same forwarding implementation. A Cyclone PSMX Agnocast backend
remains an alternative if one writer can serve an ordinary DDS reader while
retaining local zero-copy delivery for dynamic PointCloud2. Only explicit
Path bridging has measured delivery on this laptop. Active point-cloud bridge
latency, restart behavior and PSMX costs remain unmeasured.

Bridge qualification uses one native writer, a native crop and an ordinary
DDS recorder. Compare route absent, enabled but idle, and actively recording.
Measure both readers separately, startup and late-join time, and sequence
loss or duplicates through relay restart. First qualify native matching and
publisher identity with the bridge disabled.

## Two native writers and one reliable Path reader

The writer process creates two publishers on the same topic, with identical
volatile KeepLast(8) history and different offered reliability. The reliable
reader discovers both before sending starts. The best-effort writer sends odd
sequence numbers and the reliable writer sends even numbers. Six samples fit
the reader history. The reader verifies the full nested Path and drains the
executor for one second. In a second phase a best-effort subscription in the
same reader process receives another six labelled samples. Its callback GIDs
establish the receiver-local identity of each writer. The strict test requires
stable, nonzero, distinct identities, an ordered 2/4/6 on the reliable reader
and zero incompatible callbacks. Exit 3 indicates a failed matching or
identity assertion. The runner separately enforces discovery and completeness.

| RMW / take | Discovered / matched writers | Matched readers, reliable / best-effort writer | Reliable-reader callbacks, compatible / incompatible | Writer identity |
|---|---:|---:|---:|---|
| Cyclone-only control, copy | 2 / 1 | 1 / 0 | 3 / 0 | distinct, stable |
| rmw_agnocast, copy | 2 / 1 | 1 / 0 | 3 / 3 | empty for both |
| rmw_agnocast, loan | 2 / 1 | 1 / 0 | 3 / 3 | empty for both |

All endpoints within each run use the same RMW. Bridge and discovery agent are
off. No ordinary DDS peer participates in the native runs. The Cyclone-only
run is a separate fixture control. The first identity assertion compared the
writer process's get_gid() bytes with sample metadata on the receiver. Those
bytes differed even under plain Cyclone. The final test logs both and checks
attribution against receiver-local observations.

Reproduce with the installed Lyrical Agnocast, message and patched rclcpp
overlays. Run sequentially and inspect each exit status:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
export BUFFER_RUNTIME_ONLY=1 AGNOCAST_RMW_DIRECT_WAIT=1
ROS_DISABLE_LOANED_MESSAGES=1 experiments/buffer/run-lyrical.sh timeout 35s bash experiments/rmw-agnocast/run-loadable.sh matching cyclone
ROS_DISABLE_LOANED_MESSAGES=1 experiments/buffer/run-lyrical.sh timeout 35s bash experiments/rmw-agnocast/run-loadable.sh matching native
ROS_DISABLE_LOANED_MESSAGES=0 experiments/buffer/run-lyrical.sh timeout 35s bash experiments/rmw-agnocast/run-loadable.sh matching native
```

The delegated match counts already express the expected reliability policy.
Native delivery ignores it and the native take clears message info. The
received loan's pubsub ID is constructed with the subscription ID. Using that
ID as the writer identity would conflate both publishers. Next carry a
per-writer identity and offered reliability through native delivery and enforce
matching before incompatible samples can occupy the reader's KeepLast queue.
Preserve the existing native object/bridge layout or version any new
representation. Then exercise best-effort flooding at shallow depth.
[Recorded observations and exit status](native-matching-results.txt).

### Native writer identity and reliability before queue admission

A selected ROS topic now maps to private Agnocast routes by ROS topic, message
type, storage layout and offered reliability. A writer publishes once. A
reliable reader opens the reliable route. A best-effort reader opens both and
merges them into one bounded application queue. Direct wait watches a
notification FD for each open route. This matters at depth one. In
`agnocast_kmod/agnocast_ioctl.c`, `receive_msg_core` applies the subscriber
depth while scanning deliverable entries. A callback-side filter would run
after a burst of incompatible entries could already displace a compatible one.

The writer obtains the GID and resolved reliability from its Cyclone graph
endpoint. It appends the GID bytes to the shared allocation after the native
object, host-cloud representation or length-prefixed CDR payload. The reader
sets the local implementation identifier when filling ROS message info. The
loan keeps its payload address. Source/receive timestamps, sequence numbers,
other QoS policies, retransmission and ordering across the two routes remain
outside this result. The test exercises reliable/best-effort *matching*.

Configure this format consistently across a deployment. Existing raw Agnocast
publishers and the upstream Path bridge speak the earlier format. To run that
bridge set `AGNOCAST_RMW_LEGACY_WIRE=1` on its rmw_agnocast peers. This restores
the previously measured behavior including its missing writer identity and
reliability filtering. Discovery can still report a DDS match when native
routes or layouts differ. A bridge that understands the new routes is open work.
The kernel module and preload library are the same for both formats.

Run from the worktree with the existing Universe Lyrical root, heaphook, loaded
module and scoped-payload benchmark overlays. The runtime wrapper monitors host
memory and caps each sequential invocation at 512 MiB.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
export BUFFER_RUNTIME_ONLY=1
run() { experiments/buffer/run-lyrical.sh timeout 180s bash "$@"; }
probe=experiments/rmw-agnocast/run-loadable.sh
for direct in 0 1; do
  for disabled in 0 1; do
    for traffic in normal flood; do
      AGNOCAST_RMW_DIRECT_WAIT=$direct ROS_DISABLE_LOANED_MESSAGES=$disabled \
        run "$probe" matching native "$traffic"
    done
  done
done
for test in host-loan cloud-cdr host-cloud host-view pressure contract trajectory path bridge; do
  run "$probe" "$test"
done
bench=experiments/rmw-agnocast/bench-host-cloud.sh
run "$bench" --check-cold
run "$bench" build/rmw-agnocast/wire-forward --wire
run "$bench" build/rmw-agnocast/wire-reverse --wire --reverse
```

The two-writer matrix passed all eight combinations. A further control ran
`matching cyclone flood` with direct wait selected and ROS loans disabled.
Cyclone retained the reliable sample. Repeating `matching native flood` with
the legacy wire instead delivered one incompatible sample, lost the reliable
sample and exited 3. With the new wire the same reader received the reliable
sample with the publisher's GID. Both typed and serialized cloud readers
checked the GID against the publisher across all four cloud probes.

| Universe crop on laptop | Runs | Median ms | p95 ms | Child CPU s | Largest child peak MiB |
|---|---:|---:|---:|---:|---:|
| Native Agnocast | 6 | 0.360 | 0.645 | 0.585 | 137.8 |
| rmw_agnocast legacy wire | 6 | 0.434 | 0.708 | 0.716 | 139.4 |
| rmw_agnocast versioned wire | 6 | 0.442 | 0.752 | 0.707 | 139.1 |
| Cyclone DDS, CPU | 3 | 1.302 | 1.455 | 0.789 | 134.0 |
| Zenoh, CPU | 3 | 1.076 | 1.351 | 3.486 | 288.1 |

Each run verified 60 sequential frames on both crop edges. Latency pools the
last 50 per run. CPU and peak RSS are medians of whole-run child resource
measurements, including the router for Zenoh. Native Agnocast and the two RMW
variants rotated order in both directions. Both RMW variants used fixed
blocking wait and scoped publisher allocation. Each publisher on both edges
reported 60 loans and zero private payload copies in every run. Cyclone and
Zenoh CPU controls followed as a separate batch, three runs per transport in
orders Cyclone/Zenoh, Zenoh/Cyclone, Cyclone/Zenoh. To repeat them, use the
same overlays and loopback Cyclone configuration as `bench-host-cloud.sh`
and run `python3 src/universe_crop_benchmark/run_case.py TRANSPORT FILE.csv
--resources FILE-resources.csv` once per case using a fresh filename.

The versioned wire reached 1.23× native median and 1.17× native p95. Relative
to the legacy wire, six same-round differences in median ranged from −52 to
+71 µs (median +3 µs). The p95 differences ranged from −72 to +151 µs
(median +79 µs). The higher pooled p95 merits tracking on another host. This
experiment demonstrates correct admission and attribution with retained CPU
payload loans on this laptop. The next integration test should connect a
versioned native Path route to ordinary ROS in both directions and verify the
identity exposed at each side of the bridge. Keep the explicit legacy bridge
as a control.
[Raw latency, resources and contract checks](native-matching-v2-results/).

### Crossing the versioned native boundary

The experimental `route_bridge` is a standard typed rclcpp subscriber and
publisher in a single rmw_agnocast process. The bridge selects native
transport independently for its subscriber and publisher at startup. An empty
directional list delegates that side to Cyclone. Other processes can continue
to use `AGNOCAST_RMW_TOPIC` for both directions. The bridge gets the native
object through the versioned RMW and publishes a new ROS sample. It needs the
same kernel device and heaphook as a native peer. Ordinary Cyclone peers need
neither. The native and DDS topic names may be identical or distinct.

Start one process per directed route with the same Lyrical overlay and
heaphook setup as `run-loadable.sh`. Its positional arguments are direction
(`a2r` or `r2a`), type (`path` or `cloud`), absolute input topic and absolute
output topic. Path uses reliable KeepLast(4). PointCloud2 uses SensorDataQoS.
For example, with the benchmark's host-cloud representation enabled, the
outbound point-cloud route is `route_bridge a2r cloud /output /mirror/output`.
Select one direction on a given topic. Reciprocal routes can form a forwarding
loop. Multi-route supervision, deduplication, late join/replay and cross-host
transport still need qualification.

For each Path direction the native and ordinary Cyclone endpoints use the
same name, an extra best-effort writer offers an incompatible Path, and the
receiver verifies the compatible message body and exact callback count.
With the route absent it receives zero. With the route present it receives
one. The bridge's native ingress reports the original native writer GID on
a2r. On r2a the native reader reports the bridge publisher GID. Byte equality
is asserted on that native leg. On the DDS leg Cyclone reports a receiver-local
GID. Message provenance beyond the republishing endpoint requires additional
application metadata. Native timing metadata retains its earlier limitations.
The upstream legacy bridge was separately exercised in both directions.

For the crop benchmark an active native subscriber always validates and times
the local output. The idle bridge subscribes to the live output and takes all
60 messages. Its distinct DDS output has zero matched readers so it skips
publication. Active mode adds a stock Cyclone subscriber (without the
heaphook) on that DDS output, checks all 60 full point payloads in order,
and records its own end-to-end latency. The measured DDS crossing includes
materializing the CPU PointCloud2 and DDS transport. The local crop uses the
same allocator and blocking wait settings across all three modes.

To reproduce, start with the built Universe, native Agnocast, patched rclcpp
and Lyrical overlays from the scoped-payload experiment. Build the two probe
packages sequentially, then run each case under the 512 MiB runtime limit.
Use fresh output directories. The reference batch rotates native Agnocast,
Cyclone DDS CPU and Zenoh CPU. The separate profile batch timestamps only
the bridge callback entry and return from publishing, using the ROS clock
of the benchmark on this host. It stores the first 60 records until shutdown.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
export BUFFER_RUNTIME_ONLY=1
run() { experiments/buffer/run-lyrical.sh timeout 190s bash "$@"; }
probe=experiments/rmw-agnocast/run-loadable.sh
bench=experiments/rmw-agnocast/bench-host-cloud.sh
out=build/rmw-agnocast/my-route-results
mkdir "$out"
run "$probe" route-bridge
run "$probe" bridge
run "$probe" matching native flood
run "$bench" "$out/forward" --bridge
run "$bench" "$out/reverse" --bridge --reverse
run "$bench" "$out/profile" --profile-bridge
run "$bench" "$out/references" --references
python3 experiments/rmw-agnocast/summarize-route.py "$out"
```

| Universe crop, laptop | Runs | Median ms | p95 ms | Child CPU s | Largest child peak MiB |
|---|---:|---:|---:|---:|---:|
| Native Agnocast | 3 | 0.373 | 1.037 | 0.546 | 137.1 |
| rmw_agnocast, bridge off | 6 | 0.438 | 0.695 | 0.616 | 139.8 |
| rmw_agnocast, bridge idle | 6 | 0.436 | 0.706 | 0.765 | 139.1 |
| rmw_agnocast, active, local sink | 6 | 0.437 | 0.653 | 0.941 | 139.3 |
| Same active runs, Cyclone observer | 6 | 0.990 | 1.323 | shared with local sink | shared with local sink |
| Cyclone DDS CPU pipeline | 3 | 1.344 | 1.609 | 0.730 | 130.8 |
| Zenoh CPU pipeline | 3 | 1.014 | 1.199 | 1.457 | 289.0 |

Latency pools the final 50 frames per run. CPU includes startup and teardown
of all children, including the observer or Zenoh router where present. Peak
RSS is the largest child's peak. The paired observer-minus-local latency was
0.551 ms median and 0.694 ms p95. The active-minus-off differences in run
medians ranged from −37 to +15 µs. Differences in run p95 ranged from −108
to +125 µs. The lower pooled active p95 therefore supplies little evidence
for a local speedup. All original local writers still reported 60 loans and
zero private payload copies on each edge. Bridge conversion and DDS handling
incur additional work outside those counters.

Three separate active runs gave the following median/p95 intervals in ms over
150 post-warmup frames. Source stamp to relay callback was 0.538/0.807,
callback entry through return from publish was 0.334/0.404, and callback
entry to Cyclone observer was 0.429/0.555. The last interval includes work
that overlaps the publish call. The timestamps cannot isolate serialization,
scheduler delay or memory copies individually. The first optimization to test
is avoiding idle payload materialization. Measure CPU together with the first
sample received after an external subscriber joins. For active traffic, use
the measured callback-to-observer interval as the comparison point.
[Per-frame latency, resource counters, profiles and contract evidence](route-bridge-results/).

### Item 5, first branched Universe workload

A publisher feeds a Universe crop box. Its output fans out to a verifying
reader and a second crop box. A second verifying reader consumes the final
output. All five application processes run concurrently. The second crop
uses the same bounds and processes the already-filtered points. Both readers
check every point and its order against the source frame. Both latencies
start at the source stamp. The timestamp excludes construction of the source
cloud and the sink's subsequent validation work. The tap and final sink are
independently scheduled, so subtracting their observations does not isolate
the execution time of the second filter.

The input has 32,768 points of 16 bytes (512 KiB). Both crop outputs are
256 KiB. Test a nominal 10 Hz and then 50 Hz with the same graph. Compare
native Agnocast, the versioned host-cloud RMW with scoped loans and the fixed
blocking wait, ordinary Cyclone DDS, and Zenoh. Each transport occupies each
position in the four-run order once. Each run starts fresh and verifies 60
frames at both readers. Pool the last 50 per run for each reported latency.
CPU is median aggregate child user and system time including startup and
teardown, and Zenoh includes its router. Compare CPU within a given rate.
This is a short burst at higher rate, with a static executor topology and two
instances of one algorithm. It is an intermediate workload for the existing
serious-load item.

With the same overlays, heaphook, loaded module and Lyrical root as the crop
benchmark, build and run sequentially under the existing memory monitor:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/buffer/build-universe-lyrical.sh
export BUFFER_RUNTIME_ONLY=1
bench=experiments/rmw-agnocast/bench-host-cloud.sh
for ms in 100 20; do
  out=build/rmw-agnocast/branched-period-$ms
  experiments/buffer/run-lyrical.sh timeout 250s bash "$bench" "$out" --branched "$ms"
  python3 experiments/rmw-agnocast/summarize-branched.py "$out"
done
```

| Rate | Transport | Tap median / p95 ms | Final sink median / p95 ms | Final median / p95 relative to native | Child CPU s |
|---|---|---|---|---|---:|
| 10 Hz | Native Agnocast | 0.327 / 0.542 | 0.556 / 0.895 | 1.00 / 1.00 | 0.983 |
| 10 Hz | rmw_agnocast | 0.415 / 0.650 | 0.683 / 0.989 | 1.23 / 1.10 | 1.131 |
| 10 Hz | Cyclone DDS | 1.373 / 3.629 | 1.813 / 4.594 | 3.26 / 5.13 | 1.278 |
| 10 Hz | Zenoh | 1.057 / 1.377 | 1.438 / 1.959 | 2.58 / 2.19 | 2.740 |
| 50 Hz | Native Agnocast | 0.323 / 0.557 | 0.543 / 0.924 | 1.00 / 1.00 | 0.970 |
| 50 Hz | rmw_agnocast | 0.325 / 0.532 | 0.528 / 0.948 | 0.97 / 1.03 | 1.204 |
| 50 Hz | Cyclone DDS | 1.303 / 1.668 | 1.697 / 2.222 | 3.12 / 2.41 | 1.257 |
| 50 Hz | Zenoh | 0.863 / 1.263 | 1.296 / 2.090 | 2.39 / 2.26 | 2.614 |

At 10 Hz, reaching 1.10× on median would save about 72 µs from the final RMW
result, and parity would save 127 µs. The 50 Hz matrix already straddles
parity at median and p95, though run-paired median ratios range from 0.86 to
1.14. The four 10 Hz pairs range from 1.18 to 1.32. The higher-rate RMW also
used more child CPU than native. No scheduling, power-state or wakeup cause
can be inferred from the rate comparison alone. All 1,920 source frames in
the matrix were checked at both destinations. Each of the three RMW writers
in every run used 60 loans with zero private payload copies.

The 10 Hz matrix used the original fixed 100 ms timer. The 50 Hz matrix used
the incremental fixture rebuild exposing the timer period as a ROS parameter.
The unchanged two-edge workload on the rebuilt binary also delivered all
60 delegated diagnostics. For a same-binary check interleaving the rates and
reversing the transport order at each rate, run the following command on a
fresh output directory:

```bash
experiments/buffer/run-lyrical.sh timeout 150s bash "$bench" \
  build/rmw-agnocast/branched-confirm --confirm-branched
```

In the interleaved confirmation, 10 Hz final median/p95 was 0.572/1.204 ms
native and 0.711/1.201 ms through the RMW (1.24×/1.00×). At 50 Hz it was
0.546/0.845 ms native and 0.561/1.056 ms through the RMW (1.03×/1.25×).
All 480 additional source frames passed at both readers. Measured publication
rates were 10.00 Hz and 50.01–50.03 Hz in this repeat. The smaller high-rate
median gap reproduced. The high-rate p95 ratio changed substantially.

Use 1.10× native median and p95 as a proposed **per-workload** stretch
milestone and track parity as a research question. Pair runs and retain
absolute latency, drops, deadlines and CPU. A production claim needs longer
runs on a realistic graph, particularly for tails. Next integrate an actual
multi-LiDAR synchronization/concatenation path with different input rates,
timestamps and TF, exercise the normal supported executor, and measure
sensor-to-consumer latency against native at the same delivered workload.
Keep the branched and simple crops as controls. Revisit the low-rate wakeup
path if the gap persists there. The bridge already has a separate CPU and
delivery baseline. [Raw samples and checks](branched-results/).

### Does the preceding idle interval explain the branched latency gap?

Run the same five-process graph with alternating 20 and 100 ms gaps **inside**
each publisher lifetime. One publisher timer callback drives 60 frames against
absolute steady-clock deadlines. It uses the original message preparation and
publish path for both phases. The fixture records the actual ROS stamp just
before publication. The original per-frame timer remains the default for other
benchmarks. Reverse the starting phase and transport order across four runs.
Run once without crop instrumentation and again with it. A third batch drives
constant 20 or 100 ms gaps using this same publisher loop and profile. Compare
two runs per rate/transport with the order reversed.

The opt-in first-crop callback wrapper samples wall time, thread CPU time and
the second field of Linux `/proc/thread-self/schedstat` (cumulative runnable
queue delay, ns). It checks thread identity, buffers records in memory and
writes them after the last callback. Match all 60 first-crop records to the
publisher by exact source stamp. Discard the first ten frames, validate the
remaining gaps against the expected pattern and retain the two existing sink
payload checks. Each alternating run supplies 25 frames per gap. The
unprofiled runs retain the same publisher stamp capture.

* **Stamp to entry** includes publication, transport notification, wakeup,
  take and dispatch up to the first crop callback.
* **Callback CPU and wall** cover the first filter callback including output
  publication. Wall also includes preemption and instrumentation. The
  intermediate and final readers can run concurrently with its return.
* **Runnable between callbacks** is the change in this callback thread's
  runnable-delay counter since its previous callback finished. It can include
  other work on that thread and does not include sleeping time. It cannot
  locate the exact notification or account for another thread's delay.

The unprivileged host rejected perf access to the scheduler switch tracepoint.
This measurement uses per-thread accounting without changing host tracing
permissions, affinity, frequency settings or executor priorities.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/buffer/build-universe-lyrical.sh
export BUFFER_RUNTIME_ONLY=1
bench=experiments/rmw-agnocast/bench-host-cloud.sh
run() { experiments/buffer/run-lyrical.sh timeout 160s bash "$bench" "$@"; }
root=build/rmw-agnocast/gap-reproduction
mkdir "$root"
run "$root/untraced" --gaps
run "$root/traced" --gaps --profile
run "$root/steady" --steady-gaps
for t in untraced traced steady; do
  python3 experiments/rmw-agnocast/summarize-gaps.py "$root/$t"
done
run --check-cold
```

| Median, µs | Native, 20 ms | Native, 100 ms | RMW, 20 ms | RMW, 100 ms |
|---|---:|---:|---:|---:|
| Final reader, uninstrumented crop | 554 | 604 | 592 | 678 |
| Final reader, profiled crop | 556 | 629 | 589 | 685 |
| Source stamp to first crop callback | 29 | 44 | 49 | 80 |
| First callback wall time | 260 | 310 | 291 | 329 |
| First callback thread CPU | 251 | 292 | 277 | 314 |
| Thread runnable delay between callbacks | 0 | 0 | 0 | 0 |

The uninstrumented RMW long-minus-short median increased in each run (+56,
+86, +78 and +98 µs). Its pooled p95 increased from 1055 to 1208 µs.
In the alternating profile, every one of the 200 RMW between-callback
run-queue counter deltas was zero. Native had two positive increments, both
under 25 µs. The counter advanced under a separate same-CPU contention
control. First-crop CPU time grew after a longer gap for both transports.
Stamp-to-entry grew more on the RMW. These are separate observations in a
pipeline with concurrent stages. Median differences do not sum to an exact
decomposition of the final latency.

With **constant gaps**, final RMW medians were 583 and 692 µs. Stamp-to-entry
was 51 and 78 µs, again higher at 100 ms. Callback CPU was 294 and 292 µs,
so the alternating callback CPU effect did not reproduce consistently between
steady batches. Only six of 200 between-callback run-queue increments were
positive for the RMW in those controls, all under 25 µs. Prioritize splitting
the repeatable pre-entry interval at writer publication, native wait return
and RMW take. CPU frequency, cache state and idle exit remain hypotheses.
Scheduler tracepoints would be needed to locate the precise wakeup. All
1,440 source frames across the three batches passed both readers. The
original cold diagnostic also passed. [Raw frames, profiles and per-round summaries](gap-results/).

### Locate the first-hop cost after a longer gap

Use the same branched graph, scoped loans, fixed blocking wait, real source
stamps and first crop profile as the preceding experiment. Opt in to
`BENCH_HOP_TRACE=1` when running the gap-profile cases. Only the RMW source
and first crop record additional events. Each executor thread buffers
timestamps in memory, writing them at teardown. The native Agnocast runs
remain endpoint and callback profile controls. Publisher and reader use the
same host realtime clock as the ROS fixture. Exclude the first ten frames.

The publish-entry marker follows RMW endpoint and loan lookup. Handoff is
immediately before Agnocast publish. Writer return can overlap subscriber
execution and is reported separately. Poll is the first fixed-wait native
readiness marker after ppoll. Take is the point where the host-cloud sample
is identified in RMW take. Restored is after setting the local message's
metadata and Buffer view. Callback entry comes from the existing crop probe.
Publish, take and callback events are joined by exact source stamp. Require
exactly one poll between handoff and take for every analyzed frame.

A second steady-rate run subdivides poll-to-take. Admission means the native
subscription callback has received the loan. Ready means that the mixed RMW
wait is exposing the subscription to ROS. The last interval includes the
remaining wait return, ROS executor dispatch and the beginning of RMW take.
No scheduler or transport setting was changed between the original and
subdivided profiles.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
export BUFFER_RUNTIME_ONLY=1 BENCH_HOP_TRACE=1
bench=experiments/rmw-agnocast/bench-host-cloud.sh
out=build/rmw-agnocast/first-hop-repeat
mkdir "$out"
experiments/buffer/run-lyrical.sh timeout 160 bash "$bench" "$out/alternating" --gaps --profile
experiments/buffer/run-lyrical.sh timeout 160 bash "$bench" "$out/steady" --steady-gaps
for test in alternating steady; do
  python3 experiments/rmw-agnocast/summarize-hop.py "$out/$test"
  python3 experiments/rmw-agnocast/summarize-gaps.py "$out/$test"
done
```

| First-graph-hop median increase at 100 ms, µs | Alternating | Steady | Steady repeat |
|---|---:|---:|---:|
| Stamp to publish-entry marker | 5.4 | 5.4 | 7.0 |
| Publish-entry marker to native handoff | 0.5 | 0.5 | 0.6 |
| Handoff to poll readiness marker | 5.7 | 10.7 | −3.0 |
| Poll readiness to take marker | 18.3 | 19.5 | 18.3 |
| Take marker to restored host-cloud view | 1.0 | 1.0 | 1.0 |
| Restored view to crop callback entry | 2.1 | 1.7 | 2.3 |
| Source stamp to crop callback entry | 32.6 | 29.3 | 27.3 |

The most reproducible interval begins after the thread returns from ppoll.
In the repeat it split into +8.2 µs before the Agnocast subscription callback
received the loan, +0.8 µs to expose readiness and +10.1 µs from readiness
to the RMW take marker. These are differences of segment medians. Their sum
need not equal the difference of end-to-end medians. The native handoff to
poll interval varied even in sign. Restoring the Buffer was a small part of
the measured difference. Trace markers locate costs but cannot show whether
frequency, cache state, executor bookkeeping or another mechanism caused them.

The two profiled 10 Hz batches gave final RMW/native medians of 697/653 µs
and 713/602 µs (1.07× and 1.18×). The repeat would need about 50 µs to
reach 1.10× and 110 µs for parity. Even eliminating its entire measured
first-hop 27 µs idle penalty would leave about 1.14× under an otherwise
identical latency distribution. This arithmetic illustrates scale only.
The native comparator itself is sensitive to idle gaps. Measure a real
intervention at all hops and both endpoints before claiming a ratio.

Next A/B one blocking-path change: use the native descriptor already reported
ready by the outer wait to avoid rediscovering it in the inner Agnocast
executor. Retain the existing path as control and preserve coalesced
notifications, reliability admission, loan ownership and delegated wakeups.
Compare uninstrumented paired 10 Hz runs, then check 50 Hz, CPU and p95.
Inspect ROS ready-to-take separately if it remains dominant.
[Raw observations and per-stage summaries](first-hop-results/).

### A/B the ready subscription descriptor

Both variants use the same binary and blocking outer wait. A goes through
the inner Agnocast executor, which searches its ready queue and calls
zero-time epoll when it needs to discover the event. B maps each native
route's notification FD to its registered callback ID once, while creating
the reader. On POLLIN it invokes the existing Agnocast subscription event
handler for that ID, then executes the resulting work and continuation
batches through the existing callback-group machinery. It processes each
signalled reliability route under the reader's receive lock. The ordinary
take fallback still supports callers that take without a preceding wait.
The switch is `AGNOCAST_RMW_READY_FD=1` with direct wait enabled. A teardown
counter records actual ready-descriptor dispatches.

At each of 10 and 50 Hz, compare A, B and native Agnocast in four rounds,
reversing transport and period order. Both RMW cases use the scoped loan
and fixed blocking wait. The ordinary publisher timer drives all unprofiled
runs. Each reader validates all 60 frames per run. Latency pools the final
50. CPU includes all five application children through teardown. An
independent profile uses the steady-gap fixture, two rounds per period and
RMW variant, with first-hop timestamps joined by exact source stamp.
These profiles use a different publisher loop and support stage attribution.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-loadable.sh
export BUFFER_RUNTIME_ONLY=1
bench=experiments/rmw-agnocast/bench-host-cloud.sh
probe=experiments/rmw-agnocast/run-loadable.sh
out=build/rmw-agnocast/ready-fd-repeat
mkdir "$out"
run() { experiments/buffer/run-lyrical.sh timeout 280 bash "$@"; }
for spec in 'matching native normal' 'matching native flood' pressure host-loan contract; do
  AGNOCAST_RMW_READY_FD=1 AGNOCAST_RMW_DIRECT_WAIT=1 run "$probe" $spec
done
AGNOCAST_RMW_READY_FD=1 run "$bench" --check-cold
run "$bench" "$out/timed" --ready-fd
run "$bench" "$out/profile" --ready-fd-profile
python3 experiments/rmw-agnocast/summarize-ready-fd.py "$out/timed"
for mode in epoll ready; do
  python3 experiments/rmw-agnocast/summarize-hop.py "$out/profile" "$mode"
done
```

The depth-one reliable subscriber retained its single compatible sample
across a 64-sample incompatible burst. The two-route best-effort reader
attributed both writers. Queue pressure, reader restart, retained loans,
reader-death cleanup, serialized cloud reads and the delegated diagnostic
passed with B. In the first timed batch each of the four RMW readers in
every B run reported 60 known-descriptor dispatches.

| Rate and mode | Final median / p95 µs | Relative to native | Child CPU s |
|---|---:|---:|---:|
| 10 Hz native | 594 / 937 | 1.00 / 1.00 | 1.033 |
| 10 Hz A, inner epoll | 684 / 884 | 1.15 / 0.94 | 1.166 |
| 10 Hz B, known descriptor | 641 / 927 | 1.08 / 0.99 | 1.208 |
| 50 Hz native | 508 / 822 | 1.00 / 1.00 | 1.037 |
| 50 Hz A | 556 / 821 | 1.09 / 1.00 | 1.126 |
| 50 Hz B | 540 / 738 | 1.06 / 0.90 | 1.111 |

In the first 10 Hz batch the four same-round final median differences
(B minus A) were −124, +24, +3 and −86 µs. At 50 Hz B had a lower
median in all four rounds. At 10 Hz the profiled first-reader poll-to-loan
median moved from 23.9 to 23.2 µs, while poll-to-take was 41.4 µs in both
variants. The ready-to-take median was also 15.6 µs in both. The long-gap
poll-to-loan penalty persisted (+9.6 µs A, +11.1 µs B). These measurements
rule out the extra ready-queue check and zero-time epoll as a sufficient
explanation of the recurring first-hop latency gap. Work shared by both
variants includes draining eventfd, receiving a kernel loan, executing the
Agnocast callback and dispatching the regular ROS subscription. Their
individual costs still need measurement.

The independent 10 Hz confirmation uses the same unprofiled runner. Append
`--10hz-only` after `--ready-fd` and pass the same option to
`summarize-ready-fd.py`. Keep B opt-in pending a reproducible end-to-end
benefit and tail/CPU evidence. The first batch's 1.08× median alone does
not establish the mechanism.

The 10 Hz repeat gave native 599/1045 µs median/p95, A 689/930 µs
(1.15×/0.89×) and B 717/915 µs (1.20×/0.88×). B minus A median by round
was +43, +18, +93 and −5 µs. Across both batches B won three of eight
paired medians. Median child CPU rose from 1.250 to 1.292 s in the repeat
and from 1.166 to 1.208 s in the first batch. The narrower poll-to-loan
metric in the profiled 10 Hz pairs also changed in both directions
(24.1→21.3 and 23.8→24.6 µs). Leave the default on A. The next discriminating
measurement is eventfd read through the receive ioctl and callback invocation,
and separately exposed readiness through ROS dispatch. All 2,640 source
frames across the timed, profile and confirmation batches passed both
readers. [Raw observations and summaries](ready-fd-results/).

### Split the default receive handler and ROS take path

Use A (the existing inner epoll path) in the branched crop. Build an isolated
Agnocast diagnostic library using the existing Path-probe configuration.
Select it only in the first RMW crop process. Compile-time markers inside the
normal upstream handler record the eventfd read, entry to the deferred receive
function, the receive ioctl, and construction/invocation of the loan callback.
The interval before the ioctl includes receive argument initialization and
acquisition of the mapping mutex. The interval after the ioctl includes any
required mapping, traversal of returned entries and tracepoint bookkeeping.

A small preload library in that same first crop records return from a
successful rcl_wait, entry/return of the subscription loan-capability query,
and entry to rcl_take. The RMW records entry to rmw_take_with_info separately
from the existing typed host-cloud take marker. ROS wait return to the
capability check includes executor selection and early dispatch. From the
capability check return until rcl_take, rclcpp prepares the local message
and enters its type-erased subscription take path. The probe does not
distinguish every operation in those two intervals.

Run four alternating-gap rounds and two steady-rate pairs per transport.
Counters and userspace timestamps are buffered until teardown. For each
post-warmup frame require one ordered set of stage events, one callback ID
between poll and native loan admission, and a matching crop callback stamp.
Each input graph still verifies every field and point at its two readers.
Capture native Agnocast in the same session as an end-to-end control.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
run() { experiments/buffer/run-lyrical.sh "$@"; }
run bash experiments/rmw-agnocast/build-path-trace.sh
run bash experiments/rmw-agnocast/build-loadable.sh
export BUFFER_RUNTIME_ONLY=1 BENCH_RECEIVE_TRACE=1 BENCH_HOP_TRACE=1 AGNOCAST_RMW_READY_FD=0
bench=experiments/rmw-agnocast/bench-host-cloud.sh
out=build/rmw-agnocast/receive-reproduction
mkdir "$out"
run timeout 160 bash "$bench" "$out/alternating" --gaps --profile
run timeout 160 bash "$bench" "$out/steady" --steady-gaps
for t in alternating steady; do
  python3 experiments/rmw-agnocast/summarize-receive.py "$out/$t"
  python3 experiments/rmw-agnocast/summarize-hop.py "$out/$t"
done
```

| Selected first-crop intervals | Alternating gaps, 20 / 100 ms median (µs) | Steady gaps, 20 / 100 ms median (µs) |
|---|---:|---:|
| Eventfd read | 3.1 / 3.2 | 3.2 / 2.9 |
| Receive ioctl | 6.0 / 7.2 | 5.8 / 7.1 |
| Eventfd read return through deferred receive function entry | 1.7 / 3.1 | 1.2 / 3.1 |
| Return from rcl_wait to first loan-capability check | 2.7 / 8.5 | 2.2 / 8.2 |
| Loan-capability check itself | 0.3 / 0.7 | 0.2 / 0.7 |
| Check return to rcl_take entry | 1.0 / 2.5 | 0.6 / 2.6 |
| RMW take entry to identifying the host-cloud sample | 0.2 / 0.4 | 0.2 / 0.3 |

The ioctl consumes several microseconds at either rate, but its median
increased by only 1.2–1.3 µs in the first two profiles. The larger native
admission interval accumulated smaller differences in multiple stages. In
ROS, about 6 µs of additional median latency preceded even the loan check.
The original broad stamp-to-callback difference remained 31.8 µs for the
alternating series and 29.0 µs for the first steady series. These are
differences of medians, not additive per-frame cost estimates. No affinity,
priority or kernel-module setting was changed.

A separate steady-rate control used the regular Agnocast library and kept
only the earlier broad RMW and crop timestamps. Set `BENCH_RECEIVE_TRACE=0`
to reproduce it. Its source-to-callback gap was 30.1 µs, close to 29.0 µs
with the extra probes, though individual quantiles varied. This control
provides a check on observer effects at the broad boundary. It cannot
calibrate the overhead of each individual timestamp.

For a finer steady-rate profile set `BENCH_RECEIVE_TRACE=1 BENCH_EXECUTOR_SPLIT=1`
and rerun `--steady-gaps` in a fresh directory. This selects a separate
diagnostic interposer compiled for the pinned x86-64 Lyrical rclcpp ABI.
It records entry to get_next_ready_executable, the successful return with a
subscription, and entry to execute_subscription. The analysis requires one
of each marker, in order, for every post-warmup cloud. Both preload probes
are local to the first crop in the diagnostic configuration.

The detailed steady repeat gave the following long-minus-short median
differences (µs). RCL wait return to starting selection was +2.4 in the first
run set and +3.7 in the repeat. Looking for and selecting the subscription
itself added +1.0 and +1.5. Selected subscription to entering execution
added +0.3 and +0.6. Entering execution to the loan check added +0.6 and
+0.7. The first interval includes returning through rclcpp's wait-set code
and notify-waitable handling before it calls get_next_ready_executable.
The trace localizes a range of code. It does not identify a particular
waitable as responsible. The close match of the light and detailed profiles
at the broad boundary does not imply microsecond-level measurement accuracy
for every subinterval.

For the CPU-placement observation set `BENCH_RECEIVE_CPU=1` with the
executor-split interposer in the alternating-gap case. Run
`python3 experiments/rmw-agnocast/summarize-receive-cpu.py RESULT_DIR`.
It compares the logical CPU at the first crop's successful wait return for
successive frames. On the same observed CPU, the short/long medians from
source stamp to first callback were 49.0/83.3 µs (85/87 frames). On a different
observed CPU they were 59.4/99.1 µs (15/13 frames). The latter group is
small and the groups were not randomized. A change of CPU between observations
is unnecessary for the gap to appear. Migration within an interval and CPU
power/cache state were not measured. These observations do not establish an
affinity fix.

Across the six batches, all 2,880 source frames reached both validating
readers. The original two-edge cold diagnostic delivered all 60 messages.
Use the existing epoll A path. Before changing the kernel or skipping
executor correctness checks, compare a fixed amount of CPU work at callback
entry after the two idle intervals, stratified by observed CPU. That would
test whether the slowdown generalizes beyond this ROS receive code.
[Raw observations, stage summaries, CPU groups and checks](receive-results/).

### Fixed CPU work at the first crop callback

Use the existing branched Universe crop benchmark and blocking wait A. The
first crop records its entry time, then measures two identical blocks of
4,096 dependent unsigned shift/XOR/multiply iterations. Each block starts at
one. Its clocks are raw monotonic wall time and callback-thread CPU time.
The empty control uses the same calls and zero iterations. Observations stay
in memory until the last frame. The source alternates actual 20 and 100 ms
intervals within a process. Four rounds reverse the starting phase and
transport order. Exclude ten warmup frames per run.

From the experimental worktree with the existing Lyrical root and overlays:

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
run=experiments/buffer/run-lyrical.sh
bench=experiments/rmw-agnocast/bench-host-cloud.sh
out=build/rmw-agnocast
"$run" bash experiments/buffer/build-universe-lyrical.sh
export BUFFER_RUNTIME_ONLY=1 BENCH_HOP_TRACE=0 BENCH_RECEIVE_TRACE=0 AGNOCAST_RMW_READY_FD=0
for batch in work empty repeat; do
  export BENCH_FIXED_WORK=work
  rounds=4096
  if [[ $batch == empty ]]; then
    export BENCH_FIXED_WORK=empty
    rounds=0
  fi
  "$run" timeout 160 bash "$bench" "$out/register-$batch" --gaps --profile || exit 1
  python3 experiments/rmw-agnocast/summarize-fixed-work.py "$out/register-$batch" "$rounds" || exit 1
done
unset BENCH_FIXED_WORK
"$run" timeout 70 bash "$bench" --check-cold
```

The analysis checks both block results against an independent calculation,
joins all crop observations to source timestamps and checks both reader
sequences. The same-CPU subset requires matching CPU observations after the
previous probe and before and after the current pair. Migrations between
observations remain possible. Arrival means source stamp to callback entry,
measured before the probe executes. Probe runs add work to the graph and
are diagnostic measurements.

| Batch | Transport | Arrival after 20 / 100 ms, median µs | First block after 20 / 100 ms, median thread µs |
|---|---|---:|---:|
| Work | Native Agnocast | 37.37 / 45.25 | 6.91 / 6.91 |
| Work | rmw_agnocast | 53.21 / 83.29 | 6.81 / 6.65 |
| Empty | Native Agnocast | 38.56 / 44.64 | 1.02 / 1.06 |
| Empty | rmw_agnocast | 51.98 / 81.28 | 1.15 / 1.21 |
| Work repeat | Native Agnocast | 35.73 / 44.49 | 6.46 / 5.34 |
| Work repeat | rmw_agnocast | 66.79 / 79.17 | 6.44 / 6.58 |

Each cell contains 100 post-warmup frames per interval. All 12 RMW rounds
had positive long-minus-short arrival medians (+7.2 to +32.8 µs). The two
work batches gave first-block RMW wall medians within 0.22 µs across gaps.
Restricting to the same observed CPU gave arrival differences of +29.60
and +11.59 µs, and first-block CPU differences of −0.27 and +0.10 µs.
The empty probe cost about 1 µs of thread time. There was no repeatable
long-gap arithmetic penalty at callback entry. The receiver has already
run the wake and dispatch path by then. Transient effects earlier during
wake, instruction or data locality, and work specific to the ROS receive
path remain open.

Keep blocking A. For the next causal test, run the same small probe
immediately after the first crop returns from its blocking poll, alternating
empty and work runs. Compare probe duration and the subsequent poll-to-take
interval separately against the earlier callback-entry control. Report the
total source-to-callback time as well: work inserted early can move cost
between stages. Keep this diagnostic off during transport comparisons.
[Raw samples, per-round differences, p95 and runtime checks](fixed-work-results/).

### Blocking-poll probe and graph-wide scheduler hint

Question: can the branched graph remove its excess 10 Hz final-sink latency?
Start with the same blocking RMW path (A) and 512 KiB clouds. For the
diagnostic only, mark the first crop's return from ppoll after checking its
native readiness bits, then time zero or 4,096 dependent shift/XOR/multiply
iterations. Check the checksum and record thread CPU and wall time. Join
each wake to its exact cloud via the handoff and take markers. Across four
rounds per transport per condition, discard ten startup frames per run. Each
RMW column below represents 100 frames per interval. Numbers are medians in
microseconds. The empty condition precedes work, so batch differences can
include host drift.

| First crop interval | Empty 20 / 100 ms | Work 20 / 100 ms |
|---|---:|---:|
| Probe thread CPU | 1.0 / 1.0 | 5.3 / 5.2 |
| Probe wall | 2.7 / 3.1 | 7.1 / 7.4 |
| Probe end to take | 30.6 / 42.6 | 30.9 / 41.5 |
| Blocking poll return to take | 33.5 / 45.7 | 39.6 / 49.2 |
| Source stamp to callback | 68.3 / 87.0 | 71.3 / 91.3 |

The probe found a similar arithmetic cost at both gaps. Work after wake
left the long-gap dispatch penalty visible, and total median arrival rose
in the work batch. Test one earlier intervention: run the whole candidate
process tree under Linux uclamp.min 512 (on a 0–1024 scale). The controls
use minimum 0. All three inherit their ordinary policy, affinity and maximum
clamp. The runner checks the effective minimum of each launched application
process. This changes the scheduler's utilization input. The experiment
does not measure actual clock frequency, idle residency, or energy.

Interleave native Agnocast, default A, and A with the hint at steady 10 and
50 Hz. Use the ordinary publisher timer and disable hop, crop and receive
profiling. The same A binary handles both RMW cases. Compare four rounds per
transport and rate in each of two fresh batches. Pool 200 post-warmup frames
per cell. Compare final sink, tap, aggregate child CPU including start/stop,
loan and delivery counts. The per-round excess penalty is the 10 Hz minus
50 Hz median for the RMW minus that same difference for native.

```bash
set -e
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
run=experiments/buffer/run-lyrical.sh
bench=experiments/rmw-agnocast/bench-host-cloud.sh
out=build/rmw-agnocast
"$run" bash experiments/rmw-agnocast/build-loadable.sh
export BUFFER_RUNTIME_ONLY=1 AGNOCAST_RMW_READY_FD=0 BENCH_RECEIVE_TRACE=0
for mode in empty work; do
  BENCH_HOP_TRACE=1 BENCH_POLL_WORK=$mode "$run" timeout 160 bash "$bench" "$out/poll-$mode" --gaps --profile
  python3 experiments/rmw-agnocast/summarize-hop.py "$out/poll-$mode"
done
unset BENCH_POLL_WORK BENCH_HOP_TRACE BENCH_FIXED_WORK
for batch in timed repeat; do
  "$run" timeout 320 bash "$bench" "$out/uclamp-$batch" --uclamp
  python3 experiments/rmw-agnocast/summarize-ready-fd.py "$out/uclamp-$batch" --uclamp
done
"$run" timeout 70 bash "$bench" --check-cold
```

| Rate | Transport | Batch 1 median / p95 µs | Batch 2 median / p95 µs | Batch 1 / 2 child CPU s |
|---|---|---:|---:|---:|
| 10 Hz | Native | 576 / 1316 | 567 / 990 | 1.034 / 1.033 |
| 10 Hz | A | 670 / 921 | 652 / 964 | 1.175 / 1.148 |
| 10 Hz | A + clamp | 676 / 905 | 668 / 851 | 1.150 / 1.125 |
| 50 Hz | Native | 532 / 895 | 542 / 860 | 1.009 / 1.032 |
| 50 Hz | A | 606 / 853 | 558 / 1172 | 1.129 / 1.117 |
| 50 Hz | A + clamp | 583 / 849 | 585 / 1852 | 1.144 / 1.190 |

At 10 Hz the hint raised the pooled median by 5.5 and 15.6 µs and improved
only three of eight round-paired medians. Its 10 Hz pooled p95 and aggregate
CPU were lower in both batches. At 50 Hz the median changed direction
between batches and the repeat's p95 reached 1.85 ms. Four short runs per
cell leave considerable uncertainty about tails. The pooled excess 10 Hz
penalty above native was about 20 and 69 µs for A, and 48 and 58 µs with
the hint. The smaller excess for the hint in the repeat coincided with a
slower 50 Hz median. Absolute 10 Hz performance missed the objective in
both batches. Keep the ordinary scheduler configuration and blocking A.

Close this crop-only investigation here. The low-rate effect reproduced
and part of it persists in the receive/dispatch path after wake. Its exact
cause and a remedy remain open. Move the workload milestone to the Universe
multi-LiDAR synchronization/concatenation graph, retaining this benchmark as
a control. Compare at matched delivered sensor rates with native, A, Cyclone
DDS and Zenoh, and validate synchronized output contents and timestamps.
[Per-frame observations, resource counters, paired results and checks](poll-clamp-results/).

### Universe multi-LiDAR concatenation and synchronization

Build the actual PointCloudConcatenateDataSynchronizerComponent, its collectors,
matcher, transform and cloud-combination code from the tracked Universe subtree.
The isolated colcon target registers the same plugin class and executable as
Universe. It avoids building unrelated CPU/GPU filters. The node uses regular
rclcpp publishers/subscriptions. The application build and launch use
ENABLE_AGNOCAST=0. The existing Lyrical overlay supplies managed TF and message
dependencies. No relay is started.

The C++ fixture is a separate process from the synchronizer. It drives two
10 Hz inputs and one 5 Hz input, with acquisition stamps offset by 0/1/2 ms.
One input is in base_link and two are in a sensor frame translated by +4 m
through a separate ROS static TF publisher. Motion compensation and optional
per-sensor output clouds are disabled. Every other group is missing the third
sensor by design and should publish after the configured 40 ms timeout. For
each output, check frame, shape, oldest stamp, per-sensor stamp, exact payload
bytes including TF, segment positions and lengths, and success/timeout status
on the separately received output_info topic. Reject duplicate or missing
groups and abnormal child termination. This is a controlled, synthetic sensor
workload running the real algorithm on the laptop.

The Cyclone and Zenoh references use their stock RMWs. The other three modes
select the same four resolved cloud topics in rmw_agnocast. TF, ROS services
and output_info remain delegated to Cyclone. CDR serializes the clouds.
Host uses the same-host representation with the component's default CPU
subscription and normal publishing. Fixed uses that same representation and
the existing single-threaded scoped wait runner **only for the synchronizer**.
It enters blocking A after the first verified-fixture output arrives, by
which time the component has lazily created its collectors and timers.
Check the fixed-wait counters at teardown. Ordinary node startup is exercised
in all other modes. The two compiled RMW host-cloud modes copy the received
payload into the CPU Buffer. These runs do not measure loaned host views.
Native Agnocast has no equivalent transport row for this rclcpp-only component.

From this worktree with the existing Lyrical root, Agnocast kernel module and
crop/RMW overlays:

```bash
set -e
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/build-sync.sh
bash experiments/rmw-agnocast/bench-sync.sh build/universe-sync/new-batch
```

Both matrix passes use 32,768 XYZIRC points (512 KiB) per sensor input.
The fixture prepares the input bytes before timing, stamps immediately before
publishing the first input of the group, and measures until receipt of the
combined cloud in the fixture process. Each pass covers 24 groups, 60 inputs
and 1,966,080 input points per mode. The repeat reverses mode order. Report
complete groups and intentional timeouts separately. After four warmup
groups, each per-pass p95 is the largest of ten samples of that kind. Child
CPU includes launch, discovery, processing, validation and teardown, plus the
router for Zenoh. Short-run tails and CPU totals are descriptive.

| Mode | Complete median ms, pass 1 / 2 | Complete p95 ms, pass 1 / 2 | Partial median ms, pass 1 / 2 |
|---|---:|---:|---:|
| Cyclone DDS | 4.92 / 5.58 | 5.75 / 6.28 | 43.47 / 43.65 |
| rmw_agnocast, CDR | 4.60 / 4.24 | 5.92 / 5.79 | 43.04 / 43.09 |
| rmw_agnocast, host-cloud | 4.64 / 4.49 | 6.15 / 5.39 | 42.27 / 42.18 |
| rmw_agnocast, host-cloud + A | 4.38 / 5.15 | 5.51 / 5.44 | 42.15 / 42.44 |
| Zenoh | 4.49 / 3.56 | 6.13 / 4.29 | 43.04 / 42.79 |

The final two passes validated all 240 output clouds and metadata records,
including the expected 120 partially populated outputs. All processes exited
cleanly. Both A runs reported 57 native wakes, 24 delegated completions and
zero native-triggered DDS interrupts. A worked with the real timer and
subscription topology. Its advantage over ordinary host-cloud varied in
sign at the complete-group median. The next comparison should isolate
payload copies on this exact graph before changing deployment defaults.
[Per-set latency, exit status, counters, CPU and memory](sync-results/).

### Read a retained input loan in the Universe synchronizer

On the existing 10/10/5 Hz three-sensor graph, change only the synchronizer's
input side. The isolated colcon target applies
loan-input.patch to two build-directory copies of the Universe sources.
With AUTOWARE_ENABLE_AGNOCAST_HOST_VIEW=OFF it compiles the original
sources directly. Both the adapter and instrumentation live in the fixture.
The prototype RMW exports its view header with its ament package. The three
synchronizer subscriptions request `agnocast_host_view` through Lyrical's
`acceptable_buffer_backends` option.
The prototype RMW recognizes this name in a comma-separated list as well as
the original `any` opt-in. The ordinary CPU default still receives a copy.

The collector retains the original const ROS message. When the group is
complete or its timer expires, the converter reads XYZIRC bytes directly
from the const vector owned by the received Buffer implementation. Both
the byte-copy and byte-view cases use the same converter, blocking A,
normal publishers and verifying sink. Packed rows use a bulk copy into the
canonical XYZIRC cloud. With a longer point stride the converter copies the
XYZIRC prefix and skips the extra point and row bytes. It requires a valid
native-endian XYZIRC prefix and checks strides and data length. The existing
transform and concatenation steps and their allocations run in both cases.
The fixed reference uses the original per-point converter. Comparing fixed
to byte-view alone would conflate the converter and transport changes.

The timed runs record counts at the converter. A separate untimed audit
compares the address of the vector being read against the retained owner's
address and stores weak references. For the padded test, the third sensor
has two rows, a 20-byte point stride and 8 padding bytes per row. The
fixture independently checks the same transformed output and per-sensor
metadata as for packed input. At shutdown all 60 weak owners had expired
after 33,030,336 input bytes. The packed graph avoids a 512 KiB RMW-to-CPU
copy on each of 60 input messages (30 MiB per run). The publisher and
output still materialize payloads. The older host-view probe separately
checks address identity, mutation isolation, retention and final release.

From the same worktree and Lyrical root as above:

```bash
set -e
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
run=experiments/buffer/run-lyrical.sh
"$run" bash experiments/rmw-agnocast/build-loadable.sh
AUTOWARE_ENABLE_AGNOCAST_HOST_VIEW=OFF "$run" bash experiments/rmw-agnocast/build-sync.sh
BUFFER_RUNTIME_ONLY=1 "$run" timeout -k 4 45 bash experiments/rmw-agnocast/run-sync.sh cyclone
"$run" bash experiments/rmw-agnocast/build-sync.sh
export BUFFER_RUNTIME_ONLY=1
"$run" timeout 60 bash experiments/rmw-agnocast/run-loadable.sh host-view
for mode in byte-copy byte-view; do
  SYNC_LARGE=1 SYNC_AUDIT=1 SYNC_STRIDED_INPUT=1 "$run" timeout -k 4 45 \
    bash experiments/rmw-agnocast/run-sync.sh "$mode"
done
bash experiments/rmw-agnocast/bench-sync.sh build/universe-sync/new-input-test --loan-input
```

The first two timed passes include Cyclone, Zenoh, CDR, host-cloud and the
original fixed control. Two further passes repeat the matched input pair.
Orders for the four pairs are copy/view, view/copy, view/copy and copy/view.
Two additional passes run Cyclone and Zenoh with the same byte converter,
reversing their order. Their ordinary executor and middleware differ from A.
Keep complete and intentionally partial groups separate. Each per-run
median and p95 uses ten post-warmup groups of each kind. Child CPU includes
startup and teardown. A native Agnocast comparator for this rclcpp-only
component is still unavailable.

| Complete-group median, ms | Pair 1 | Pair 2 | Pair 3 | Pair 4 |
|---|---:|---:|---:|---:|
| Blocking A, copied CPU input | 4.05 | 3.89 | 3.97 | 4.16 |
| Blocking A, retained view | 2.74 | 2.60 | 2.88 | 2.65 |

The view lowered the matched median by 1.09–1.51 ms in all four pairs
and the observed p95 by 0.90–1.40 ms. Partial-group medians clustered
at 41.56–41.75 ms with the 40 ms timeout. CPU totals overlapped. With
the same converter and ordinary ROS executors, the two Cyclone reference
medians were 4.70 and 4.64 ms, and Zenoh's were 2.64 and 3.11 ms.
All 22 timed graphs verified their outputs and exited cleanly. Keep the view
as the experimental input baseline. The next distinct copy to investigate
is on publication of the newly constructed concatenated cloud.
[Per-frame timings, counts and memory checks](sync-input-results/).

### Transfer the Universe synchronizer output through a publisher loan

Keep the three retained input subscriptions, blocking A, source cadence, TF,
collector timeout and ordinary CPU sink from the preceding experiment. The
synchronizer normally hands a unique PointCloud2 to rclcpp. Its host-cloud
RMW publication still receives a const message and copies the finished vector.
Compare that path (byte-view) with two ways to move the completed message
into a short rclcpp publisher loan. Output-private keeps the normal vector
allocation. Output-shared uses the existing Agnocast heap hook around the
single output reserve, restores the thread-local allocator state immediately,
and appends the points using the existing Universe code. The RMW checks the
finished vector against the writer's shared mapping and copies private
storage on fallback. No change to the RMW or kernel is needed for this test.
The fixture applies loan-output.patch only in its build directory. The scoped
allocator calls are Agnocast-specific and experimental.

On the reference laptop, source the same overlays and use the same exported
Lyrical root as in the retained-input experiment. Run one graph at a time:

```bash
set -e
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
run=experiments/buffer/run-lyrical.sh
"$run" bash experiments/rmw-agnocast/build-sync.sh
export BUFFER_RUNTIME_ONLY=1
for mode in output-private output-shared; do
  SYNC_LARGE=1 SYNC_AUDIT=1 SYNC_STRIDED_INPUT=1 "$run" timeout -k 4 45 \
    bash experiments/rmw-agnocast/run-sync.sh "$mode"
done
bash experiments/rmw-agnocast/bench-sync.sh build/universe-sync/new-output-test --loan-output
for mode in byte-view output-private output-shared output-shared output-private byte-view; do
  SYNC_LARGE=1 SYNC_OUTPUT_PROFILE=1 "$run" timeout -k 4 45 \
    bash experiments/rmw-agnocast/run-sync.sh "$mode"
done
```

The timed comparison uses four order-balanced ordinary/shared pairs, two
private-loan controls, and fresh Cyclone DDS and Zenoh references with CPU
input and the same XYZIRC converter. The fixture verifies all points and
metadata for full and intentionally partial groups. Latency runs from the
oldest sensor stamp to the sink callback. Per-run median and nearest-rank
p95 use ten post-warmup groups of each kind. Separate instrumented runs time
output reserve, the sum of the per-sensor appends, and the call to publish
(including borrow and move in the loan cases). These three durations cover
only part of the end-to-end path. Profiling records are emitted after publish.

| Transport and output policy | Complete median, ms | Complete p95, ms | Partial median, ms |
|---|---:|---:|---:|
| rmw_agnocast, ordinary publication | 2.43–2.83 | 3.28–3.74 | 41.45–41.61 |
| rmw_agnocast, private vector moved into loan | 2.67–2.77 | 3.09–4.04 | 41.60–41.63 |
| rmw_agnocast, shared vector moved into loan | 3.58–3.99 | 4.64–5.16 | 42.19–42.48 |
| Cyclone DDS | 3.98–8.47 | 4.49–13.04 | 42.86–46.32 |
| Zenoh | 2.53–3.01 | 3.27–3.63 | 41.98–42.12 |

The table gives ranges of per-run statistics. The 14 timing graphs verified
336 outputs. All four shared runs recorded 24 publisher loans with zero
private copies. Both private controls recorded 24 copies. Complete-group
median increased by 1.03–1.49 ms in the four paired ordinary/shared runs.
Partial groups include the configured 40 ms timeout. The Cyclone and Zenoh
rows provide ordinary ROS references. No matched native Agnocast port exists
for this synchronizer.

In the separate profile, the shared writer's full-group median publish call
took 15–16 µs versus 79–104 µs for private loans. Writing the shared output
was sometimes more expensive early in a run. For groups 4, 6, 8 and 10 the
three appends together took 0.77–1.07 ms in the two shared profiles. For the
following six complete groups they took 0.06–0.09 ms. The uninstrumented
shared route was slower in the later groups as well.

To repeat the timestamp attribution, create a fresh directory under /ws for
each diagnostic invocation. Set SYNC_TRACE_DIR to its absolute in-runner path
and run output-private, output-shared, output-shared and output-private with
SYNC_LARGE=1 using the guarded runner above. The existing RMW traces record
the writer and the sink separately. Join on the oldest sensor stamp. The
following are full-group medians from two runs per mode:

| Interval, µs | Private output loan | Shared output loan |
|---|---:|---:|
| Oldest sensor stamp to output RMW publish entry | 1,478 / 1,551 | 2,919 / 2,492 |
| Output RMW publish entry to native handoff | 66.7 / 67.1 | 0.6 / 0.6 |
| Native handoff to sink take | 58.5 / 55.8 | 59.8 / 65.3 |
| Sink take to restored CPU message | 695 / 650 | 659 / 670 |

These measurements place the dominant difference before output RMW
publication. Keep retained input plus ordinary publication as the laptop
baseline. The next diagnostic should timestamp input admission, collector
match, per-sensor conversion and transform, and completion of concatenation
on the same groups. The current evidence cannot distinguish scheduling and
input-arrival effects from the rest of the construction cost.
[Per-group timings, CPU, memory, traces and checks](sync-output-results/).

### Native Agnocast comparator for the three-LiDAR synchronizer

Build the native variant into build/universe-sync-native. The existing Agnocast
kernel, heap hook, Lyrical library and CIE are reused. The isolated target builds
the sensing message package and the synchronizer fixture. It applies native.patch
to private build copies. All four PointCloud2 topics use typed native Agnocast
publishers and subscriptions. Metadata, TF and the collector's original ROS timer
continue through rclcpp. The native run selects Cyclone for the ROS entities and
disables the Agnocast bridge. ENABLE_AGNOCAST alone does not implement these
endpoints.

Both variants generate the same source clouds ahead of the clock, start timing
at the oldest input stamp and publish the already constructed inputs. Native
performs one copy from each finished private message into a publisher loan,
including the finished concatenated output. The native input callback retains
the native loan behind a local Buffer view so the converter can read the foreign
CPU vector while running virtual Buffer operations in its own process. The
RMW path uses its existing retained-input view and ordinary output publication.
The native sink retains output loans and verifies the native CPU vectors. The
RMW baseline uses its regular CPU subscriber. Both run the same TF, XYZIRC
conversion, matching algorithm, source segment checks and timeout assertions.

The native component and sink use the Agnocast mixed callback-isolated executor.
The fixture drives the native source on its main thread, then joins the executor
before inspecting results. Blocking A drives the RMW component, and its fixture
uses single-threaded rclcpp dispatch. This measures deployed graph behavior,
including executor differences. The 60 inputs per graph are two 10 Hz streams
and one 5 Hz stream. The alternating 1.5 MiB and 1 MiB outputs represent three
and two sensors respectively.

```bash
set -e
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
run=experiments/buffer/run-lyrical.sh
SYNC_NATIVE=ON "$run" bash experiments/rmw-agnocast/build-sync.sh
"$run" bash experiments/rmw-agnocast/build-sync.sh
export BUFFER_RUNTIME_ONLY=1
SYNC_LARGE=1 SYNC_AUDIT=1 SYNC_STRIDED_INPUT=1 "$run" timeout -k 4 45 \
  bash experiments/rmw-agnocast/run-sync.sh native
bash experiments/rmw-agnocast/bench-sync.sh build/universe-sync-native/new-matrix --native
```

Four native/RMW pairs reverse order twice. Two additional pairs reverse the
ordinary Cyclone DDS and Zenoh references with the same byte converter.
Discard four groups per graph, then calculate pooled median and nearest-rank
p95 separately for complete and partial groups. Per-run statistics and whole
graph user+system CPU time are retained alongside each group's latency.

| Transport | Complete median / p95 ms | Partial median / p95 ms | Complete median / p95 relative to native | Median graph CPU s |
|---|---:|---:|---:|---:|
| Native Agnocast with mixed CIE | 2.525 / 2.980 | 52.048 / 52.513 | 1.000 / 1.000 | 1.184 |
| rmw_agnocast with retained input, blocking A | 2.459 / 3.788 | 41.625 / 42.121 | 0.974 / 1.271 | 1.317 |
| Cyclone DDS | 3.973 / 5.402 | 43.034 / 43.730 | 1.574 / 1.813 | 0.999 |
| Zenoh | 2.743 / 3.550 | 42.091 / 42.803 | 1.086 / 1.191 | 1.496 |

All 12 timing graphs verified all points and source metadata in 288 outputs.
Each native and RMW statistic pools 40 post-warmup groups of the stated kind.
The RMW complete-group p95 was higher in all four paired runs. Keep this as
a workload-specific tail finding while tuning is deferred. Timeout groups
have different executor behavior. The native CIE reported mixed ROS and
Agnocast callbacks on the cloud group; its default native timed wait is
50 ms. This is consistent with the 52 ms partial-group latency, although the
run did not vary that wait to establish causality. The RMW partial median was
41.625 ms with the same configured 40 ms collector timeout.
[Verified runs, lifetime audit, CPU and memory readings](sync-native-results/summary.txt).

### Launch three Universe crops into the three-LiDAR synchronizer

Use the tracked Universe CPU crop and synchronizer in a colcon overlay with
ENABLE_AGNOCAST=0 at build and runtime. The launch file loads three independent
CropBoxFilterComponent instances into the standard single-threaded
rclcpp_components container through its load-node service. The synchronizer
runs in its own process. Separate source and sink processes exercise every
cloud edge across a process boundary. A static TF process supplies the same
4 m transform as the three-LiDAR control.

The source publishes 60 packed, 512 KiB PointCloud2 messages over 24 groups
at 10/10/5 Hz. Each crop retains a different contiguous half of its input.
For the left, right and top sensors the accepted point indices start at
0, 8192 and 16384, respectively. At the sink, compare the bytes of each
transformed segment against the corresponding original point indices.
Check the cloud and input stamps, frame, source offsets, counts, timeout
status, missing or duplicate groups and clean termination. The source writes
its timestamp manifest after publication. The file is a verifier input;
point clouds and concatenation metadata travel on the ROS graph.

The plain variant uses both original component implementations, including
the original converter. The stock variant uses the existing retained-input
fixture adaptation and the same standard ROS executables. The runner variant
changes only the synchronizer executor to blocking A after its first output.
The two ordinary middleware references use the byte converter with CPU inputs.
All crops use the original code in every mode. The crop benchmark package
also builds an older endpoint tool; it now uses the wrapper allocation macro
so the package compiles with Agnocast disabled.

For rmw_agnocast, select the installed experimental RMW and host-cloud mode
on the application processes, preload the matching heap hook, and give all
participants access to the host Agnocast device. Select seven resolved cloud
topics: three raw inputs, three crop outputs, and one concatenated output.
The component load service, TF and output_info use delegated Cyclone DDS.
The launch client uses Cyclone directly. The bridge is disabled. Only the
runner variant enables the scoped wait probe. Runtime setup for each variant
is in run-pipeline.sh and the installed three_lidar.launch.py.

```bash
set -e
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
run=experiments/buffer/run-lyrical.sh
"$run" bash experiments/rmw-agnocast/build-pipeline.sh
"$run" bash experiments/rmw-agnocast/build-pipeline.sh plain
bash experiments/rmw-agnocast/bench-pipeline.sh build/universe-pipeline/fresh-pairs
```

The build reuses the existing Lyrical Agnocast, RMW and sensing message
overlays. A separate plain synchronizer prefix compiles the tracked source
directly. The driver runs one launch at a time in forward and reverse order.
Discard the first four groups per run; report pooled median and nearest-rank
p95 for 20 complete and 20 partial groups per mode. The reference CPU times
include launch, discovery, source preparation, verification and shutdown.

| Graph mode | Complete median / p95 ms | Partial median / p95 ms | Graph CPU s, runs 1 / 2 |
|---|---:|---:|---:|
| rmw_agnocast, original sources and stock launch | 3.379 / 4.430 | 42.034 / 42.390 | 1.833 / 1.692 |
| rmw_agnocast, retained input and stock launch | 2.404 / 3.359 | 41.603 / 42.084 | 1.681 / 1.755 |
| rmw_agnocast, retained input and blocking-A runner | 2.407 / 3.132 | 41.635 / 41.904 | 1.636 / 1.889 |
| Cyclone DDS | 2.868 / 3.270 | 42.114 / 42.392 | 1.659 / 1.793 |
| Zenoh | 2.765 / 3.594 | 42.133 / 42.346 | 2.708 / 2.838 |

All ten launches verified 240 concatenated clouds and their metadata from
600 raw sensor messages. All seven Agnocast host-cloud routes had actual
readers and writers. The retained-input runs each reported 60 views and
zero copied synchronizer inputs. The ordinary-source and retained-input
rows also differ in XYZIRC conversion. The directly comparable executor
rows have similar medians. Two runs per mode give limited tail evidence.
Use stock launch for the next workload; keep the runner as a diagnostic
control. The earlier native synchronizer measurement has a shorter graph
and is not a denominator for these results.
[Timing samples and verification](pipeline-launch-results/summary.txt)
and [build selections](pipeline-launch-results/build-checks.txt).

### Top LiDAR arrival jitter

Two paired launches per middleware used the same three crops, synchronizer,
10/10/5 Hz input and nominal acquisition stamps. Baseline top publications
started 0.38–4.22 ms after group start. Under jitter they started at
15.04–15.07 or 50.05–50.07 ms. These offsets were measured at the raw
publisher. Synchronizer ingress was not instrumented.

| Middleware | Baseline full | 15 ms full | 50 ms left/right | 50 ms top only |
|---|---:|---:|---:|---:|
| rmw_agnocast, retained inputs and stock launch | 2.483 / 3.023 | 17.135 / 17.649 | 41.580 / 42.168 | 89.368 / 89.590 |
| Cyclone DDS | 2.925 / 3.761 | 17.256 / 17.693 | 41.983 / 42.246 | 89.467 / 89.798 |
| Zenoh | 2.811 / 3.519 | 17.056 / 17.423 | 41.806 / 42.351 | 89.489 / 89.841 |

Values are median / nearest-rank p95 in ms from the oldest contributing
acquisition stamp to the sink callback. First four acquisition groups per
launch are excluded from timing only. Baseline full has 20 measured outputs
per middleware and each jitter column has 10, so its p95 is the observed
maximum. Every launch verified all 60 inputs through payload and metadata.
At 15 ms the top joined its pair. At 50 ms the pair timed out and the top
formed a second timed-out output before the next group. The roughly 89 ms
top-only result includes the imposed delay and its own matching window.
Odd groups with the top absent had similar median latency across conditions.
Stock rmw_agnocast retained all 60 synchronizer inputs, and a separate audit
verified every loan was released. Native Agnocast on this complete graph
remains unmeasured. Reproduction is in the [journal](../buffer/JOURNAL.md#2026-11-21--top-lidar-arrives-on-either-side-of-the-synchronizer-timeout).
[All samples, CPU and memory ranges](pipeline-arrival-results/summary.txt).

### Slow downstream consumer

Each cloud callback slept 125 ms in the single-threaded sink, with nominal
10/10/5 Hz sensor timing. The sink recorded latency on entry, before sleeping.
All 12 paired timing launches verified 12 full and 12 expected partial groups
each, including member stamps, transformed payload and metadata. Each timing
cell is pooled median / nearest-rank p95 in ms (20 post-warm-up samples per
group type and condition).

| Middleware | Baseline full | Slow full | Baseline partial | Slow partial |
|---|---:|---:|---:|---:|
| rmw_agnocast | 3.02 / 4.88 | 329.23 / 584.64 | 42.03 / 42.97 | 354.87 / 610.18 |
| Cyclone DDS | 3.96 / 4.67 | 331.18 / 585.67 | 42.62 / 43.13 | 356.96 / 611.25 |
| Zenoh | 3.50 / 5.03 | 327.61 / 579.51 | 42.50 / 43.19 | 353.20 / 604.84 |

The callback can process at most eight clouds per second while ten arrive.
Latency therefore grew with sequence number. These percentiles describe
the 24-group burst and drain. In separate stock rmw_agnocast traces,
reader admission to pending-loan release rose from 0.395 / 0.543 ms to
327.105 / 568.497 ms. Concurrent pending loans peaked at one and five
respectively. Five fits within the depth-32 sink, so loss and eviction
under sustained overload remain open. Both traces accounted for 24 output
loans and both synchronizer audits released 60 of 60 retained input views.
The whole-scope peak includes launch and allocator overhead. Two repeats
cannot resolve the byte cost of five queued loans from this measure.
[Timing, CPU and memory](pipeline-slow-results/summary.txt) ·
[Queue diagnostics](pipeline-slow-results/queue-summary.txt) ·
[Reproduction](../buffer/JOURNAL.md#2026-11-21--slow-output-consumer-at-nominal-lidar-timing).
