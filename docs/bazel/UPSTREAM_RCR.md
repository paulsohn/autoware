# Findings for the ROS Central Registry

This document collects demonstrated findings that may help the ROS Central
Registry. Add an entry only when it includes a minimal reproducer or exact target,
the observed result, the confirmed cause, and a proposed upstream action.

For each finding, record:

- RCR module and version;
- Bazel version, platform, and relevant flags;
- minimal command or target that reproduces it;
- symptom, confirmed cause, and local experiment;
- whether current upstream still exhibits it;
- suggested issue, documentation change, test, or patch.

## Findings

### rclpy statically embeds ROS/DDS globals in its Python extension

Local correction: `a2dcb386`.

- [ ] Associate the shared-dependency correction with an upstream RCR change.

Verified 2026-11-16 on Ubuntu 24.04 x86_64, Bazel 9.3.2, LLVM/libc++ 21.1.8,
Python 3.14.4, Lyrical and Fast DDS. The selected module is
`rclpy@10.0.11-1.rcr.0`, with the published rosbag2 Python runtime in the same
process. No Agnocast allocator preload is used for this Python suite.

Reproducer in this experiment:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --config=no-agnocast \
  --nocache_test_results --test_output=errors @ros2agnocast//:tests
```

All 95 Python assertions pass, then the interpreter aborts with
`corrupted size vs. prev_size while consolidating` or
`double free or corruption (!prev)`. A GDB shutdown backtrace reaches
`eprosima::fastdds::dds::DomainParticipantQos::~DomainParticipantQos()` inside
`rclpy/_rclpy_pybind11.so`, followed by libc exit handlers. The suite imports
both rclpy and rosbag2_py; test assertions alone miss the process-exit defect.

The overlay's `rclpy_bindings` cc_shared_library omits `dynamic_deps`, despite
its matching compile/shared `DEPENDENCIES` map. ELF inspection confirms the
extension embeds Fast DDS symbols and has no ROS/DDS DT_NEEDED entries.
Other loaded native extensions use shared ROS dependencies, duplicating DDS
global state in one process. Add `dynamic_deps = DEPENDENCIES.keys()` to
`rclpy_bindings`, matching the established RCR shared-library pattern. The
local single_version_override pins that module and applies only this BUILD
change through `third_party/rclpy/rclpy_shared_dependencies.patch`.

After the correction, the complete 95-case suite exits successfully. The
extension has DT_NEEDED entries for librcl.so and librmw_fastrtps_cpp.so and
no longer defines the Fast DDS DomainParticipantQos destructor. Keep the normal
Python shutdown path; suppressing destructors or filtering tests would hide
the linking defect. Broader Phase 4 validation is recorded in JOURNAL.md.

The current published [module metadata](https://bazel.ros.org/modules/rclpy/metadata.json)
lists 10.0.11-1.rcr.0 as the latest version on the verification date. Its
[published overlay](https://bazel.ros.org/modules/rclpy/10.0.11-1.rcr.0/overlay/BUILD.bazel)
still omits the boundary; the downloaded overlay matches source.json integrity
`sha256-PlBGagSfMwv+J/XOTHk84MgFhtKJuZjk0CONtbdIbAI=`.
This verifies the published release, not unpublished upstream work. Suggested
upstream action: fix the overlay and add a Python native-extension coexistence
check that also requires clean interpreter exit. Other OSes, toolchains and
ROS distributions have not been checked for this finding.

### CUDA 13.2 CRT compatibility with the LLVM 21 libc++ toolchain

- [ ] Associate a documentation/example proposal with upstream. This is a
  tested unsupported-configuration workaround, not an RCR/NVIDIA support claim.

Verified 2026-11-18: Bazel 9.3.2, Ubuntu 24.04 x86_64, hermetic LLVM/libc++
21.1.8, upstream rules_cuda 0.4.0, CUDA 13.2.2 redist/CRT 13.2.86 and sm_120.
RCR's available rule fork is 0.3.0.rcr.0; the exercised nvcc/shared-cudart
consumer works with upstream 0.4.0 without carrying that rule fork's patches.
No equivalence for untested static/multi-DSO, clang-CUDA or SBSA paths is claimed.

Reproducer: the local autoware_cuda_utils package's complete original suite,
`bazel test --config=no-agnocast --nocache_test_results --test_output=errors @autoware_cuda_utils//...`.
Without the explicit policy override, CUDA rejects libc++ on x86. With
_ALLOW_UNSUPPORTED_LIBCPP but unmodified CRT, pow refers to removed
std::__promote. Subsequent diagnostics expose isnan's exception specification
and classification definitions conflicting with libc++ using declarations.
The pinned, unmodified vendor archive reproduces these failures; later CUDA
versions and other standard-library versions were not tested.

