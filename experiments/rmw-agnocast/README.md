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

1. Exercise two readers including a slow reader, KeepLast pressure, outstanding
   loans at shutdown, endpoint restart and mixed native/DDS waits. Record
   deliveries, drops, final loan returns and teardown hangs. Validate metadata,
   error paths and unsupported RMW operations before advertising capabilities.
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
