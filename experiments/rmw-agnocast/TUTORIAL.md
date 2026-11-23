# Migrate one topic in an existing Cyclone DDS project to rmw_agnocast

Start with a working **ROS 2 Lyrical C++ project using rmw_cyclonedds_cpp on Linux**. Pick one publisher and one subscriber on the same host. You will build the pilot alongside your project, run the same two nodes with one topic selected for shared memory, and check that the subscriber took native samples. Add a bridge only if an ordinary DDS participant also needs that topic.

Keep your current ROS_DOMAIN_ID, CYCLONEDDS_URI, message definitions, remappings and DDS discovery setup. The worked example uses the existing ROS C++ minimal publisher and subscriber, which exchange std_msgs/msg/String on /topic. Substitute your own workspace, executables and **fully resolved** topic name where indicated. Run each command individually in Bash. Check each result before continuing.

## 1. Record a baseline with your current launch

Start your project as usual with Cyclone. In an ordinary project terminal run:

```bash
ros2 topic info --verbose /topic
ros2 topic echo --once /topic std_msgs/msg/String
```

Record the resolved name, type, publisher and subscriber nodes, offered/requested QoS, and an example payload. For the example the payload contains `Hello, world!` and an increasing counter. Choose a topic using volatile durability and KeepLast history with positive depth; those are the pilot's supported native settings. Stop the existing nodes before starting the migrated pair. A second DDS publisher on the same name makes the transport check ambiguous.

## 2. Build the middleware overlay from source

Open a **fresh build shell** with your Lyrical distribution underlay. Keep the pilot in a separate checkout from your application. The commands below target Ubuntu 26.04 x86-64, Clang 21 and GCC 15's standard library. Start without LD_PRELOAD or an older project overlay in this shell. The checkout includes the matching source for patched rclcpp, the RMW, its local transport and the manual gateway. You do not need a previous Autoware install, a pilot build tree, Docker or Bazel.

```bash
source /opt/ros/lyrical/setup.bash
git clone --branch experiment/rosidl-buffer-clean --single-branch https://github.com/paulsohn/autoware.git "$HOME/rmw-agnocast-pilot"
cd "$HOME/rmw-agnocast-pilot"
git rev-parse --short HEAD
cat src/core/agnocast/agnocast_kmod/version.txt
```

Save the commit ID alongside your project's dependencies. The kernel module version for this recipe is **2.4.1**. Install the build tools on the development host if missing. These instructions assume your existing ROS apt repository and rosdep initialization.

```bash
sudo apt install clang-21 g++-15 cargo rustc curl python3-colcon-common-extensions python3-rosdep ros-lyrical-rmw-cyclonedds-cpp
rosdep update
rosdep install --rosdistro lyrical --ignore-src -t build -t buildtool -t build_export -t buildtool_export --from-paths \
  src/rclcpp/rclcpp \
  src/core/agnocast/src/agnocast_cie_config_msgs \
  src/core/agnocast/src/agnocast_cie_thread_configurator \
  src/core/agnocast/src/agnocastlib \
  src/rmw_agnocast \
  src/core/autoware_msgs/autoware_common_msgs \
  src/core/autoware_msgs/autoware_planning_msgs \
  src/agnocast_rmw_loan_probe
```

Stage the two ROS package schemas locally so message generation can resolve them during the build. The catalog maps the schema URI used in package.xml to these files.

```bash
mkdir -p build/migration-guide/schema
curl -fL https://raw.githubusercontent.com/ros-infrastructure/rep/master/xsd/package_format3.xsd -o build/migration-guide/schema/package_format3.xsd
curl -fL https://raw.githubusercontent.com/ros-infrastructure/rep/master/xsd/package_common.xsd -o build/migration-guide/schema/package_common.xsd
echo '<catalog xmlns="urn:oasis:names:tc:entity:xmlns:xml:catalog"><uri name="http://download.ros.org/schema/package_format3.xsd" uri="package_format3.xsd"/></catalog>' > build/migration-guide/schema/catalog.xml
export XML_CATALOG_FILES="$PWD/build/migration-guide/schema/catalog.xml"
```

