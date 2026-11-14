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

## Documentation roles

- PLAN.md defines the Bazel adoption strategy, phase boundaries, and durable
  decisions supported by experiment evidence.
- JOURNAL.md is the adoption guide for a non-Bazel Autoware tree: it records
  chronological to-dos, milestones, experiments, failures, decisions, and
  verification needed to introduce Bazel successfully.
- DEVELOPING.md describes the steady-state developer workflow after Bazel has
  already been adopted for a package or target. It must not carry migration
  sequencing or experiment history.
- UPSTREAM_RCR.md and UPSTREAM_AWF.md contain only demonstrated findings useful
  outside this repository.

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
- Starting with Autoware package integration, Cyclone DDS is the primary
  middleware target. Fast DDS is a Tier 2 compatibility target: keep it
  buildable and test it at explicit compatibility gates, but do not let it
  determine the primary package path.
- The fixed Cyclone DDS smoke graph loads 352 packages and configures 70,383
  targets. Fast DDS remains available through `--config=fastdds`; its smoke
  graph loads 371 packages and configures 76,915 targets.
- The loopback smoke behavior needs no custom Cyclone DDS XML or host network
  tuning. Add either only when a later network topology demonstrates the need.
- Committed Bazel inputs include the module lockfile. Registry ordering and the
  read-only RCR CI cache are repository configuration.
- ROS packages should come from the RCR until a local source package is needed
  for the current target.
- A local Autoware ROS package is a package-local Bazel module selected by a
  root `local_path_override`. The root pins the package's published RCR version
  so removing only the override selects the registry source without changing
  consumer BUILD files.
- An interface package retains its manifest-level package dependencies in its
  `MODULE.bazel`. Each `ros_message` or `ros_service` declares only referenced
  interface targets, and `default_generators.dependencies` contains only
  packages referenced by generated types. The localization service validates
  this convention across an Autoware package boundary; the v2x package
  validates an unused manifest dependency.
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

## Middleware gate before Phase 1 (complete)

Purpose: validate the middleware support policy on the existing smoke pair
before an Autoware package makes the graph larger.

Completed work:

1. Select `rmw_cyclonedds_cpp` as the default fixed RMW and retain Fast DDS
   behind an explicit compatibility configuration.
2. Build and run the talker/listener with Cyclone DDS in a clean caller
   environment. Begin with no custom Cyclone DDS XML or host tuning.
3. Build and run the same targets through the Fast DDS compatibility
   configuration.
4. Repeat both builds in the development image and compare the host and
   container executables for each configuration.
5. Record configured-target, package, action, and behavior results. Introduce
   middleware configuration or host setup only if a current test demonstrates
   that it is required.

Exit criteria:

- Cyclone DDS is the default and delivers five smoke messages.
- Fast DDS remains selectable and passes the same behavior check.
- Both configurations build without a sourced ROS installation.
- Host and development-image artifact comparisons have recorded results.
- Day-to-day commands for both tiers are documented.

## Phase 1: interface package layout (complete)

Purpose: establish a maintainable package-local module layout for Autoware
interfaces while preserving the RCR's generated target conventions.

Completed work:

1. Added `autoware_common_msgs` as a package-local module with one direct
   `rosidl_default_generators@0.0.0` dependency.
2. Declared `ResponseStatus` with `ros_message` and generated the package-level
   C, C++, Python, Rust, and protobuf targets with `default_generators`.
3. Selected the module from the root with `local_path_override` and consumed
   its paired `:cc`/`:cc_shared` targets from a C++ test.
4. Built the complete module surface and ran the consumer under Cyclone DDS,
   Fast DDS, and the read-only-source development image.
5. Selected the published `autoware_common_msgs@1.11.0-3.rcr.2` source by
   removing its local override and ran the unchanged root consumer.
6. Added `autoware_localization_msgs` with message and service targets. Its
   `InitializeLocalization` service directly depends on
   `autoware_common_msgs/ResponseStatus`.
7. Published and received generated `KinematicState` messages with Cyclone DDS,
   then repeated the behavior with Fast DDS.

Exit criteria:

- At least two interface packages build as package-local modules, including one
  cross-package Autoware interface dependency.
- A consumer switches between local source and the RCR without changing its
  BUILD target.
- The package convention and day-to-day commands are documented.
- A generated Autoware message passes Cyclone DDS behavior and the Fast DDS
  compatibility gate.

Answers from Phase 1:

- Package source selection belongs in the root module. A concrete RCR version
  plus `local_path_override` selects local development by default; removing the
  override selects the published source without changing a consumer label.
- Cross-package interfaces retain the RCR target convention. The consuming
  package needs a direct module dependency, the referenced message target on
  the individual interface rule, and the package repository in
  `default_generators.dependencies`.
- Each additional local module needs one root `bazel_dep`, one root
  `local_path_override`, and its own package-local `MODULE.bazel`. No central
  target list was needed.

## Phase 2: complete the Autoware interface package set (complete)

Purpose: apply the demonstrated package convention to the remaining Autoware
interface packages in dependency order.

Begin with the smallest package layer that depends only on ROS interfaces or
the two modules already integrated. Add packages in bounded groups, build every
generated target for each group, and add consumer tests for new dependency
shapes. Do not add a generator until repeated package work demonstrates what it
must automate.

Completed initial slice:

- added `autoware_control_msgs` and `autoware_v2x_msgs`, each with four message
  definitions and no dependency on another unported Autoware package;
