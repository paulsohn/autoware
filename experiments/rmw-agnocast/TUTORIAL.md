# Move a same-host ROS 2 project from Cyclone DDS to rmw_agnocast

Start with an ordinary ROS 2 Lyrical project that you normally run with Cyclone DDS. Build your application once against the middleware overlay, then select the RMW when you launch it. On this revision, every supported topic in that process uses native shared memory by default. Your node code, topic names, remappings, executor and launch descriptions can stay as they are.

Choose two processes on the same Linux host, running as the same user with the same ROS domain, IPC namespace, accessible /dev/agnocast and shared /dev/shm. Use matching builds on both ends. The worked example exchanges std_msgs/msg/String with ordinary rclcpp and then records it locally. Step 7 exports the same topic to a stock Cyclone recorder. A top-level Lyrical PointCloud2 uses the specialized host-cloud representation. For an enclosing message with Buffer fields, the RMW uses the generated CDR serializer to carry its contents through shared memory. This path supports ordinary typed subscribers and serialized readers, including a local recorder. Budget for encoding and decoding nested clouds.

## 1. Get the middleware sources and build tools

Use a fresh Bash shell, a sourced Lyrical installation and a new checkout. The laptop used Ubuntu 26.04 x86-64 Lyrical userspace with Clang 21 and GCC 15's standard library, running on the host kernel. A native Lyrical installation uses the same overlay steps. Run each command separately.

```bash
source /opt/ros/lyrical/setup.bash
git clone --branch experiment/rosidl-buffer-clean --single-branch https://github.com/paulsohn/autoware.git "$HOME/rmw-agnocast-pilot"
cd "$HOME/rmw-agnocast-pilot"
git rev-parse --short HEAD
cat src/core/agnocast/agnocast_kmod/version.txt
```

Save the commit ID alongside your project's dependencies. This walkthrough was checked against middleware revision `761e6029`. The kernel module version for this recipe is **2.4.1**. Install the build tools on the development host if missing. These instructions assume your existing ROS apt repository and rosdep initialization.

```bash
sudo apt install clang-21 g++-15 curl python3-colcon-common-extensions python3-rosdep ros-lyrical-rmw-zenoh-cpp ros-lyrical-rosbag2-storage-default-plugins
rosdep update
rosdep install --rosdistro lyrical --ignore-src -t build -t buildtool -t build_export -t buildtool_export --from-paths \
  src/rclcpp/rclcpp \
  src/core/agnocast/src/agnocast_cie_config_msgs \
  src/core/agnocast/src/agnocast_cie_thread_configurator \
  src/core/agnocast/src/agnocastlib \
  src/rmw_agnocast
```

Stage the two ROS package schemas locally so message generation can resolve them during the build. The catalog maps the schema URI used in package.xml to these files.

```bash
mkdir -p build/migration-guide/schema
curl -fL https://raw.githubusercontent.com/ros-infrastructure/rep/master/xsd/package_format3.xsd -o build/migration-guide/schema/package_format3.xsd
curl -fL https://raw.githubusercontent.com/ros-infrastructure/rep/master/xsd/package_common.xsd -o build/migration-guide/schema/package_common.xsd
echo '<catalog xmlns="urn:oasis:names:tc:entity:xmlns:xml:catalog"><uri name="http://download.ros.org/schema/package_format3.xsd" uri="package_format3.xsd"/></catalog>' > build/migration-guide/schema/catalog.xml
export XML_CATALOG_FILES="$PWD/build/migration-guide/schema/catalog.xml"
```

## 2. Build the five-package middleware overlay

The checkout supplies rclcpp, two CIE library prerequisites, agnocastlib and rmw_agnocast. The generated CDR codecs are used for serialized ROS traffic. Generic ROS CMake packages also look for an installed RMW while configuring. This recipe selects Zenoh for that lookup. The finished native processes do not need a Zenoh router or DDS participant.

```bash
export RMW_IMPLEMENTATION=rmw_zenoh_cpp
export CC=/usr/bin/clang-21
export CXX=/usr/bin/clang++-21
export CMAKE_BUILD_PARALLEL_LEVEL=1
colcon --log-base build/migration-guide/log build \
  --build-base build/migration-guide/obj \
  --install-base build/migration-guide/install \
  --executor sequential \
  --paths src/rclcpp/rclcpp \
          src/core/agnocast/src/agnocast_cie_config_msgs \
          src/core/agnocast/src/agnocast_cie_thread_configurator \
          src/core/agnocast/src/agnocastlib \
          src/rmw_agnocast \
  --cmake-args -DRMW_IMPLEMENTATION=rmw_zenoh_cpp -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
               -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 \
               -DCMAKE_POLICY_VERSION_MINIMUM=3.5
source build/migration-guide/install/local_setup.bash
ros2 pkg prefix rclcpp
ros2 pkg prefix rmw_agnocast
ros2 pkg prefix agnocastlib
ldd "$(ros2 pkg prefix rmw_agnocast)/lib/librmw_agnocast.so"
```