Build the eight packages. Restricting colcon to these paths avoids building Autoware Universe. The two CIE packages are library build dependencies; you can keep your current ROS executor. The two Autoware message packages satisfy the package that currently ships the gateway executable, even for projects that use only standard ROS messages.

```bash
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
          src/core/autoware_msgs/autoware_common_msgs \
          src/core/autoware_msgs/autoware_planning_msgs \
          src/agnocast_rmw_loan_probe \
  --cmake-args -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
               '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG' \
               -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 \
               -DCMAKE_POLICY_VERSION_MINIMUM=3.5
source build/migration-guide/install/local_setup.bash
ros2 pkg prefix rclcpp
ros2 pkg prefix rmw_agnocast
ros2 pkg executables agnocast_rmw_loan_probe
```

Expect eight packages finished. Both prefixes should be under this checkout's build/migration-guide/install. Look for `route_bridge` in the executable list. Build and install the allocator hook from the **same checkout**:

```bash
export CARGO_BUILD_JOBS=1
export CARGO_TARGET_DIR="$PWD/build/migration-guide/cargo"
cargo build --locked --release --manifest-path src/core/agnocast/agnocast_heaphook/Cargo.toml
install -m 755 "$CARGO_TARGET_DIR/release/libagnocast_heaphook.so" "$(ros2 pkg prefix agnocastlib)/lib/libagnocast_heaphook.so"
ls -l "$(ros2 pkg prefix agnocastlib)/lib/libagnocast_heaphook.so"
ldd "$(ros2 pkg prefix rmw_agnocast)/lib/librmw_agnocast.so"
```

Check that ldd resolves librmw_cyclonedds_cpp and the local transport library and reports no missing libraries. The preload is a **runtime** setting for native processes. Leave it unset during builds.

## 3. Prepare each host that will run native nodes

Check the running host kernel, including when your ROS userspace lives in a container. The container must also have access to the host device:

```bash
cat /sys/module/agnocast/version
ls -l /dev/agnocast
test -r /dev/agnocast
test -w /dev/agnocast
```

If the version is 2.4.1 and both access checks succeed, proceed to step 4. If the matching module is installed but unloaded, run `sudo modprobe agnocast` and repeat the checks. If you need a new installation on Ubuntu, the checkout includes the DKMS packaging. On the **host**, from the checkout, build the package and install it for the running kernel:

```bash
sudo apt install dkms dh-dkms debhelper dpkg-dev fakeroot rsync "linux-headers-$(uname -r)"
cd "$HOME/rmw-agnocast-pilot/src/core/agnocast/agnocast_kmod"
dpkg-buildpackage -b -us -uc
sudo apt install ../agnocast-kmod-v2.4.1_2.4.1-1_all.deb
dkms status
sudo modprobe agnocast
cat /sys/module/agnocast/version
ls -l /dev/agnocast
```

Expect DKMS to report an installed module for the running kernel. Resolve a DKMS build, Secure Boot signing, or device-access error before starting the nodes. Coordinate replacement of an older loaded module with other users of the host. A stock remote Cyclone subscriber or recorder skips this entire step.

## 4. Rebuild your application against the overlay

In a clean shell, source Lyrical, then the new overlay, then build the same packages you use today into a **separate install directory**. Your existing Cyclone build serves as the rollback. Ordinary typed rclcpp publishers and subscriptions keep their source API. Rebuilding picks up the branch's headers, which matters for subscription callbacks that retain loaned data.

In your own project, change to its workspace root and run your normal dependency selection with these output directories. For an Autoware package that uses autoware_agnocast_wrapper, set `export ENABLE_AGNOCAST=0` **before building and running** so it takes its ordinary rclcpp path.

```bash
source /opt/ros/lyrical/setup.bash
source "$HOME/rmw-agnocast-pilot/build/migration-guide/install/local_setup.bash"
export CMAKE_BUILD_PARALLEL_LEVEL=1
colcon --log-base log-rmw-agnocast build --executor sequential \
  --build-base build-rmw-agnocast --install-base install-rmw-agnocast
```

**To run the concrete example instead**, use the existing Lyrical ROS examples as your application workspace:

