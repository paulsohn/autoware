# Use rmw_agnocast with ROS 2 Lyrical

Start with a ROS topic whose publisher and subscriber run on the same computer.
Select that topic for local shared-memory transport. When another computer
needs the data, start a bridge on the computer that owns the native topic.
**The pilot supports manual topic bridging between rmw_agnocast and Cyclone DDS.**

This walkthrough uses two roles. **A** runs the Lyrical colcon pilot.
**B** is an ordinary Lyrical Cyclone computer with the message packages and
rosbag2. On the provisioned laptop, the prepared container plays B with its
own network and IPC namespaces. You will publish a numbered pose on B, import
it into a native topic on A, export it back under a new name, and record it on B.
Using three topic names makes each crossing visible.

```text
B: /demo/in   ── Cyclone DDS ──► A: import ──► /demo/local (shared memory)
B: /demo/out  ◄─ Cyclone DDS ─── A: export ◄──────────┘
```

Choose the connection you need:

| Publisher → subscriber | What to start |
|---|---|
| Local native → local native on A | Select the topic on both processes. They share A’s local pool. |
| Native on A → stock Cyclone on B | Start one export on A. Run the usual ROS subscriber or recorder on B. |
| Stock Cyclone on B → native on A | Start one import on A. Run the usual ROS publisher on B. |
| Ordinary DDS → ordinary DDS | Use normal Cyclone discovery and QoS. Leave this topic out of the native selections. |

## 1. Open the two environments

