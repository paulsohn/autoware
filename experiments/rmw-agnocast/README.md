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
