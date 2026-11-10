# Upstreaming to the Autoware Foundation (AWF)

What this PoC found that helps AWF repositories **whether or not they ever use Bazel**. The AWF has no Bazel plan, so
nothing here argues for Bazel. The build is a different way to build the same sources: ROS 2 Lyrical (rclcpp 32),
clang 21 + libc++ instead of GCC + libstdc++, a glibc 2.34 target, strict per-package dependencies, and a headless
container without apt. Each difference exposed something that colcon on Jazzy hides. The fixes are ordinary source,
`package.xml` or documentation changes.

Sources: `autoware_core` 1.10.0, `autoware_utils` 1.11.0, `autoware_cuda` 1.0.0, `autoware_simple_planning_simulator`
1.1.0, `agnocast` 2.4.1, `autoware_universe` 0.53.2 (exact SHAs in the `chore(src): import` commits). Each fix below is
a separate commit on branch `bazel` (`git log --grep='^fix(src'`), so it can be cherry-picked onto upstream as it is.

Pitch: "We built autoware_core on Lyrical with clang/libc++. Here are the fixes, all Jazzy-compatible or version-guarded."
Lyrical is the next LTS that Autoware will have to move to anyway.

Before opening each PR: rebase onto upstream main, build and test with colcon on **Jazzy** (the fix must not break the
current distribution), and check that nobody fixed it meanwhile. Spot-checked on 2026-11-10: items 1.1, 1.2, 1.4 and 4.1
are still present on upstream main, and agnocast main has no rclcpp 32 support.

## 1. Bugs independent of distribution and toolchain (send first)

These are wrong today, on Jazzy with GCC.

| # | Repo / package | Bug | Fix (commit) |
|---|---|---|---|
| 1.1 | autoware_cuda / autoware_tensorrt_common | `utils.hpp` tests `__GNUC_MAJOR__`, a macro no compiler defines (GCC has `__GNUC__`), so it always falls back to `<experimental/filesystem>`. It only works because libstdc++ still ships that header | `#if __has_include(<filesystem>)` (5f591e18) |
| 1.2 | autoware_simple_planning_simulator | `simple_planning_simulator_default.param.yaml` line 21: a *commented-out* line contains `$(var vehicle_model_pkg)`. The launch file loads the file with substitutions on, so launching without that argument fails (`launch configuration 'vehicle_model_pkg' does not exist`) | Remove the substitution from the comment, or load without substitution (not fixed in our tree: the demo ships its own param file) |
| 1.3 | agnocast / agnocastlib | ApproximateTime unit test builds `ipc_shared_ptr` with `entry_id -1` (publisher side, never published). Releasing it calls `decrement_borrowed_publisher_num()` with nothing borrowed, and the process exits. It also frees messages that `TestMsgManager` frees. The same code is on upstream main | `entry_id 0`, like `test_exact_time.cpp` (3eccfb51) |
| 1.4 | autoware_utils (debug, geometry, pcl) | Headers include packages that `package.xml` does not declare: `autoware_utils_debug` → std_msgs, rosidl_runtime_cpp; `_geometry` → geometry_msgs, autoware_perception_msgs, autoware_planning_msgs; `_pcl` → rclcpp. colcon only builds because something else pulls them in transitively. rosdep, binary releases and isolated builds can break | Add the `<depend>`s (6fde3111) |
| 1.5 | autoware_core / autoware_motion_velocity_planner | Listed in `member_of_group rosidl_interface_packages` but has no interface files (stale) | Drop the group (not changed in our tree: our generator detects interface packages by their files) |
| 1.6 | autoware_core / autoware_trajectory | `pose.cpp` includes `tf2_geometry_msgs/tf2_geometry_msgs/tf2_geometry_msgs.hpp`. That path only resolves with colcon's doubled `include/<pkg>/<pkg>` install layout | Canonical `tf2_geometry_msgs/tf2_geometry_msgs.hpp` (f2e21122) |
| 1.7 | autoware_core / autoware_command_gate | The README describes an operation-mode interface that the node does not have: it provides only `/system/operation_mode/change_operation_mode` and publishes `/api/operation_mode/state` only in reply to it | Docs PR |