Start on branch `experiment/rosidl-buffer-clean` with the pilot built. A needs
the branch's rclcpp overlay, rmw_agnocast, gateway, matching heap hook and a
working /dev/agnocast accessible to the user. B needs rmw_cyclonedds_cpp,
geometry_msgs and rosbag2 with SQLite. For a laptop without the prepared B,
follow **Leave an idle peer available interactively** in the
[peer setup](network-peer/README.md#leave-an-idle-peer-available-interactively).
The build and peer recipes assume an already provisioned Lyrical environment.
[Pilot build steps and prerequisites](../buffer/JOURNAL.md#2026-11-23--reproduce-the-configured-colcon-pilot-from-fresh-build-outputs)

**Terminal A** — from the colcon worktree on the provisioned laptop, enter
the Lyrical userspace. The runner mounts this checkout at /ws.

```bash
export LYRICAL_ROOT=$(cat build/agnocast-lyrical/universe-root)
BUFFER_RUNTIME_ONLY=1 experiments/buffer/run-lyrical.sh bash --noprofile --norc
```

In that shell, run:

```bash
cd /ws
source build/agnocast-lyrical/install/local_setup.bash
source build/rmw-agnocast/install/local_setup.bash
source build/rmw-agnocast/load-install/local_setup.bash
export ROS_DOMAIN_ID=84
export CYCLONEDDS_URI=file:///ws/build/lyrical-peer-ready/host.xml
export ENABLE_AGNOCAST=0
hook="$(ros2 pkg prefix agnocastlib)/lib/libagnocast_heaphook.so"
bridge="$(ros2 pkg prefix agnocast_rmw_loan_probe)/lib/agnocast_rmw_loan_probe/route_bridge"
test -r /dev/agnocast && test -w /dev/agnocast && test -r "$hook" && test -x "$bridge" && ros2 pkg prefix rmw_agnocast
native=(env RMW_IMPLEMENTATION=rmw_agnocast LD_PRELOAD="$hook" AGNOCAST_BRIDGE_MODE=OFF AGNOCAST_NO_DISCOVERY_AGENT=1)
```

The check should print the installed rmw_agnocast prefix. The Bash array
applies the settings only to the processes you start with it.
AGNOCAST_BRIDGE_MODE=OFF disables the upstream automatic bridging mechanism.
You will launch the pilot's directed gateway yourself.

**Terminals B1 and B2** — open two more terminals. In each one, enter the
same prepared peer:

```bash
sg docker -c 'docker exec -it awpilot0-stock bash'
```

In each B shell, run:

```bash
source /opt/ros/lyrical/setup.bash
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp ROS_DOMAIN_ID=84
export CYCLONEDDS_URI=file:///tmp/peer.xml
```

On **two physical computers**, use a Lyrical shell and the installed pilot
prefixes on A, and a normal Lyrical shell on B. Give each its own Cyclone XML.
Use the generated host.xml and peer.xml as templates. Change each
NetworkInterface name to the local network interface and the Peers addresses
to the two routable IPs. Set CYCLONEDDS_URI to the local file on each side,
use the same ROS_DOMAIN_ID, and allow DDS UDP traffic between them. B needs
no native device, preload, or shared memory from A. The isolated-container
topology is verified; the physical two-computer instructions await field validation.

## 2. Start two manual routes on A

In **A**, start one process per direction. Keep the shell open.

```bash
"${native[@]}" "$bridge" r2a geometry_msgs/msg/PoseArray /demo/in /demo/local &
import_pid=$!
"${native[@]}" AGNOCAST_BRIDGE_DEMAND=1 "$bridge" a2r geometry_msgs/msg/PoseArray /demo/local /demo/out &
export_pid=$!
```

Read each command as **direction, type, input topic, output topic**.
`r2a` imports ordinary DDS data into a local native topic. `a2r` exports
a local native topic to DDS. Each route pins its type, so you can start it
before its publisher. The export attaches its native reader when a DDS
reader matches. For one-way logging of an existing native topic, start only
the export. Keep one gateway per configured direction.

## 3. Publish, receive, and record on B

In **B1**, start a publisher and leave it running:

```bash
ros2 topic pub --rate 2 /demo/in geometry_msgs/msg/PoseArray \
  '{header: {frame_id: tutorial_42}, poses: [{position: {x: 42.0}, orientation: {w: 1.0}}]}'
```

In **B2**, read one exported message:

```bash
ros2 topic echo --no-daemon --once /demo/out geometry_msgs/msg/PoseArray
```

Look for `frame_id: tutorial_42` and `x: 42.0`. B1 published only on
/demo/in. Receiving the value on /demo/out checks delivery through both
gateways. A topic name in the discovery graph alone does not check delivery.

Still in **B2**, record the exported topic:

```bash
bag="/tmp/tutorial-poses-$(date +%s)"
ros2 bag record --topics /demo/out -s sqlite3 -o "$bag"
```

Wait for the subscription message and a few seconds of data. Press Ctrl-C,
then run `ros2 bag info "$bag"`. Look for /demo/out, geometry_msgs/msg/PoseArray
and a positive message count. Press Ctrl-C in **B1** to stop the publisher.
In **A**, stop the two gateway processes:

```bash
kill -INT "$export_pid" "$import_pid"
wait "$export_pid" "$import_pid"
```

## 4. Use your own application topics

With **rmw_cyclonedds_cpp**, an ordinary publisher and subscriber use DDS
according to domain, discovery and QoS. With **rmw_agnocast**, also choose
which endpoints should carry their data in local shared memory. Unselected
topics use Cyclone. Setting RMW_IMPLEMENTATION alone starts no gateway.

For example, suppose a C++ driver publishes /lidar/cloud and a filter reads
it. Build wrapper-based Autoware nodes with ENABLE_AGNOCAST=0 and run their
ordinary rclcpp path. In A's configured environment use the following pattern,
substituting your installed package and executable names. Start each command
in a separately configured A shell:

```bash
"${native[@]}" AGNOCAST_RMW_HOST_CLOUD=1 AGNOCAST_RMW_PUBLISH_TOPICS=/lidar/cloud \
  ros2 run YOUR_DRIVER_PACKAGE YOUR_DRIVER_EXECUTABLE
"${native[@]}" AGNOCAST_RMW_HOST_CLOUD=1 AGNOCAST_RMW_SUBSCRIBE_TOPICS=/lidar/cloud \
  ros2 run YOUR_FILTER_PACKAGE YOUR_FILTER_EXECUTABLE
```

Set both variables on a process that reads and writes native topics. Use
fully resolved names after remapping and a colon between multiple names,
for example `/lidar/left:/lidar/right`. Use matching message layouts on the
participants. The host-cloud setting selects the tested CPU PointCloud2
layout; an ordinary CPU-vector consumer can still copy bytes on receive.
Native endpoints in this pilot require C++ introspection and volatile
KeepLast QoS with positive depth.

To send that cloud to another computer's logger, run this on **A** while
the driver is running:

```bash
"${native[@]}" AGNOCAST_RMW_HOST_CLOUD=1 AGNOCAST_BRIDGE_DEMAND=1 \
  "$bridge" a2r cloud /lidar/cloud /dds/lidar/cloud
```

On **B**, run `ros2 bag record --topics /dds/lidar/cloud`. The cloud route
uses sensor-data QoS. For other consumers, request compatible reliability
and durability. Each route sends ordinary ROS message content across DDS.
Native loans, host pointers and GPU handles stay local.

For a different topic, pass its full installed ROS type as in step 2, or use
`auto` with an already running input publisher of one unambiguous type.
The bridge has a specialized PointCloud2 path and runtime serialized
forwarding for tested types such as PoseArray and Image. Install the relevant
type support on the gateway and on tools that decode the message. Start with
the tested layout for Buffer-bearing types. This pilot covers directed topic
data; services, actions and other network middleware bridges are outside
the verified recipe. For two native islands, the intended topology is an
export on the source host and a separate import on the destination host,
joined by one DDS topic. For example, export the native cloud to /dds/lidar/cloud
with the command above. On the second native host, repeat A's setup with its
own paths and network profile, then start:

```bash
"${native[@]}" AGNOCAST_RMW_HOST_CLOUD=1 \
  "$bridge" r2a cloud /dds/lidar/cloud /remote/lidar/cloud
```

Select /remote/lidar/cloud on that host's native subscriber. The two hosts
can use independent shared-memory pools. This final topology is a deployment
example; the verified round trip in steps 1–3 uses one native host.

## If the reader sees no samples

1. Check that the input publisher is sending and that the gateway is still
   running. Check resolved input and output names against the manual command.
2. Inspect the DDS output with `ros2 topic info --verbose --no-daemon /demo/out`.
   Check the domain, peer addresses, type support and QoS. On A, a demand
   export reports `matches=1` when its first DDS reader attaches.
3. Read an actual payload or check that bag counts advance. A volatile
   best-effort export can lose samples during an outage. In the tested
   laptop setup an interface going down and back up recovered automatically.
   Replacing the Docker interface could leave the old recorder unmatched;
   cleanly close it and start a fresh bag segment after connectivity returns.

[Measured bridge recovery](network-peer/results/match-status.md) · [Tested native message and loan behavior](README.md#pilot-support-by-application-interface)