```bash
git clone --branch lyrical --single-branch https://github.com/ros2/examples.git "$HOME/ros-examples"
cd "$HOME/ros-examples"
git checkout cc721964a79192e46db0111c0ba9f6a831fc9395
source /opt/ros/lyrical/setup.bash
source "$HOME/rmw-agnocast-pilot/build/migration-guide/install/local_setup.bash"
export CMAKE_BUILD_PARALLEL_LEVEL=1
colcon --log-base log-rmw-agnocast build --executor sequential \
  --base-paths rclcpp/topics \
  --packages-select examples_rclcpp_minimal_publisher examples_rclcpp_minimal_subscriber \
  --build-base build-rmw-agnocast --install-base install-rmw-agnocast \
  --cmake-args -DBUILD_TESTING=OFF
```

## 5. Migrate the publisher and subscriber together

Open two application terminals on the same host. In **each**, source these in order. Replace the last path with your own application install if you used your project. Preserve the domain and Cyclone configuration from step 1.

```bash
source /opt/ros/lyrical/setup.bash
source "$HOME/rmw-agnocast-pilot/build/migration-guide/install/local_setup.bash"
source "$HOME/ros-examples/install-rmw-agnocast/local_setup.bash"
ros2 pkg prefix rclcpp
export RMW_IMPLEMENTATION=rmw_agnocast
export AGNOCAST_BRIDGE_MODE=OFF
export AGNOCAST_NO_DISCOVERY_AGENT=1
export LD_PRELOAD="$(ros2 pkg prefix agnocastlib)/lib/libagnocast_heaphook.so"
```

Check that rclcpp still resolves to the pilot prefix. These runtime settings apply to processes started from the configured terminals. The pilot uses its own directed bridge; the upstream bridge and discovery agent can stay off.

In the **subscriber terminal**, select the resolved input name. The optional trace in this first run gives you evidence of native takes when the node shuts down:

```bash
export AGNOCAST_RMW_SUBSCRIBE_TOPICS=/topic
export AGNOCAST_RMW_HANDOFF_FILE="$HOME/migration-topic-takes.txt"
ros2 run examples_rclcpp_minimal_subscriber subscriber_member_function
```

In the **publisher terminal**, select the same name for output:

```bash
export AGNOCAST_RMW_PUBLISH_TOPICS=/topic
ros2 run examples_rclcpp_minimal_publisher publisher_member_function
```

Watch for successive `I heard: 'Hello, world! …'` lines. Stop the subscriber with Ctrl-C after several callbacks. In that terminal run:

```bash
wc -l "$HOME/migration-topic-takes.txt"
head -n 1 "$HOME/migration-topic-takes.txt"
unset AGNOCAST_RMW_HANDOFF_FILE
```

On the default threaded reader path, a positive row count records native loan handoffs; the tenth column is the take-completion timestamp. Leave the publisher running for the export exercise below. For your application, restart the subscriber without tracing once you have verified the edge.

**How do I keep my existing launch file?** If one publisher process and one subscriber process both use the names you selected, launch them from a configured shell with both selection variables set. For a larger graph, scope each selection to the process that needs it. For example, add this keyword argument to the existing publisher's Python launch `Node(...)` action after setting the common runtime environment in the launching shell:

```python
additional_env={"AGNOCAST_RMW_PUBLISH_TOPICS": "/topic"},
```

On the subscriber's existing action use:

```python
additional_env={"AGNOCAST_RMW_SUBSCRIBE_TOPICS": "/topic"},
```

A node that reads and writes native topics gets both selection variables. Use a colon between resolved names, for example `/lidar/left:/lidar/right`. Composed nodes in one process share the process environment. Endpoints you leave unselected continue to use Cyclone. Both native endpoints must agree on the topic name, message representation and compatible QoS. For the tested CPU PointCloud2 layout, also set `AGNOCAST_RMW_HOST_CLOUD=1` in the driver, filter and gateway processes. Selecting the RMW does not guarantee that every application-level copy disappears.

## 6. Keep an existing DDS subscriber or logger working

Once a publisher uses the native path, a stock DDS peer may still **discover** the topic without receiving its payload. **The only supported bridge in this pilot is between rmw_agnocast and Cyclone DDS.** It runs manually, one process per configured direction, on the host that owns the native endpoints. The network carries ROS message content using DDS. No shared-memory pool or device is shared between hosts.