Expect five packages finished and all three prefixes under build/migration-guide/install. Check ldd for missing libraries. The native library has no dependency on librmw_cyclonedds_cpp or libddsc.

## 3. Use the released hook and kernel module

Use the upstream **2.4.1** runtime for native processes. Relative to the imported Agnocast source, the kernel module is unchanged. The only addition under the heap hook is a Cargo.lock for reproducing source builds. The released hook works with this pilot.

On the **native host**, check what is already available:

```bash
cat /sys/module/agnocast/version
ls -l /dev/agnocast
test -r /dev/agnocast
test -w /dev/agnocast
dpkg-query -L agnocast-heaphook-v2.4.1
```

If the running module reports 2.4.1, the device checks succeed, and you have the matching hook, skip installation. Otherwise, on an Ubuntu host with the upstream packages available, check the candidates and install the missing components:

```bash
apt-cache policy agnocast-kmod-v2.4.1 agnocast-heaphook-v2.4.1
sudo apt install "linux-headers-$(uname -r)" agnocast-kmod-v2.4.1 agnocast-heaphook-v2.4.1
sudo modprobe agnocast
cat /sys/module/agnocast/version
ls -l /dev/agnocast
dpkg-query -L agnocast-heaphook-v2.4.1
```

Use a package source supporting your host release. The tested packages came from the upstream `t4-system-software/agnocast` Noble PPA on the host. A candidate for Ubuntu 26.04 was not verified. DKMS may compile the released module for the host kernel during installation. Coordinate replacement of any different loaded version before proceeding.

Find the **absolute path** of libagnocast_heaphook.so in the package listing. The tested package installs it at `/opt/ros/jazzy/lib/libagnocast_heaphook.so`; use your actual path in step 5. Continue to source Lyrical throughout. For containerized nodes, expose the host device and mount or copy the matching .so into the userspace at a known path.

## 4. Build your ordinary ROS project once

In a fresh shell, source Lyrical and then the new middleware overlay **before the first application build**. This gives typed subscriptions the overlay's rclcpp headers, including support for callbacks that retain loans. Keep your project's existing packages, source and launch files. There is no Agnocast-specific application build flag to set.

From your own project's clean workspace root, run your usual colcon build. For example:

```bash
source /opt/ros/lyrical/setup.bash
source "$HOME/rmw-agnocast-pilot/build/migration-guide/install/local_setup.bash"
export CMAKE_BUILD_PARALLEL_LEVEL=1
colcon build --executor sequential
```

**To run the concrete example instead**, use the existing Lyrical ROS examples as your application workspace:

```bash
git clone --branch lyrical --single-branch https://github.com/ros2/examples.git "$HOME/ros-examples"
cd "$HOME/ros-examples"
git checkout cc721964a79192e46db0111c0ba9f6a831fc9395
source /opt/ros/lyrical/setup.bash
source "$HOME/rmw-agnocast-pilot/build/migration-guide/install/local_setup.bash"
export CMAKE_BUILD_PARALLEL_LEVEL=1
colcon build --executor sequential \
  --base-paths rclcpp/topics \
  --packages-select examples_rclcpp_minimal_publisher examples_rclcpp_minimal_subscriber \
  --cmake-args -DBUILD_TESTING=OFF
```

## 5. Run the same nodes with shared memory

In **each terminal**, source the middleware and application overlays before setting the hook. Substitute your own application install if you built your project. Keep the same ROS_DOMAIN_ID in every terminal. Set LD_PRELOAD to the absolute hook path you verified in step 3.

```bash
source /opt/ros/lyrical/setup.bash
source "$HOME/rmw-agnocast-pilot/build/migration-guide/install/local_setup.bash"
source "$HOME/ros-examples/install/local_setup.bash"
export RMW_IMPLEMENTATION=rmw_agnocast
export LD_PRELOAD=/opt/ros/jazzy/lib/libagnocast_heaphook.so
```

In terminal A, start the listener.

```bash
ros2 run examples_rclcpp_minimal_subscriber subscriber_member_function
```

In terminal B, start the talker.

```bash
ros2 run examples_rclcpp_minimal_publisher publisher_member_function
```

Expect successive `I heard: 'Hello, world! …'` lines in terminal A. For your own project, launch the nodes from the configured environment. Every participating process inherits the middleware choice. Topic selection lists, a handoff file, native ROS CLI shims and bridge-mode flags are unnecessary. The old handoff file was benchmark instrumentation. CYCLONEDDS_URI applies only to processes you launch with Cyclone.

