# Autoware + Bazel integration experiment

## Goal

Build a useful Autoware subset with Bazel on Ubuntu 24.04 x86_64, using ROS 2
Lyrical packages from the ROS Central Registry where practical. The experiment
should produce pinned, repeatable builds and eventually a headless planning
simulation in an OCI image. Jetson Thor support follows the x86 experiment.

## Method

Advance one runnable slice at a time. Each phase begins with the smallest target
that can answer its open questions. A choice becomes a project decision only
after this branch has exercised it. Failed approaches and their evidence belong
in JOURNAL.md; durable decisions and their reasons belong here.

For every slice:

1. Define the behavior and dependency boundary.
2. Add the minimum workspace, rule, or source changes required.
3. Build and test in a clean environment.
4. Run a behavior check when the target is executable.
5. Journal commands, results, and diagnosed failures before committing.

## Current decisions

- The first platform is x86_64 Ubuntu 24.04.
- Bazel 9.3.2 is selected by `.bazelversion`.
- The bootstrap uses the RCR's `lyrical.2026-10-05.rcr.0` `ros_core`
  release and the matching `lyrical.2026-10-05.rcr.3` `rosdistro` patch.
- LLVM 21.1.8 supplies the C/C++ toolchain, with glibc 2.34 as the Linux ABI
  floor.
- Fast DDS is selected for the bootstrap. Leaving RMW selection dynamic loaded
  1,188 packages during analysis; selecting `rmw_fastrtps_cpp` completed with
  371 packages while preserving the RCR shared-library layout.
- Committed Bazel inputs include the module lockfile. Registry ordering and the
  read-only RCR CI cache are repository configuration.
- ROS packages should come from the RCR until a local source package is needed
  for the current target.
- Source snapshots under `src/` remain ordinary tracked files.
- Bazel work must not depend on a sourced host ROS installation.
- Developer-specific resource limits live in ignored `user.bazelrc`.
- Source compatibility changes and Bazel build definitions use separate commits.
- The journal is the reproducible experiment record; upstream notes contain only
  demonstrated, generally useful findings.

## Phase 0: bootstrap ROS with Bazel (complete)

Purpose: establish whether a minimal Lyrical C++ publisher and subscriber can be
built reproducibly on the development host without using the host ROS install.

Completed work:

1. Inspect the current RCR examples and release metadata for the supported Bazel
   version, toolchain registration, registry configuration, and Lyrical release
   module. Record the exact source revision or URLs consulted.
2. Add the minimal root workspace files and pin the selected Bazel version.
3. Add a publisher and subscriber using `rclcpp` and `std_msgs`.
4. Build from an environment that does not expose `/opt/ros` variables.
5. Run publisher and subscriber together and verify message delivery.
6. Repeat the build in a minimal Ubuntu 24.04 development image, then compare the
   relevant executable digest with the host build.

Exit criteria:

- Both smoke targets build without apt-installed ROS development packages.
- The subscriber receives messages from the publisher.
- Workspace versions and registry inputs are pinned.
- Host requirements and day-to-day commands are documented.
- Host and development-image output comparison has a recorded result, including
  any known limitation in what was compared.

Answers from Phase 0:

- `ros_core` provides the release selection for `rclcpp`, `std_msgs`, and their
  transitive dependencies; the matching `rosdistro` module supplies the RMW
  configuration and shared-library collection.
- Fast DDS avoids loading every available RMW implementation. The smoke graph
  remains broad: 76,915 configured targets and 7,352 declared actions.
- A clean caller environment needs only `HOME` and a path containing Bazelisk
  and system tools. Actions receive pinned locale and path values; ROS runtime
  variables are set by `.bazelrc`.
- The RCR configuration works on Ubuntu 24.04 after declaring direct root
  dependencies needed by root labels and selecting a platform ABI constraint.
- The development image needs CA certificates, curl, git, Python 3, unzip, and
  zip in addition to Bazelisk. It contains no ROS installation.

## Later phases

These are scope markers, not implementation decisions. Refine each phase only
when the preceding phase meets its exit criteria.

1. Autoware interface packages and a maintainable local-package layout.
2. Autoware utility packages and shared build conventions.
3. Autoware core packages in dependency-driven runnable slices.
4. CUDA and TensorRT packages on x86 hardware.
5. Install tree, headless OCI image, and planning simulator behavior test.
6. Cross-build and non-GPU tests for Jetson Thor, followed by hardware tests.

Potential topics such as Agnocast, OSQP compatibility, OpenMP, CMake parity,
runtime-image deduplication, and remote caching will be evaluated when a current
target requires them.

## Commit and publication policy

Use signed-off Conventional Commits and validate each task in proportion to its
behavior. Work directly on local branch `bazel` and push validated commits to
`paulsohn/bazel`. Use force-with-lease only when intentionally replacing remote
history and only against an explicitly observed remote SHA.
