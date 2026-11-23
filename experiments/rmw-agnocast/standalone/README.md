# Standalone same-host RMW milestone

The rmw_agnocast library in this revision owns its context, node and endpoint handles, guard conditions and blocking wait. The publisher lends native objects through Agnocast. The subscriber queues kernel loans and wakes its local RMW context. Two matching processes on one host need the compatible device and heap hook. Ordinary supported topic names go directly to native transport without topic lists. Native route identity includes the ROS domain, resolved name, canonical C++ message type, reliability and durability. This revision versions the native route because the shared sample now carries an expiry time, source timestamp and publication sequence. Upgrade communicating processes together.

The release package of the heap hook and the running kernel module are unchanged. The Agnocast **userspace library** built from this revision suppresses its legacy automatic bridge and discovery processes when rmw_agnocast is selected. Build that library alongside the RMW. Use a fresh install prefix to avoid stale bridge executables from earlier builds.

## Reproduce on the provisioned Lyrical laptop

Use a scratch copy of the distro prefix on the same filesystem. In the following shell commands the build/standalone/distro path must be new. The Python step removes only hard links in that scratch prefix. It uses the installed Debian file manifests for both Cyclone packages.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
mkdir -p build/standalone
cp -al "$LYRICAL_ROOT/opt/ros/lyrical" build/standalone/distro
python3 - <<'PY'
import os
from pathlib import Path
root = Path(os.environ["LYRICAL_ROOT"])
for package in ["rmw-cyclonedds-cpp", "cyclonedds"]:
    manifest = root / f"var/lib/dpkg/info/ros-lyrical-{package}.list"
    for entry in manifest.read_text().splitlines():
        if entry.startswith("/opt/ros/lyrical/"):
            path = Path("build/standalone/distro") / entry.removeprefix("/opt/ros/lyrical/")
            if path.is_file() or path.is_symlink():
                path.unlink()