- built all 15 generated targets exposed by each module and passed a root C++
  consumer under the Cyclone DDS and Fast DDS configurations;
- validated same-package message dependencies and bounded sequences;
- retained the v2x manifest's `geometry_msgs` module dependency without adding
  it to generator inputs because no v2x interface references it.

The read-only development-image gate now covers the complete Phase 2 package
set. All 208 targets build and all five C++ consumers execute under both the
Cyclone DDS and Fast DDS configurations with the repository mounted read-only.

Completed base layer:

- added `autoware_system_msgs`, `autoware_sensing_msgs`,
  `autoware_map_msgs`, `autoware_planning_msgs`, and
  `autoware_perception_msgs`;
- built all 114 generated module targets and passed one representative C++
  consumer under the Cyclone DDS and Fast DDS configurations;
- selected `geographic_msgs@1.0.6-3.rcr.2` directly at the root because the
  `ros_core` release does not select packages outside its package set.

Completed dependent layer:

- added `autoware_vehicle_msgs` and `autoware_simulation_msgs` and built their
  complete 26-target and 13-target surfaces;
- selected the published vehicle version at the root while assigning the
  unpublished simulation module the local-only `0.0.0` dependency version;
- demonstrated that each referenced external interface, including
  `autoware_perception_msgs/Shape`, must be an explicit message-rule edge;
- built all 208 targets from the 11 interface packages together and executed
  all five C++ consumers under both middleware configurations.

`autoware_msgs` remains an ament metapackage rather than a Bazel module. It
contains no interfaces or generated outputs; its package manifest only groups
the same 11 modules that the combined build gate validates directly.

Exit criteria:

- Every interface package required by the first runnable Autoware core slice is
  available as a local module.
- Each added package's complete generated target surface builds with Cyclone
  DDS, and each bounded group passes the Fast DDS compatibility gate.
- Repeated registration or BUILD patterns are documented with enough evidence
  to decide whether automation is warranted.

All Phase 2 exit criteria are met.

## Phase 3: Autoware utilities and C++ package conventions (in progress)

Purpose: establish the package-local Bazel conventions needed by Autoware C++
libraries and tests before selecting the first runnable core slice.

Begin with `autoware_utils_math`. It is a compiled library with an existing
GoogleTest suite but no runtime ROS dependency, so it isolates C++ packaging
and test behavior from middleware and core-package concerns. Add subsequent
utility packages in dependency order and introduce ROS-specific conventions
only when their source requires them.

Completed initial slice:

- added `autoware_utils_math` as a package-local module selected by the root;
- exposed its public headers and two compiled sources as one `cc_library`;
- built and executed the complete existing nine-file test suite as one
  `cc_test`;
- demonstrated that a test dependency in an overridden package module cannot
  be marked `dev_dependency`, because development dependencies are ignored for
  non-root modules.

Completed ROS leaf slice:

- added `autoware_utils_system` as a compiled library and
  `autoware_utils_rclcpp` as a header-only library;
- kept RCR shared-library closure on final `cc_test` targets because
  `cc_library` has no `dynamic_deps` attribute;
- executed both packages' existing tests with Cyclone DDS and Fast DDS;
- used `autoware_utils_rclcpp`'s polling-subscriber tests as the first utility
  publisher/subscriber behavior gate.

Completed generated-message and Boost leaf slice:

- added the header-only `autoware_utils_uuid` against the rosdistro's existing
  Boost.UUID target and `unique_identifier_msgs`;
- added compiled `autoware_utils_visualization` against RCLCPP and
  `visualization_msgs`;
- pinned external ROS packages at the root and kept package-local ROS
  dependencies at `0.0.0`, preserving root-controlled release selection;
- executed both test suites with Cyclone DDS and Fast DDS.

Exit criteria:

- The utility packages required by the first runnable core target build from
  their local source with public include paths matching their CMake consumers.
- Existing unit tests for every integrated utility package execute with Bazel.
- Repeated C++ library, test, ROS dependency, and runtime-data patterns are
  documented only after examples demonstrate them.
- The complete bounded utility set passes in the read-only development image.

The following core phase will keep its Agnocast-free portion limited to the
smallest publisher/subscriber slice needed to establish node and communication
test infrastructure. Agnocast becomes a dedicated core subphase when a current
target requires its wrapper or backend.

## Later phases

These are scope markers, not implementation decisions. Refine each phase only
when the preceding phase meets its exit criteria.

1. Autoware core packages in dependency-driven runnable slices, with a minimal
   Agnocast-free publisher/subscriber slice before the Agnocast subphase.
2. CUDA and TensorRT packages on x86 hardware.
3. Install tree, headless OCI image, and planning simulator behavior test.
4. Cross-build and non-GPU tests for Jetson Thor, followed by hardware tests.

From phase 1 onward, runnable slices use Cyclone DDS by default. Fast DDS
compatibility is checked after each phase reaches its Cyclone DDS exit criteria.

Potential topics such as Agnocast, OSQP compatibility, OpenMP, CMake parity,
runtime-image deduplication, and remote caching will be evaluated when a current
target requires them.

## Commit and publication policy

Use signed-off Conventional Commits and validate each task in proportion to its
behavior. Work directly on local branch `bazel` and push validated commits to
`paulsohn/bazel`. Use force-with-lease only when intentionally replacing remote
history and only against an explicitly observed remote SHA.