## 2. Lyrical forward-ports that also build on Jazzy

Mechanical changes. The newer API exists on Jazzy, and Lyrical deprecates or removes the old one. With `-Werror` on
Lyrical, each one breaks the build. These are good first PRs, grouped by repository, one PR per repository.

| Change | Where (commits) |
|---|---|
| `tf2*/…/*.h` → `.hpp` (the `.h` headers emit `#warning` in Lyrical, and some are removed: `tf2/LinearMath/*.h`, `tf2/utils.h`) | utils, trajectory, mission_planner, ndt_scan_matcher, velocity_smoother, crop_box_filter, object_recognition_utils, simulator, dummy_perception_publisher, agnocast (f2e21122, 6ccc6b74, 410beede, fb4df0f9, 3ab6aa4e, 015fc915, a246f979, 875cee4a, b87a5a8a) |
| Subscription callbacks take `ConstSharedPtr` (non-const `SharedPtr` callbacks are deprecated) | utils diagnostics test, perception_objects_converter, command_gate, euclidean_cluster, planning_topic_converter, stop_filter, twist2accel, vehicle_velocity_converter, gyro_odometer, ekf_localizer, gnss_poser, motion_velocity_planner, filter tests (2e2693af, 49e444f9, 0aedc57a, 7deb85ff, 7bea49af, 786c5fc6, 6f200662, 3ab6aa4e) |
| `rclcpp::spin_some(node)` → `SingleThreadedExecutor().spin_node_some(node)` (what it did internally) | several tests (49e444f9, 015fc915, 0419a032, 824a460c, a246f979) |
| `get_package_share_directory` → `get_package_share_path(...)` | vehicle_info_utils, map_loader, lanelet2_utils, ndt_scan_matcher, simulator tests (a7ea5184, 9fa97420, 5bea8592, 410beede, a246f979). Jazzy's ament_index_cpp has it |
| rclpy `logger.warn()` → `logger.warning()` (the alias is removed in Lyrical) | agnocast discovery agent + mock loggers (b73fd1aa) |
| gtest `INSTANTIATE_TEST_CASE_P` → `INSTANTIATE_TEST_SUITE_P` | autoware_utils math tests (2e2693af) |
| `TransformListener` from `NodeInterfaces` | gnss_poser tests (786c5fc6) |

## 3. Lyrical changes that need a version guard

| Repo | Change | Commit |
|---|---|---|
| agnocast (agnocastlib) | Real port to rclcpp 32, guarded like the existing `RCLCPP_VERSION_MAJOR >= 28` code: Executor, ClientBase, ServiceBase and NodeLoggingInterface overrides take `shared_ptr` by const reference (`AGNOCAST_RCLCPP_SHARED_PTR_PARAM`). `get_notify_guard_condition()` is gone, and `get_shared_notify_guard_condition()` must return a real guard condition, or `agnocast::Node` cannot be added to rclcpp 32 executors. New `NodeGraphInterface`/`NodeParametersInterface` methods; `TRACEPOINT()` → `TRACETOOLS_TRACEPOINT()`; message_filters dropped `mp_count`. 29 files, Jazzy still builds | b87a5a8a |
| autoware_core (agnocast_wrapper, ground_filter) | message_filters: `Subscriber` is templated on the message only, and `subscribe()` takes `rclcpp::QoS` (the `rmw_qos_profile_t` overloads are gone). 13 more `get_rmw_qos_profile()` uses in `src/core` will need the same | 39b32ed5, 3ab6aa4e |
| autoware_core (ndt_scan_matcher) | Eigen `eulerAngles()` is deprecated. We kept it with a local suppression, because `canonicalEulerAngles()` returns different ranges, which is a behaviour change. Upstream should decide | 410beede |

