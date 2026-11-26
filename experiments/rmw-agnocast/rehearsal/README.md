# Rehearse a Cyclone-to-Agnocast migration on one laptop

Run this job to check an ordinary project's migration, gateway recovery and rollback against a fresh build. A second job in the same runner measures API compatibility with pinned upstream tests. The [developer tutorial](../TUTORIAL.md) explains the commands a project developer uses in each terminal.

## Prepare the fixture

Run from the experiment checkout on x86-64 Linux. Use the existing laptop fixture or supply an exported Ubuntu 26.04 Lyrical filesystem with Clang 21, GCC 15 headers, colcon, the ROS development tools, Cyclone, Zenoh, rosbag2 SQLite, and the package build dependencies from the tutorial. The host needs the version 2.4.1 kernel module, its released heap hook, Bubblewrap, the repository's user-systemd memory guard and access to the default Docker Engine. The runtime image needs the matching Ubuntu system libraries and Python. Both containers mount the same stock ROS distribution read-only. Only the application container receives the new overlay, heap hook and Agnocast device. The two containers have separate IPC and network namespaces.

The test build also needs test_msgs, osrf_testing_tools_cpp, performance_test_fixture, mimick_vendor and their development dependencies. Install them in the Lyrical userspace or point REHEARSAL_TEST_DEBS at a directory containing the stock Debian packages, using the /ws path seen inside Bubblewrap. The job extracts the packages into its own workspace. On the provisioned laptop, these commands use previously downloaded source objects, schemas and Debian packages. Each workspace under build/ must have a fresh name.

```bash
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
export REHEARSAL_HOOK=/opt/ros/jazzy/lib/libagnocast_heaphook.so
export REHEARSAL_TEST_DEBS=/ws/build/rmw-agnocast/test-debs
mkdir -p build/rehearsal-inputs
git clone --depth 1 --branch 3.1.6 https://github.com/ros2/rmw_implementation.git build/rehearsal-inputs/rmw_implementation
git clone --branch lyrical https://github.com/ros2/examples.git build/rehearsal-inputs/examples
curl -fL https://raw.githubusercontent.com/ros-infrastructure/rep/master/xsd/package_common.xsd -o build/rehearsal-inputs/package_common.xsd
curl -fL https://raw.githubusercontent.com/ros-infrastructure/rep/master/xsd/package_format3.xsd -o build/rehearsal-inputs/package_format3.xsd
sg docker -c 'bash experiments/rmw-agnocast/rehearsal/run.sh build/rehearsal-next build/rehearsal-inputs/examples build/rehearsal-inputs build/rehearsal-inputs/rmw_implementation'
```

The default peer image is local/autoware-migration:awf-buffer-lyrical-zenoh. Select a prepared equivalent with REHEARSAL_IMAGE. The job records its image ID. The inputs include source archives of the experiment's HEAD, the two pinned upstream examples and both checksummed schemas. The release build uses one compile job at a time. The conformance build adds test executables after the live migration.

## Read the evidence

Inspect runtime/results.json for the application hashes, listener sequences, decoded local and peer bag sequences, verification windows and intentional outages. Each case saves the process library maps. A manual gateway stop leaves the publisher, subscriber and both recorders running. The local stream must cover the outage without a gap. The remote bag must omit the outage window and contain a complete five-sample window on each side. The automatic case starts the gateway through the application context. Rollback restarts the same executables with Cyclone.

The conformance job runs one GoogleTest case per process so a crash can be reported alongside later outcomes. The conformance-results/cases.csv inventory distinguishes test assertions, signals, timeouts and upstream skips. The regression check flags inventory changes and newly failing tests. Use compare.py with --strict to require every enabled case to pass. Raw logs and XML are kept in the numbered per-program directories under conformance-results. Program-local numbering follows upstream GoogleTest enumeration, starting at zero.

A second machine and an independent developer following the tutorial remain separate qualifications. This job measures behavior with a sequence-numbered String topic and exercises the standard API surface. The earlier Universe workloads remain the performance evidence.

## API support on the tested revision

The original migration rehearsal reproduced all six scenarios from two fresh builds. After the API fixes, 100 of 138 upstream RMW cases passed, 37 failed assertions and one skipped. All cases completed without an abort. Of 92 selected rclcpp cases, 88 passed and four failed assertions.

The [case inventory](baseline.csv) records each outcome against rmw_implementation 3.1.6 and the patched Lyrical rclcpp. It covers all 16 upstream RMW GoogleTest programs and four selected rclcpp programs. The GoogleTest counts include bad-input, fault-injection and mocked client-library cases. Use the separate two-process migration as the application check.

| Area | Verified behavior | Remaining limitation |
|---|---|---|
| Local topics and local recording | Ordinary C++ nodes exchanged String over the native RMW. A stock native recorder produced a readable SQLite bag. Positive RMW sequence take and ignore-local-publications cases passed. | The RMW publisher and subscription suites report remaining error-contract failures. |
| Graph and executors | All 17 RMW graph cases, 39 rclcpp node-graph cases and 28 selected executor cases passed. | The upstream matched-event and wait-set programs now pass. Finite liveliness-lease support remains open. |
| Services and clients | Default create/destroy, server availability with valid arguments and ordinary client/service counts passed. | Requested native naming, some QoS queries and invalid-input cases still fail expectations. |
| QoS | KeepLast, effective system-default liveliness and the explicit infinite-duration test passed. | A finite-liveliness-lease profile compared with itself returned a compatibility error. KeepAll subscription cases also differed from upstream. |
| Serialization | The upstream clean C and C++ round trips passed for ordinary and bounded messages. Local and stock-peer bag payloads decoded in sequence. | The C and C++ invalid-CDR cases now return errors. The suite covers the upstream fixtures. |
| Loans and preallocation | All four selected rclcpp LoanedMessage cases passed. | Four upstream loan cases reported failed assertions as well as a skip in setup. They count as failures. Publisher preallocation was the only clean upstream skip. |
| Lifecycle and input validation | Ordinary node init/shutdown and default publisher/client/service creation passed. | Publisher and subscription destruction, client/service destruction, and the upstream init/shutdown tests pass. Other bad-input cases remain open. |
| Network migration | Both manually configured networks delivered across a gateway restart, and the native RMW started a configured Cyclone gateway. | The rehearsal covers a configured export on a single laptop. Previous discovery, import and Universe workload results have their own fixtures. |

Five entry points explicitly return RMW_RET_UNSUPPORTED on this revision. They cover the two DynamicData take functions, serialization-support initialization, and publisher allocation init/fini. Dynamic discovery of a gateway topic by its installed type name is a separate capability. The recorded failures include APIs with implementations that differ from the upstream contract.