## 6. Record the topic on the same host

Leave the talker running. Open terminal C with the step 5 environment. The stock recorder joins the native graph through the same device and hook and writes serialized CDR to disk. Choose a fresh bag directory.

```bash
ros2 bag record --storage sqlite3 -o migration-topic-bag --topics /topic
```

After several publications press Ctrl-C, then inspect the bag.

```bash
ros2 bag info migration-topic-bag
```

Look for /topic, std_msgs/msg/String, and a positive message count. Local delivery and disk recording have different costs. A C++ subscription can take a native loan. C-layout consumers convert the message and recording requires serialization.

## 7. Record the native talker from a Cyclone peer

Keep the talker from step 5 running. On the host with the Agnocast device, open a fresh gateway terminal. Use the same ROS domain as the talker and your usual working Cyclone network configuration for the peer. Install the message package on both sides. The gateway loads the installed type by name.

```bash
source /opt/ros/lyrical/setup.bash
source "$HOME/rmw-agnocast-pilot/build/migration-guide/install/local_setup.bash"
cd "$HOME/rmw-agnocast-pilot"
unset LD_PRELOAD
colcon build --paths src/agnocast_cyclone_gateway --executor sequential --build-base build/manual-gateway/obj --install-base build/manual-gateway/install
source build/manual-gateway/install/local_setup.bash
ros2 run agnocast_cyclone_gateway topic_gateway export std_msgs/msg/String /topic /from_robot/topic --heaphook /opt/ros/jazzy/lib/libagnocast_heaphook.so
```

Use the absolute hook path from step 3. The command starts a native reader and a Cyclone writer in separate processes. The gateway advertises the DDS topic immediately. Watch for both endpoint readiness lines. The native subscription arms when a compatible DDS reader appears.

On the peer, open a stock Lyrical terminal with your working Cyclone discovery configuration and matching domain. The peer needs only Cyclone, rosbag2 and the message package.

```bash
source /opt/ros/lyrical/setup.bash
unset LD_PRELOAD
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
ros2 bag record --topics /from_robot/topic -o robot-topic-bag
```

Wait for `source armed` in the gateway terminal. After several messages, press Ctrl+C in the recorder terminal, then inspect the bag.

```bash
ros2 bag info robot-topic-bag
```

Expect a positive count on /from_robot/topic. The gateway releases its native subscription after the last match disappears. Recording begins after discovery and activation. Stopping the gateway creates a gap in a volatile stream. Local subscribers continue receiving.

For the reverse direction, open another gateway terminal with both overlays sourced. This example imports a distinct peer topic into the local graph.

```bash
ros2 run agnocast_cyclone_gateway topic_gateway import std_msgs/msg/String /from_peer/topic /local/peer_topic --heaphook /opt/ros/jazzy/lib/libagnocast_heaphook.so
```

Run a listener on /local/peer_topic with the native setup from step 5. Publish on /from_peer/topic from the Cyclone peer. An import subscribes immediately. Each command owns one direction until Ctrl+C. Both routes use volatile history, depth 10, best-effort input and reliable output. Use `--input-reliability reliable` for a reliable source when the bridge must request retransmission, or set `--output-reliability best_effort` for compatible readers. Use `--depth` for queue depth. Each serialized sample must fit within 64 MiB. This release has laptop-to-isolated-container evidence in both directions. Two physical hosts still need qualification.

## 8. Decide which processes to migrate together

| Project requirement | Behavior in this revision |
|---|---|
| A local node uses services or parameters | Native request/reply and graph discovery support ordinary ROS nodes. The stock check exercised get_parameters across processes. |
| A stock Cyclone process needs the same topic | The transports are isolated even with identical names and ROS_DOMAIN_ID. Use the explicit topic route in step 7 or migrate the communicating ends together. |
| A recorder or node lives on another host | Cross-host delivery requires a separate gateway. Use the gateway in step 7 with a reachable peer and compatible QoS. |
| A cluster subscription reports “unsupported Buffer wire type” | The older standalone library rejected enclosing types such as DetectedObjectsWithFeature when creating the endpoint. Check the sourced middleware prefix in both processes. Use the revision with generated CDR support for enclosing Buffer fields. |
| Two local nodes start but no samples arrive | Check the resolved topic, compatible QoS, domain, effective user, IPC namespace and shared /dev/shm. Verify both can access the device and resolve the same middleware overlay and hook. Start the worked String pair to isolate application-specific problems. |

To return to Cyclone, stop the migrated processes. Open fresh terminals, source the same Lyrical, middleware and application overlays, select your usual Cyclone configuration and restart the same binaries.

```bash
unset LD_PRELOAD
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
```