The agnocast port is the largest single contribution and the one that blocks Agnocast on Lyrical. It is worth an issue on
autowarefoundation/agnocast first, to agree on the guard style. Also offer the heaphook `Cargo.lock` (dd516c9d):
without it, the crate versions of `agnocast_heaphook` float, so two builds of the same commit can differ.

Heads-up, not a PR: Autoware's `osqp_interface` / `qp_interface` use the OSQP 0.6 API (~170 references). Lyrical's
distribution (at least the RCR's) carries OSQP 1.0, and porting also changes solver defaults. We keep 0.6.3 next to 1.0.
Upstream needs a plan before Lyrical.

## 4. Compiler and standard-library portability

All of these are valid C++ fixes or test fixes. They matter to anyone building Autoware with clang (clang-tidy users,
sanitizer builds, macOS, Android-like targets) or with a newer GCC.

| # | Issue | Fix (commit) |
|---|---|---|
| 4.1 | Missing standard includes, hidden by libstdc++'s transitive includes: `<sstream>` (tensorrt_common `profiler.cpp`), `<cstddef>` (tensorrt_plugins `argsort.hpp`) | Include them (5f591e18, 5a437333) |
| 4.2 | GCC-only `#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"` under clang (clang defines `__GNUC__` too) | `#if !defined(__clang__)`, as autoware_utils already does elsewhere (2e2693af, f886c3ca) |
| 4.3 | `std::chrono` counts printed with a format for `long` (`rep` is `long long` in libc++) | `%lld` (f886c3ca) |
| 4.4 | `std::bind(callback, ...)` with an rvalue-reference member pointer: libc++ keeps the reference type, which rclcpp's `function_traits` does not match | `std::bind(std::move(callback), ...)` (015fc915) |
| 4.5 | clang warnings under `-Werror`: unused lambda capture, `QosOverridingOptions{{X}}` (`-Wbraced-scalar-init`), set-but-unused counter | Small edits (2e2693af, 39b32ed5) |
| 4.6 | `M_PIf`/`M_PI_2f`/`M_PI_4f` are glibc ≥ 2.35 extensions (ground_filter `grid.hpp`), missing when targeting glibc ≤ 2.34 | `#ifndef M_PIf` fallbacks (3ab6aa4e) |

Tests that depend on unspecified behaviour. Both standard libraries are correct here; the tests are not:

| # | Test | Problem | Fix (commit) |
|---|---|---|---|
| 4.7 | lanelet2_utils `nn_search` | Four cases assert one particular lanelet among several at the same (zero) distance. `find_nearest` leaves tie order to `std::nth_element`/`std::sort`, which differs between libstdc++ and libc++ (a stable sort does not fix all cases) | Assert the expected lanelet is among the nearest (5bea8592). Possibly also document tie order in the API |
| 4.8 | autoware_trajectory `test_utils_lateral_metrics` | Exact compare of doubles computed along different paths (3 ULPs apart under clang) | `EXPECT_DOUBLE_EQ` (f2e21122) |
| 4.9 | path_generator goal connection | Orientation compared exactly; differs in the last bits under clang | Compare with the test's `rad_epsilon` (40359a4d) |

Lesson for AWF CI, independent of the build system: a second compiler job (clang + libc++, even only for `autoware_core`)
would have caught all of section 4, and it catches tests that pass by accident. Another data point: on our LLVM 23 trial
branch, a planning utility returned succeeding lanelet sequences in a different order after a libc++ upgrade.
Compiler upgrades can change behaviour, not only break builds.

## 5. Operations and packaging lessons (docs / ansible / Docker)

Useful to the AWF's Docker images, ansible roles and documentation:

1. **DDS settings are host requirements.** Autoware's `cyclonedds.xml` (`SocketReceiveBufferSize min="10MB"`, multicast
   on `lo`) aborts with `rmw_create_node: failed to create domain` on an untuned host. The ansible roles do not apply the
   settings. Ship a check script with the XML that names each unmet setting (ours: `bazel/config/dds_setup.sh`,
   plus a boot-time `host/autoware-dds-setup.service`). Inside containers: `net.core.rmem_max` is machine-wide (only the
   host or a privileged container can set it), `lo` multicast needs `CAP_NET_ADMIN`, and `ipfrag_*` works with `docker run --sysctl`.
   On Docker Desktop, containers see the VM's kernel, not the host's.
