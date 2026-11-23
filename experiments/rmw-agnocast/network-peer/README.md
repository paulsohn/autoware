# Record from an isolated Lyrical peer

The laptop runs the three-LiDAR graph and an explicit gateway in the colcon
pilot. A stock ROS container runs Cyclone DDS, rosbag2 and the message
packages. The peer has a private IPC namespace, a separate network namespace,
zero mounts and a 384 MiB limit. Bags are written inside the container and
copied out after rosbag closes. The host ROS processes use the existing
512 MiB runtime guard. The peer image remains available after the test.
Each invocation removes its own container and Docker network.

## Reproduce on the provisioned laptop

From the colcon worktree, use the default Docker Engine. The stock
`ros:lyrical-ros-base` image includes rosbag2 but lacks the Cyclone RMW.
The offline preparation script pins that image by digest and installs stock
Debian packages cached in the existing local migration image. It uses no
compiled Autoware or Agnocast artifacts. These commands assume the two
pinned images, the host pilot build, and the existing Lyrical root are
available locally.

```bash
sg docker -c 'bash experiments/rmw-agnocast/network-peer/prepare.sh'
sg docker -c 'python3 experiments/rmw-agnocast/network-peer/batch.py build/peer-test'
```

Use a fresh output directory for each batch. The runner creates a dedicated
internal Docker bridge, assigns a fixed peer address for the run, and writes
`host.xml` and `peer.xml` with reciprocal unicast discovery addresses. Host
participants also discover each other through the host bridge address. The
data path uses the bridge interface on the laptop and eth0 on the peer.
The runner records the image ID, ROS package versions, namespace IDs, bag
rows, local verification, frame timings and interface counters. Keep the
large SQLite bags and serialized reference clouds under the ignored build
directory.

## Operator commands for the same route

Source the pilot overlays on the laptop as in host.sh. Set ROS_DOMAIN_ID=84
on both sides. Give each side its own CYCLONEDDS_URI pointing at its generated
XML. With the three-LiDAR application running on the laptop, launch its export:

```bash
RMW_IMPLEMENTATION=rmw_agnocast \
LD_PRELOAD="$(ros2 pkg prefix agnocastlib)/lib/libagnocast_heaphook.so" \
AGNOCAST_RMW_HOST_CLOUD=1 AGNOCAST_BRIDGE_MODE=OFF \
AGNOCAST_NO_DISCOVERY_AGENT=1 AGNOCAST_BRIDGE_DEMAND=1 \
ros2 run agnocast_rmw_loan_probe route_bridge a2r cloud /output /dds/synchronized
```

Inside the stock peer, with its environment and XML as configured by batch.py:

```bash
source /opt/ros/lyrical/setup.bash
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
ros2 bag record --topics /dds/synchronized -s sqlite3 --max-cache-size 0 \
  --disable-keyboard-controls -o /bags/drive
```

A late recorder activates the export. Use source stamps to identify its actual
recorded interval. The sensor-data export is volatile and best effort. A
subscriber can appear in discovery while the link is down, and publications
during the outage can be missing from the bag.

For ingress the stock peer publishes geometry_msgs/msg/PoseArray on
/peer/poses. On the laptop, use the same gateway environment and run
`route_bridge r2a auto /peer/poses /local/poses` with demand mode unset.
Start the peer publisher before type discovery times out. The native reader
selects /local/poses with AGNOCAST_RMW_SUBSCRIBE_TOPICS and the heap hook.
The fixture uses an independent typed reader to check every pose across
48 messages with varying array sizes.

To qualify a physical peer, transfer the prepared image or install the same
packages there. Replace the Docker interface names and addresses in the two
XML files with the communicating network interfaces and routable addresses.
Repeat the payload verification and reconnection tests. The laptop's common
clock makes a one-way timestamp comparison possible here; on separate
machines that comparison also needs measured clock synchronization.

## Leave an idle peer available interactively

After the batch has finished, prepare one stock peer on a dedicated bridge:

```bash
sg docker -c 'python3 experiments/rmw-agnocast/network-peer/batch.py build/lyrical-peer-ready --prepare-only'
sg docker -c 'docker exec -it awpilot0-stock bash'
```

The preparation command prints both addresses. Source /opt/ros/lyrical/setup.bash
inside the peer. Its environment already selects Cyclone, domain 84 and its
own XML. In the laptop pilot runner, use
`CYCLONEDDS_URI=file:///ws/build/lyrical-peer-ready/host.xml` and
`ROS_DOMAIN_ID=84`. Use `ros2 topic echo --no-daemon` for CLI probes so a
daemon started with an earlier bridge configuration cannot affect discovery.
Copy out any bags before removing the peer. To retire this named setup:

```bash
sg docker -c 'docker stop awpilot0-stock && docker rm awpilot0-stock && docker network rm awpilot0'
```