**Export the running example.** Open a third terminal on the publisher's host. Source the same Lyrical and middleware overlay. Keep your existing DDS domain and peer configuration.

```bash
source /opt/ros/lyrical/setup.bash
source "$HOME/rmw-agnocast-pilot/build/migration-guide/install/local_setup.bash"
export RMW_IMPLEMENTATION=rmw_agnocast
export AGNOCAST_BRIDGE_MODE=OFF
export AGNOCAST_NO_DISCOVERY_AGENT=1
export LD_PRELOAD="$(ros2 pkg prefix agnocastlib)/lib/libagnocast_heaphook.so"
export AGNOCAST_BRIDGE_DEMAND=1
ros2 run agnocast_rmw_loan_probe route_bridge a2r std_msgs/msg/String /topic /dds/topic
```

Read the arguments as **direction, type, input, output**. The gateway selects its own native endpoint. Demand mode attaches the local reader when a DDS subscriber matches. On your normal **remote Cyclone terminal**, use your existing ROS setup:

```bash
ros2 topic echo --no-daemon --once /dds/topic std_msgs/msg/String
ros2 bag record --topics /dds/topic -o migration-topic-bag
```

Wait a few seconds, press Ctrl-C, and run `ros2 bag info migration-topic-bag`. Look for a positive message count. Choose a fresh bag directory for each run. Stock DDS computers only need network connectivity, compatible QoS, and the ROS interfaces used by their tools. For a real LiDAR export use `a2r cloud /lidar/cloud /dds/lidar/cloud` with the host-cloud setting above. The specialized cloud route uses sensor-data QoS.

For a custom ROS message, source the workspace that installs its generated type support in the gateway shell before setting LD_PRELOAD. Source the corresponding interfaces on the remote computer so its tools can decode the topic. The gateway package itself does not need a rebuild for each installed generic type.

**Import a DDS topic instead.** Suppose an existing Cyclone node publishes /external/poses and your local application expects /local/poses. In a configured gateway terminal on the native host run:

```bash
unset AGNOCAST_BRIDGE_DEMAND
ros2 run agnocast_rmw_loan_probe route_bridge r2a geometry_msgs/msg/PoseArray /external/poses /local/poses
```

Set `AGNOCAST_RMW_SUBSCRIBE_TOPICS=/local/poses` on the local subscriber. A fully qualified type allows startup before discovery. Use `auto` only with an active, unambiguous publisher and installed type support. PoseArray and Image have tested generic forwarding paths; Buffer-bearing messages may require a compatible layout. For **two native hosts**, export on the source host and import the resulting DDS topic on the destination host. Use distinct local and DDS names to avoid loops. The network crossing is qualified between isolated endpoints on one laptop; two physical native hosts remain unverified. The tutorial covers topics. It does not establish service, action or GPU-handle bridging.

## 7. Troubleshoot or roll back one edge

| What you see | What to check |
|---|---|
| ROS cannot load rmw_agnocast | Run `ros2 pkg prefix rmw_agnocast` and the ldd check in step 2 in the failing process environment. Check the overlay order and Cyclone dependency. |
| The nodes appear in discovery but your local subscriber receives nothing | Compare both fully resolved names and their direction variables. Verify the module version, device access, heap hook, C++ type support, message layout and QoS. |
| The local subscriber works but your remote logger is empty | Check that the export is running, that the recorder uses the DDS **output** topic, and that the gateway reports a match. Verify payload or bag count. |
| Must I change my executor or message definitions? | The example uses ordinary ROS C++ messages and its existing executor. Advanced loan retention and GPU buffers have separate application requirements. |
| The recorder went offline | A volatile best-effort export can lose data. After reconnection, verify new bag rows. If recreating a network interface leaves the recorder unmatched, stop it and start a fresh bag segment. |

To roll back, stop the gateway and both migrated nodes. Open fresh terminals, source your original application install, select rmw_cyclonedds_cpp and rerun your original launch. For a project that keeps the pilot loaded, you can also remove **the same edge from both native selection lists** and restart those processes to carry that topic over Cyclone.