PY
experiments/buffer/run-lyrical.sh bwrap --ro-bind / / --dev-bind /dev /dev --bind /ws /ws --bind /tmp /tmp --ro-bind /ws/build/standalone/distro /opt/ros/lyrical --chdir /ws bash --noprofile --norc
```

In that shell source the pilot's previously built rclcpp and Agnocast build prerequisites. The distro's generic rcl CMake configuration requires an installed RMW at configure time. Select the already installed Zenoh package for that lookup. The library and test processes have no dependency on the Zenoh RMW.

```bash
source build/migration-guide/install/local_setup.bash
export CC=/usr/bin/clang-21
export CXX=/usr/bin/clang++-21
export RMW_IMPLEMENTATION=rmw_zenoh_cpp
colcon build --paths src/core/agnocast/src/agnocastlib src/rmw_agnocast --build-base build/standalone/obj --install-base build/standalone/install --executor sequential --cmake-args -DRMW_IMPLEMENTATION=rmw_zenoh_cpp -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 -DBUILD_TESTING=OFF
source build/standalone/install/local_setup.bash
cmake -S experiments/rmw-agnocast/standalone -B build/standalone/probe -DRMW_IMPLEMENTATION=rmw_zenoh_cpp -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15
cmake --build build/standalone/probe -j1
export RMW_IMPLEMENTATION=rmw_agnocast
export LD_PRELOAD=/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so
bash experiments/rmw-agnocast/standalone/check.sh build/standalone/probe/topic_check
```

Use the actual location of the Agnocast 2.4.1 hook visible in the userspace. The check inspects transitive ELF dependencies, unsets the historical topic and bridge-selection variables, and runs separate C++ reader and writer processes on a new topic in each trial. Both roles must print success in both ordinary and loaned operation. The writer checks the matched reader count before publishing. The reader verifies every byte of the dynamic string and both processes check their memory maps and shut down.

## Scope

Default rclcpp nodes run their parameter, logging and type-description endpoints. C++ typed topics use native message objects and support loans. C-layout applications convert through generated CDR callbacks into the canonical C++ object. Services use CDR request/reply frames on private native routes and a reply route per client GID. A local recorder reads CDR from a native subscription. The external bridge still needs its own implementation.

The separate Cyclone control used the same ROS domain and topic name. While native writer and reader completed delivery, the stock DDS echo received no sample and timed out. A stock Cyclone publisher then delivered a control string to a fresh stock echo with the same settings. The historical Autoware pipeline timings belong to the mixed implementation at 1a7d9b42. The [standalone workload rerun](workload-results/README.md) measures Path latency, checks Trajectory ownership, and records the initial three-LiDAR startup failure. The [Universe follow-up](workload-results/README.md#cyclone-free-universe-cloud-transport) runs both the crop and three-LiDAR graphs with CPU PointCloud2 transport.

For the reverse control, run a stock Cyclone publisher at 5 Hz and a stock echo on the same fresh topic and domain. In the standalone environment run `CHECK_DDS_ISOLATION=1 build/standalone/probe/topic_check reader "$topic"`. The stock echo received the control string while the native reader reported zero callbacks and zero matches over three seconds.

The standalone check also runs a batch reader against a three-message burst.
It checks ordering, publisher metadata, destination capacity and empty takes.
Each trial exercises the native QoS compatibility query for both reliability
directions and for an incompatible durability pairing.

The rosout check keeps its writer alive and attaches a C++ subscriber after both log messages were emitted. Expect `late rosout fresh=1 expired=0` and `queued rosout after expiry=0`. Allow about half a minute for the default ten-second lifespan checks.


## Lyrical API coverage

The installed reference is rmw_cyclonedds_cpp 4.1.5 at 74e8047e. The two ELF
libraries export the same 97 rmw_ symbols. Five rmw_agnocast entry points
always return unsupported. Cyclone 4.1.5 does the same for all five.
[Reference implementation](https://github.com/ros2/rmw_cyclonedds/blob/74e8047eb717953b31155323bfe137e81caa423e/rmw_cyclonedds_cpp/src/rmw_node.cpp)

| Area | Native behavior and scope |
|---|---|
| Services | Standard discovery, QoS queries, request IDs, blocking executor waits, response routing, and arrival callbacks. CDR encoding adds work to request/reply. |
| Graph | Nodes and enclaves, names/types by node, logical endpoint counts, topic and service endpoint details, and graph guard wakeups. Per-entity file locks and inotify track lifecycle. Peers must share effective UID, IPC namespace, domain and the graph directory in /dev/shm. Each logical service or client has one graph identity. |
| Topics and tools | Ordinary, loaned, batched and CDR operations, generated C and C++ message layouts, timestamps, sequence numbers and sample arrival callbacks. Fast CDR and generated serialization callbacks encode bytes. The test checks the absence of Cyclone, Fast DDS, and their RMW libraries. |
| Events | Publication/subscription matching, incompatible type and QoS on both sides, subscriber automatic liveliness changes, and offered/requested deadline misses for unkeyed topics. Events reflect discovered graph snapshots. Very short-lived intermediate states can be coalesced. |
| QoS and filters | Volatile or transient-local KeepLast with expiry. Scalar comparisons on supported numeric, Boolean and string fields, nested paths and parameter substitution. Unsupported compound expressions fail at configuration. Local network-flow queries return an empty array. |
| Subscription allocation | Fixed-size generated C++ types support allocation init/fini. Typed single and batch takes reuse a field-copy plan. Variable-size fields and C-layout conversion are outside this path. |
| Deferred by reference | Publisher allocation init/fini (two), DynamicData take with/without info (two), and dynamic serialization-support initialization (one). Generated typesupport CDR works independently of DynamicData. The vendor DynamicData provider is outside this experiment. |
| Remaining differences from Cyclone | Reliable wait-for-all-acked needs per-reader delivery acknowledgement. Kernel publish completion exposes no such receipt. Best-effort wait completes immediately. Publisher liveliness-lost still requires finite-lease tracking. Transport message-lost needs loss accounting. Native queue replacement at KeepLast depth is intentional. |
| Other open work | Other Buffer layouts, cross-host gateway, full Lyrical conformance and Autoware load/latency qualification of this revision. |

The standalone check exercises concurrent default-option clients, executor events,
serialized pub/sub, graph inspection and filters alongside the prior topic and
rosout checks. To run the stock ROS tool check in the same sourced, stripped
environment, install or build examples_rclcpp_minimal_publisher and make its
executable available. Python needs rclpy, std_msgs, rcl_interfaces and the
rosbag2 SQLite storage plugin.

```bash
talker_prefix=$(ros2 pkg prefix examples_rclcpp_minimal_publisher)
talker="$talker_prefix/lib/examples_rclcpp_minimal_publisher/publisher_member_function"
python3 experiments/rmw-agnocast/standalone/check-stock.py "$talker"
```

The check receives a stock talker message, calls its parameter service, records
the same topic, decodes the stored messages, and checks graph cleanup after
terminating the test's child talker. Rebuild the RMW once and restart both ends
when upgrading from the previous native sample layout.

The deadline/allocation probe runs from check.sh. A finite unkeyed topic
deadline starts at the first successful publication or admitted sample.
Each finite endpoint has a monotonic timer worker. Missed periods accumulate
while an executor sleeps. A compatible sample resets the timer, and loss of
the last compatible writer disarms the reader after graph discovery. The
writer must offer a period no longer than the reader requests. The receiver
checks that offer before admission to its RMW queue. Keyed finite deadlines
and finite service deadlines are currently rejected.

The v4 topic sample includes the offered deadline. Restart peers together on
this build. Public matched-count queries share an endpoint snapshot per node and apply
the offered/requested QoS on every call. The first query subscribes to local
graph directory events before taking the snapshot. Each later query checks
that queue synchronously. A peer joining, leaving, or closing its writable
lease on process death invalidates the snapshot. Queue overflow also triggers
a rescan. If the extra watch is unavailable, queries scan the leases directly. The allocation probe counts ordinary C++ operator new
calls on the taking thread. It does not measure allocations by other threads
or C allocator calls.