Local experiment: enable only the existing modern-libc++ math branches on
Linux/libc++ >= 21. The two documented CRT patches are applied to a separately
checksum-pinned http_archive component and substituted with standard Bazel
override_repo; no custom component-patching API or rules_cuda fork is needed.
All 17 original GPU cases pass with zero skips; the native full-core regression
also passes. See the Phase 6.1 journal for pins, exact commands and action audit.

Suggested upstream action: document the root component-override pattern and
current compiler/standard-library support boundaries for RCR LLVM users. Do
not apply these headers broadly or silently enable an unsupported combination;
retain the policy choice and version-specific complete-suite evidence. This
finding is not an Autoware implementation or Lyrical source correction.

### Registered Python executables lose their launcher and runfiles

- [ ] Associate with a matching upstream change. Reviewed 2026-11-22: the
  [latest published version](https://bazel.ros.org/modules/rosdistro/metadata.json)
  is lyrical.2026-10-05.rcr.4 and its
  [registration overlay](https://bazel.ros.org/modules/rosdistro/lyrical.2026-10-05.rcr.4/overlay/ament/ament_resource.bzl)
  still iterates every DefaultInfo file and constructs runfiles without input
  runfiles. No unpublished fix or new submission is claimed.
- Input: rosdistro rcr.4, rules_python 2.4.5, Python 3.14.4, Bazel 9.3.2,
  Ubuntu 24.04 x86_64, --config=runtime. Register a py_binary whose main source
  and launcher share a basename under lib/<package>, with strip_prefix="*".
  Executing that indexed path runs the source shebang and fails to import its
  declared dependencies. Explicit renames have the same multiple-output issue.
- Select DefaultInfo.files_to_run.executable for executable targets; keep ordinary
  file lists intact, select renames consistently and merge input default runfiles.
  The local third_party/rosdistro/register_executables.patch makes only this rule
  change. The minimal command is bazel test --config=runtime --config=no-agnocast
  //third_party/rosdistro/test:registration_test. Both executable profiles and
  ordinary data pass, followed by the affected original planning-node/wrapper
  suites and installed script/startup checks. Exact commands/results are in the
  journal. Suggest upstreaming this rule correction with the generic regression.

### Standard ROS CLI runtime discovery needs distribution metadata and package-qualified executables

- [ ] Associate missing CLI distribution metadata with an upstream RCR change.
- [ ] Associate package-qualified executable aliases with an upstream RCR change.

Inspected and reproduced 2026-11-23 on Ubuntu 24.04 x86_64, Bazel 9.3.2,
LLVM/libc++ 21 and the selected Lyrical registry snapshot. These observations
refer to the pinned releases, not a claim about every subsequent registry release.

The public Python libraries for ros2action, ros2component, ros2lifecycle,
ros2node, ros2param, ros2run and ros2service 0.40.9-1.rcr.0, plus ros2launch
0.29.9-1.rcr.0, omit distribution metadata for entry_points declared in setup.py.
A CLI containing those dependencies rejects node as an unknown command; the
already registered bag/doctor/interface/pkg/topic commands remain visible.
Python importability alone does not establish importlib.metadata discovery.
The local `//third_party/ros2cli:entry_points` compatibility provider generates
metadata with pinned setuptools from each dependency's declared setup.py.
All eight version/plugin-table comparisons pass, and the consuming CLI loads
all thirteen selected command families. No SDK source/BUILD patch is needed
for that local adapter. Upstream could generate metadata once from authoritative
package inputs and expose it to both Python runfiles and installed-underlay
consumers, avoiding separate hand-maintained entrypoint tables.

Separately, rosdistro lyrical.2026-10-05.rcr.4's ament_index executable mapping
exports bin/<name>, whereas standard ros2pkg/ros2run APIs search
lib/<package>/<name>. The CLI resolves rclcpp_components but lists no executables.
The local [package alias patch](../../third_party/rosdistro/package_executables.patch)
adds the package-qualified path while retaining bin/. Its generic regression
checks file identity, discovery and invocation through those standard APIs.
The actual component container then becomes discoverable; its separate
source-lifetime issue is recorded below.

Reproduction/verification targets:

```sh
bazel test --config=runtime --config=no-agnocast --test_output=errors \
  //third_party/ros2cli:metadata_test //third_party/rosdistro:executable_index_test
```

[Metadata adoption evidence](JOURNAL.md#2026-11-23-phase-76--dependency-owned-ros-cli-extension-metadata);
[index adoption evidence](JOURNAL.md#2026-11-23-phase-76--package-qualified-runtime-executable-indexes).

### Component-container early returns leave ROS shutdown until global destruction

- [ ] Associate the early-shutdown source correction with a ROS 2 rclcpp upstream fix.
- [ ] Associate the benchmark compile-dependency correction with an RCR overlay fix.

Verified 2026-11-23 with rclcpp_components 32.0.3-1.rcr.0, LLVM/libc++ 21 and
Cyclone DDS. Both the build-tree binary and its extracted runtime copy print
usage for --help and then segfault. GDB reaches spdlog::details::registry::drop
through rcl_logging_external_shutdown, rclcpp::Context::shutdown and destruction
of the global default context in exit handlers. The help and invalid-executor
branches return after rclcpp::init without the shutdown used by normal execution.

The local [two-path source correction](../../third_party/rclcpp_components/early_shutdown.patch)
explicitly shuts down before those returns. The native regression verifies
help and both invalid-executor forms return 0/1/1 without a signal. All seven
original component manager/API cases and three benchmark operations also pass.
The complete suite first exposed a missing component_manager_library compile
dependency on benchmark_components; the separate
[overlay correction](../../third_party/rclcpp_components/benchmark_dependency.patch)
supplies its existing header/API without changing benchmark source or operations.

The [rolling source](https://github.com/ros2/rclcpp/blob/rolling/rclcpp_components/src/component_container.cpp)
retrieved on 2026-11-23 retains the two early returns without shutdown. No
matching upstream fix association is recorded; unrelated rolling changes are
outside this correction. The reproducible commands and native backtrace are in
[the source-task journal](JOURNAL.md#2026-11-23-phase-76--component-container-early-exit-logging-lifetime),
with its [suite prerequisite](JOURNAL.md#2026-11-23-phase-76--component-sdk-benchmark-dependency).

### Generated interface indexes omit declared dependency indexes

- [ ] Associate transitive interface-index forwarding with an upstream RCR change.

Verified 2026-11-23 with rosidl_core_generators 0.4.3-3.rcr.5. A consumer of
nav_msgs:ament_index can locate Odometry.msg but cannot find its referenced
geometry_msgs/Pose or std_msgs/Header resources. The published core_generators
macro forwards generated language dependencies but not their ament indexes.
Python importability and C++ link closure therefore do not establish schema
introspection closure.

The [one-line dependency correction](../../third_party/rosidl_core_generators/dependency_indexes.patch)
passes the already-declared dependencies through the macro's existing
_dependency_label mapping into ament_index.deps. It adds no package inventory
or source discovery. The generic regression, whose only data dependency is
nav_msgs:ament_index, resolves all three package manifests and message resources.
The Autoware collection consumer also imports native Trajectory/Pose type support
and resolves their schemas. No registered SDK tests exist in this pinned module;
the consumer suite supplies the acceptance evidence. No matching upstream
association is recorded for the inspected published version.

```sh
bazel test --config=runtime --config=no-agnocast --test_output=errors \
  //third_party/rosidl_core_generators:dependency_indexes_test \
  //bazel/interfaces:python_interfaces_test
```

[Reproducible adoption evidence](JOURNAL.md#2026-11-23-phase-76--transitive-interface-resource-indexes).
