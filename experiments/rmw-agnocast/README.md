# Whole-message loans with Agnocast storage

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