2. **JetPack versions**: JetPack 7.2.1 (Thor) ships CUDA 13.2.2 + TensorRT 10.16.2. TensorRT 10.16.2 is not in NVIDIA's
   public apt repository (10.16.1.11 is), and the ansible CUDA role (in our snapshot) installs 13.0. Check what the target platform ships
   before pinning from older provisioning scripts.
3. **ML models pinned by tag can move.** `ansible/roles/artifacts` pins Hugging Face repos by tag. The HF API gives the
   commit SHA (`/api/models/<repo>/revision/<tag>`) and per-file sha256 (`lfs.oid`), and `resolve/<sha>/<path>` serves
   Xet-stored files over plain HTTPS without git-lfs or xet clients. Pinning SHA + sha256 makes downloads verifiable
   with any tool. Splitting config (yaml, KB) from weights (onnx, GB) lets CI validate param files without
   downloading weights.
4. **Headless images do not need GL**, but PCL's VTK dependency pulls Qt and libGL into every node that links PCL. On Ubuntu
   24.04, `libpcl-io1.14` depends on `libvtk9.1t64`, which depends on `libglx0`, `libx11-6` and `libglew2.2`, so nodes that only
   read PCD files carry the GL stack. For
   size and attack surface of runtime images, this is worth knowing even without changing how images are built.
5. **CUDA builds depend on the checkout path.** nvcc hashes the source's absolute realpath into anonymous-namespace names
   (`_GLOBAL__N__<hash>_...`), so the same commit built in two directories gives different `.o` files. This defeats
   ccache/sccache sharing across machines or Docker build contexts. Its temporary file names also carry the PID. `--frandom-seed` (nvcc ≥ 12.9) alone fixes neither.
   Build at a fixed path, or see UPSTREAM_RCR.md 3.4 (a cudafe++ wrapper and `--keep --keep-dir`).
6. **TensorRT builder resources** are `dlopen()`ed by bare name: an install outside the system library directories needs
   `LD_LIBRARY_PATH` (an engine build segfaults otherwise). Matters for any non-apt TensorRT install, including tarballs.
7. **autoware_core alone does not make a drivable stack** without glue. There is no operation-mode transition manager
   (`is_autonomous_mode_available` never comes, and `change_to_autonomous` has no service), and the core controller is
   P-only on velocity without slope compensation. On the sample map's ~0.8 % grade with `enable_road_slope_simulation`,
   the vehicle settles below `engage_velocity`'s threshold and creeps at 0.09 m/s. The planning simulator also needs
   `dummy_perception_publisher` (the motion velocity planner waits for the obstacle point cloud). Worth a "core-only
   planning simulator" section in the docs: our working setup is `bazel/demo/launch/planning_simulator.launch.xml`.

## Order of work

1. Section 1: small, obviously correct, no Lyrical framing needed. 1.1, 1.2 and 1.3 first.
2. Section 2 as one PR per repository (autoware_utils, autoware_core, simulator, agnocast), each built on Jazzy.
3. Agnocast rclcpp 32 port (section 3): an issue first, then the PR with the Cargo.lock.
4. Section 4 fixes together with a proposal for one clang CI job.
5. Section 5 as docs issues / ansible PRs (DDS check script, JetPack versions, model pinning).
