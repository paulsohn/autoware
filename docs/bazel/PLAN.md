# Autoware + Bazel integration experiment

## Goal

Build a useful Autoware subset with Bazel on Ubuntu 24.04 x86_64, using ROS 2
Lyrical packages from the ROS Central Registry where practical. The experiment
should produce pinned, repeatable builds and eventually a headless planning
simulation in an OCI image. After the x86 experiment, target software build
compatibility with JetPack 7.2.1, a release supporting Jetson Thor. Physical
Thor hardware is not required to complete this experiment.

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

- Keep component repositories independent of the metarepository's checkout
  layout. Interface collection lives under `bazel/interfaces/`, and product
  plugin assembly under `bazel/agnocast/`. Defer a separate autoware_bazel
  source repository until there is a concrete cross-repository need.
  ros2agnocast exports its upstream templates and helpers without depending
  on the metarepository. The root selects overrides and interface roots; its
  provider has no Agnocast dependency and can serve other consumers.
  Keep a single `//bazel/interfaces:all_interfaces` selection in
  `bazel/interfaces/BUILD.bazel`. Pair explicit compile/shared provider targets
  once per package, with optional narrowed interface roots; application roots
  can override the metadata selection. The set forwards metadata, CcInfo and
  shared runtime providers to consumers. The plugin takes only this set and
  retains its own Agnocast/RCLCPP dependencies; separate proof/sample sets and
  consumer-owned copies of the package registry are unnecessary.
  The heap-hook component owns its Cargo extension; runtime launchers locate
  its artifact through repository-mapped runfiles rather than source paths.

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
  Routine tasks validate Cyclone DDS only. Reserve Fast DDS checks for major
  milestones and always obtain user approval before running any Fast DDS
  build, test, or behavior check.
- Routine validation builds affected targets, runs the changed module's full
  suite with Cyclone DDS and checks relevant behavior. Full system matrices
  are selected for communication-layer changes or scoped milestones, not
  automatically for each phase. Fast DDS remains explicitly approved Tier 2
  validation. Reuse evidence for unchanged code/configuration and distinguish
  complete sweeps from partial runs in the journal.
- The fixed Cyclone DDS smoke graph loads 352 packages and configures 70,383
  targets. Fast DDS remains available through `--config=fastdds`; its smoke
  graph loads 371 packages and configures 76,915 targets.
- The loopback smoke behavior needs no custom Cyclone DDS XML or host network
  tuning. Add either only when a later network topology demonstrates the need.
- Pin rclpy 10.0.11-1.rcr.0 with the small shared-dependency BUILD correction
  under `bazel/patches/`. Its extension must use the existing ROS/DDS shared
  targets, so importing rclpy alongside rosbag does not duplicate DDS globals
  and corrupt interpreter shutdown. The CLI suite verifies normal exit;
  UPSTREAM_RCR.md records the published overlay and reproducible evidence.
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
- Target build dependencies must be resolved and built by Bazel. Apt, pip,
  rosdep, and the existing Ansible roles are not dependency providers for
  Bazel targets; dependencies currently provisioned that way, including CUDA
  user-space libraries and tools, enter the experiment through pinned Bazel
  repositories or modules when a current target needs them. Host kernel
  drivers and the minimal tools needed to start Bazel remain platform
  prerequisites rather than target dependencies.
- `autoware_system_designer` and its related packages are outside this
  experiment. Their system-design workflow is orthogonal to building and
  running the selected Autoware subset, so their source snapshots remain
  untouched and no Bazel targets or removal work are planned for them.
- Developer-specific resource limits live in ignored `user.bazelrc`.
- Source compatibility changes and Bazel build definitions use separate commits.
- Validate Lyrical source compatibility independently while aligning with AWF
  fixes when their scope matches the current target. The AWF transition is
  tracked in [autoware#7404](https://github.com/autowarefoundation/autoware/issues/7404)
  and [autoware_core#1538](https://github.com/autowarefoundation/autoware_core/issues/1538).
  Record dated local-patch/upstream-PR associations in UPSTREAM_AWF.md, including
  overlap, differences, and missing validation. Our Ubuntu 24.04/Clang/RCR
  results do not establish AWF's Ubuntu 26.04/GCC or Humble/Jazzy compatibility.
- The journal is the reproducible experiment record; upstream notes contain only
  demonstrated, generally useful findings.
- Agnocast is compiled into Bazel targets unconditionally. `ENABLE_AGNOCAST` is
  a runtime choice: Bazel run and test actions select Agnocast by default, while
  `--config=no-agnocast` selects the RCLCPP backend from the same binaries.
- Agnocast bridge plugins are generated and linked inside the Bazel graph. A
  product-owned interface-set target supplies explicit package roots; an aspect
  follows their `RosInterfaceInfo` providers. No build step discovers types
  through `AMENT_PREFIX_PATH` or generates a package for a later build.
  Root-module plugin targets expand the upstream C++ templates using native
  Bazel actions, without a separate Python renderer or the ROS generation verb.
  Lyrical services also instantiate their synthesized event-message pub/sub
  plugins, matching the verb's all-interface behavior.
- Explicit BUILD declarations define supported interfaces. Developers register
  interfaces and their dependencies when needed; undeclared source files are
  outside the graph. Test generated targets and their consumers. All-interface
  collections expose the declared closure of selected product roots, without
  enforcing complete source-directory registration.

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

## Phase 3: Autoware utilities and C++ package conventions (complete)

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

Completed service and diagnostics utility slice:

- added header-only `autoware_utils_logging` against RCLCPP, RCUtils, and the
  generated `logging_demo` service;
- added compiled `autoware_utils_diagnostics` against RCLCPP,
  `diagnostic_msgs`, and `diagnostic_updater`;
- validated the existing diagnostics publisher/subscriber characterization
  test with Cyclone DDS and Fast DDS;
- kept service-callback and timeout-transition behavior as explicit remaining
  test gaps because the existing package tests only construct those objects.

Completed geometry and TF utility slice:

- added the complete local `autoware_internal_planning_msgs` interface package
  as the first geometry prerequisite;
- integrated compiled `autoware_utils_geometry` from its current public
  message, TF2, Eigen, and Boost includes without adding unused `tf2_eigen`;
- integrated header-only `autoware_utils_tf` on the validated geometry target;
- pinned concrete TF2 RCR releases because their `0.0.0` modules are source
  stubs without Bazel overlays unless another root dependency selects a real
  release;
- passed the interface generator surface and both utility suites with Cyclone
  DDS and Fast DDS;
- deferred package shared libraries and compiled Boost shared pairs until a
  current downstream shared consumer reproduces that link requirement.

Exit criteria:

- The utility packages required by the first runnable core target build from
  their local source with public include paths matching their CMake consumers.
- Existing unit tests for every integrated utility package execute with Bazel.
- Repeated C++ library, test, ROS dependency, and runtime-data patterns are
  documented only after examples demonstrate them.
- The complete bounded utility set passes in the read-only development image.

The closing development-image gate passes with Cyclone DDS: all 26 selected
targets build and all nine utility suites pass 165 executed cases from the
read-only source mount. Two pre-existing geometry cases remain source-disabled
for known polygon-intersection edge behavior; they are not counted as passes.
JOURNAL.md records the exact image, commands, resource limits and XML results.
The documented additional service-callback/timeout coverage gaps do not change
the existing default-suite exit criterion. No Fast DDS repeat is required.

This utility baseline now supports the minimal core node and Agnocast
communication lane completed in Phase 4. Continue package adoption through
Phase 5.1's runnable dependency graph rather than expanding utilities in bulk.

## Phase 4: Minimal core and Agnocast communication lanes (complete)

Purpose: establish one runnable core node and a reusable communication behavior
gate without allowing an Agnocast-free package expansion to delay evaluation of
the Agnocast API and runtime.

The first lane is deliberately limited to `autoware_node`. Its existing node
initialization and executor test plus a Bazel-owned reliable publisher/subscriber
test define the core baseline. The baseline must pass with Cyclone DDS and at
the Fast DDS compatibility gate before another Agnocast-free core package is
selected.

The Agnocast lane starts after that baseline by integrating `agnocastlib` and
building `autoware_agnocast_wrapper` with `USE_AGNOCAST_ENABLED`
unconditionally. First run the compiled artifact with `--config=no-agnocast`
to isolate wrapper/API integration, then supply the heap hook and kernel/runtime
prerequisites required to run the same behavior with the default Agnocast
backend. The Fast DDS compatibility configurations remain composable with
both runtime selections.

Bridge support is part of this lane rather than a post-build packaging step.
Define a product-owned set of package-level interface roots, collect their
transitive `RosInterfaceInfo` providers during analysis, and generate each
bridge translation unit as a declared Bazel output. Link the resulting plugin
library in the same invocation. The current product set has 17 explicit roots:
12 Autoware interface packages, CIE config, sample and discovery interfaces,
std_msgs and std_srvs. Their transitive providers collect 175 descriptors.
Undeclared sources remain outside the graph; no source-completeness validator
or separate proof set is required. Synthesized service-event types bring the
plugin to 197 translation units and 394 exported typed factories. This gives
full-set compile/symbol coverage; communication tests sample concrete types.

Completed first Agnocast prerequisite slice:

- added the complete `agnocast_cie_config_msgs` message/service generator
  surface as a local-only module;
- exposed the CIE thread configurator's exported client and announcement
  transport library, which is the surface directly required by `agnocastlib`;
- passed its abstract Unix-domain-socket transport test with Cyclone DDS and
  Fast DDS without a kernel module or heap hook;
- subsequently adopted the scheduling core, pinned YAML dependency, both
  executables, all 137 C++ cases and seven Rust wire/transport cases when the
  component runtime required them (Phase 4.3).

Completed Agnocast shared-library slice:

- added `agnocastlib` as a local-only module and compiled the complete source
  surface of its exported CMake library into `libagnocast.so`;
- generated `agnocast_version.hpp` from the tracked manifest and template as a
  declared Bazel output rather than requiring a configured source tree;
- paired direct ROS compile providers with shared-library dependencies at the
  final `cc_shared_library` boundary, including conditional LTTng dependencies
  only when the rosdistro tracing feature is enabled;
- added only the Lyrical compatibility required by observed rclcpp 32,
  message_filters, TF2, and tracetools build failures;
- built the library with Cyclone DDS, Fast DDS, and tracing enabled, while
  deferring all kernel, heap-hook, discovery, component, and
  configurator-daemon runtime targets to their own behavior gates.

Completed kernel-independent Agnocast test slice:

- preserved the five existing CMake unit-test groups: the main mocked library
  suite, TF2 broadcasters, TF2 buffer, TF2 transform listener, and message
  filters;
- supplied compile-time headers from `agnocast_library` while linking tests
  across the local `libagnocast.so` boundary, allowing each test's existing
  mocks to interpose production symbols without duplicate static definitions;
- updated only test compatibility exposed by Lyrical rclcpp and TF2 APIs and
  corrected the message-filter fixture to construct the subscriber-side smart
  pointer it intends to exercise;
- passed all five groups with Cyclone DDS and Fast DDS without `/dev/agnocast`,
  the heap hook, or an installed ROS environment.

Completed wrapper fallback slice:

- added `autoware_agnocast_wrapper` as a local module whose library always
  exports `USE_AGNOCAST_ENABLED` and links the Bazel-built `libagnocast.so`;
- kept `ENABLE_AGNOCAST` solely as the runtime selector, so
  `--config=no-agnocast` exercises the RCLCPP backend from the same compiled
  wrapper and test binary;
- preserved the existing C++ behavior suite for generic pub/sub, polling
  subscribers, parameter clients, service introspection, message filters, and
  backend selection;
- passed all 36 tests with the Cyclone DDS fallback and the Fast DDS
  compatibility fallback without a sourced ROS installation;
- deferred the Python launch extension and the default Agnocast transport
  behavior until their current tests require launch packaging, the heap hook,
  kernel access, or component runtime support.

Completed heap-hook and wrapper C++ runtime gate:

- built the Rust heap hook from local sources with the RCR Rust 1.93.0
  toolchain, checksum-locked crates, and the real Bazel-built Agnocast shared
  library; selected its existing legacy-glibc bootstrap for the 2.34 ABI floor;
- added a runfiles-based preload launcher and passed all 26 existing allocator
  unit tests using their default bootstrap on Ubuntu 24.04;
- select the executor matching each test's node API: mixed ROS/Agnocast nodes
  use `SingleThreadedAgnocastExecutor`, Agnocast-owned nodes use
  `AgnocastOnlySingleThreadedExecutor`, and fallback uses the RCLCPP executor;
- initialize both contexts for the mixed C++ suite and use `spin()` with
  cancellation and joining for Agnocast callback dispatch;
- run the enabled kernel-backed gate with `--strategy=TestRunner=local` to
  avoid reused sandbox PIDs colliding on the host's PID-named shared memory;
- passed all 36 C++ cases without skips under both runtime selections
  and middleware configurations. Python launch packaging remains a separate
  planned gate before claiming adoption of the entire wrapper package.

Exit criteria:

- `autoware_node` builds from local source and its initialization, executor,
  and reliable loopback communication behavior pass with Cyclone DDS and Fast
  DDS.
- One Agnocast-enabled wrapper artifact passes its applicable communication
  behavior suite with the default Agnocast backend and with
  `--config=no-agnocast`; Fast DDS passes at the explicit compatibility gate.
- Heap-hook, kernel-module, allocator, component, and runtime prerequisites are
  introduced only when a current enabled target exposes them and are recorded
  explicitly.
- A product-owned interface set exposes standard and Autoware interfaces during
  analysis, and a Bazel-built plugin library bridges a representative message
  and service without an installed ROS environment or a second build.
- Further core selection is dependency-driven and may proceed alongside the
  Agnocast lane only when it does not expand the fallback baseline merely to
  avoid an Agnocast dependency.

### Validated gate: typed Agnocast bridge for the top-level interface set

The task covers the Bazel bridge generator and the single top-level interface
selection. Collect declared interfaces from the selected 15 Autoware,
Agnocast configuration, and standard ROS packages and their transitive
interface dependencies. Build `//bazel/agnocast:plugins` against that set.
Use the tracked Agnocast templates as action inputs; do not discover interfaces
through an installed prefix or invoke a second build.

Acceptance requires generator checks,
and both directions of payload-checked message/service behavior through the
loaded shared library. Run the bridge checks with the built heap hook and
kernel module with Cyclone DDS. Fast DDS checks require a separately approved
major-milestone compatibility gate. Recheck the
existing Agnocast library, heap-hook allocator, and wrapper suites as
regressions. Also check the moved build-support and component modules from
separate checkout directories when their source-selection boundaries change.
Commit the validated integration after documentation and source-fix review.

Source-completeness validation and its inventory repository were removed.
Explicit registration is the developer's responsibility. The provider-based
interface collection continues to serve arbitrary consumers.

The module-layout gate passes with Cyclone DDS: generator unit cases,
all 26 allocator cases, six bridge behavior cases, all
439 existing Agnocast unit cases, and the wrapper's 36 C++ cases with both
runtime selections. The earlier typed-plugin experiment also passed Fast DDS;
future compatibility checks require separate approval at major milestones.
Earlier separate-checkout checks passed before support was consolidated at
the root. The collector and plugin rules now live under `bazel/`; component
definitions do not refer to metarepository paths.
The production BridgeLoader gate now passes 11 cases: loader payloads,
service events, both automatic bridge directions and actual discovery-agent
startup/gossip. All 147 discovery-agent cases pass. The modern single-thread,
multi-thread and CIE containers, configurator, Python launch extension and CLI
are packaged with their ament resources and explicit runtime dependencies.

The wrapper message-filter Subscriber preserves its QoS profile on Lyrical.
All 37 C++ wrapper cases pass with each runtime selection; all 66 Python launch
cases pass. The complete 20-target library suite passes its supported cases
(673 total, five upstream unsupported-executor skips). No dependency-related
skip or test-group omission is accepted.

Phase 4.4 closes the Cyclone DDS runtime gate: seven functional launches/38
cases, all 128 one-to-one configurations/384 cases, 24 two-to-two configurations,
and nine service arrangements/45 cases pass without skips. The real CLI test
checks discovery, echo, recording, embedded schema and deserialized payload;
the complete CLI suite passes 95 cases. CUDA samples and kernel KUnit/build
remain separate platform tasks. These results validate user space against the
prepared 2.4.1 kernel module; they do not validate a new kernel build.
The approved Fast DDS milestone also passes all 39 regression targets, both
wrapper backends and the complete 12-target runtime gate, including both full
matrices. It exposed the rclpy shared-dependency omission and a bridge-manager
handoff race; both corrections have independent commits and reproducible
upstream notes. The source handoff fix aligns with draft AWF PR #1519.
Future Fast DDS checks require fresh approval.

Durable runtime decisions from these gates:

- Disable implicit Python initializer creation across the graph; otherwise
  transitive empty initializers hide generated namespace packages.
- Use the published ros2bag 0.33.4-1.rcr.0 Python provider rather than a host
  installation or a local overlay patch.
- Keep runtime payload in application/test data, outside the run-under tool's
  execution configuration. Give root ament indexes explicit export names and
  register ROS executables under lib/<package> for automatic discovery.
- Start Python without allocator preload, then set it for launched native
  processes after bootstrap. Preloading across Python exec stages can register
  the same PID twice with the kernel module.
- Spin multi-domain configurator nodes serially with one executor per context;
  shared scheduling state retains its original single-thread assumption.
- Declare both Python interface providers and generated ament IDL/index data
  for recording consumers. C++ plugin closure alone supplies neither.

### Phase 4 substeps: completed gates

Keep the existing Phase 0–4 numbers. Refer to incoming tasks by these stable
substep identifiers in plans, journals, and commits:

- **4.1 — Complete agnocastlib test adoption.** Register all 15 existing
  integration groups in addition to the five unit groups, prepare their real
  kernel/heap-hook and mock preload prerequisites, and pass the entire suite
  with Cyclone DDS without omissions or dependency-related skips.
- **4.2 — Production bridge activation.** Exercise ordinary plugin loading
  and automatic bridge requests using the root-generated library, including
  discovery-agent dependencies and existing tests when required by that path.
- **4.3 — Component and CLI runtime.** Build the component containers,
  configurator executable, and ROS-facing tools needed for normal Agnocast
  operation, with package resources and their existing applicable tests.
- **4.4 — Agnocast end-to-end closure.** Run the existing applicable sample and
  functional launch scenarios against the Bazel-built runtime, retain wrapper
  runtime-backend checks, and explicitly inventory remaining platform-only
  prerequisites. Complete Phase 4 only when these gates pass.

## Phase 5: Remaining autoware_core runnable slices (in progress)

Phase 5.1 covers shared foundations; Phase 5.2 adopts sensing, perception,
localization, planning and control dependency slices; Phase 5.3 composes API
and system behavior. Choose packages from executable dependency graphs and pass
each declared package suite before advancing.

### Phase 5.1: Shared core foundations (complete)

Bound this subphase to five local autoware_core packages. Prepare and validate
one module at a time, committing each successful module separately:

| Order | Package | Build and acceptance boundary |
| --- | --- | --- |
| 1 | autoware_kalman_filter | Two-source Eigen library; both existing test files (23 test declarations). |
| 2 | autoware_qos_utils | Public compatibility header; one Bazel-owned consumer test constructing real RCLCPP publisher/service entities with the default macros. |
| 3 | autoware_signal_processing | All three filter sources; all three existing test files and their fixture header (12 test declarations). |
| 4 | autoware_interpolation | All four sources and public headers; all eight existing test files (32 test declarations). |
| 5 | autoware_vehicle_info_utils | Both sources, current and compatibility include paths, ament-indexed YAML/launch resources, calculator entry point, and both existing test targets (2 ROS parameter cases plus 13 pure geometry cases). |

All listed test cases have executed successfully; actual results and commands
are recorded per module in JOURNAL.md. Local-only modules use 0.0.0 and
root local_path_override selections; no RCR counterpart is asserted. Reuse the
already-selected Eigen, Boost.Optional, ROS/message and geometry/TF providers.
Do not introduce another dependency version or source patch without an actual
current-target need.

Keep public libraries as cc_library targets following the Phase 3 convention;
introduce shared/install exports when a runnable consumer requires that boundary.
Final ROS executables/tests collect the shared closure of their static
libraries, including geometry's message/TF dependencies. Vehicle-info's
parameter test must locate its real YAML through the package index; its pure
suite remains a separate target. Preserve both scoped and legacy include paths.
The existing calculator uses pinned RCR Python/YAML dependencies and the same
index. Launch files are exposed as resources; launch composition is not a new
Phase 5.1 system-level gate.

Exit criteria: each module's complete existing suite and any specified consumer
check pass with Cyclone DDS; its exported headers/resources work without host
ROS, apt/pip target dependencies or metarepository labels inside the component;
and the final journal records actual commands, results and limitations. The
vehicle calculator also needs a direct run with its default indexed YAML. No
Fast DDS run, full communication matrix, or repeat of the whole development
image is an automatic requirement here. Reuse unaffected Phase 3/4 evidence.

All five module gates passed sequentially with Cyclone DDS: Kalman (23 cases),
QoS (one consumer case), signal processing (12 cases), interpolation (32 cases)
and vehicle-info (15 cases), totaling 83 executed cases with no skips or
failures. The vehicle calculator also passed its default indexed-YAML run.
Each module has its own validated commit and journal entry. No source
compatibility patches or new dependency versions were needed.

Domain-specific point/PCL, mapping/Lanelet2, motion/trajectory and solver
packages belong to their selected Phase 5.2 runnable slices. Component-interface
and global-parameter/API composition belongs to Phase 5.3; CUDA/TensorRT remains
Phase 6. Additional common packages are not implicit Phase 5.1 scope.

### Phase 5.2: Runnable dependency slices (in progress)

Split by executable dependency groups, keeping the package as the validation
and commit boundary. The groups below order investigation; they do not authorize
adding every dependency or patch at once. Refine later groups from their actual
source/test graphs as they begin.

| Subphase | Runnable boundary | Dependencies to introduce when needed |
| --- | --- | --- |
| 5.2.1 | Vehicle velocity converter → stop filter → twist2accel | Reuse the wrapper, generated interfaces, geometry and signal processing. Establish switchable standalone nodes, component records, resources and package tests. |
| 5.2.2 | Gyro odometer and EKF localization | Localization utilities, remaining TF helpers and diagnostics; then GNSS conversion with geography support. |
| 5.2.3 | Point-cloud sensing and CPU perception | Point types, debug/TF/PCL utilities, crop/downsample/ground filtering, clustering and object conversion, selected in dependency order. |
| 5.2.4 | Map loading and map-dependent localization | Geography, Lanelet2 utilities/extension, projection/map loaders, height fitting and visualization; NDT scan matching and pose initialization after their actual prerequisites. |
| 5.2.5 | Planning and control | Motion/trajectory utilities, solvers, route/path generation, smoothing, planner plugins and pure pursuit, in executable/test dependency order. |

Phase 5.3 retains API/system composition. A shared interface-specification or
test-support package required by a Phase 5.2 executable enters as that slice's
prerequisite; it need not wait for whole-system composition. CUDA/TensorRT
remains Phase 6. Source-less grouping metapackages do not need artificial build
targets. Exclude system-designer packages throughout.

#### Phase 5.2.1: Velocity and acceleration nodes (complete)

Start with `autoware_vehicle_velocity_converter`, not the entire group at once.
Its existing suite consists of five algorithm cases and one RCLCPP pub/sub case
across two test targets. Retain both even though CMake omits the node target
when ENABLE_AGNOCAST=1. Run the complete existing suite with
`--config=no-agnocast`; its peers and executor are RCLCPP-only. Compile Agnocast
support unconditionally and test enabled behavior separately.

Validated integration pattern:

- package-local algorithm and node libraries, a shared component, component
  records, config/launch resources and the original two test files;
- a wrapper-owned Bazel adapter expanding its existing switchable-main template
  with the executor pair selected by this package's CMake:
  AgnocastOnlyCallbackIsolatedExecutor / SingleThreadedExecutor;
- root-owned standalone behavior tests that load the real parameter YAML,
  override the speed scale and check output/header/covariance plus orderly
  SIGINT shutdown with each runtime backend. The enabled case composes the
  existing root bridge plugins; no component depends on that root label.

The adapter supports only the currently needed executor pair. Extend it when a
current package needs another pair. Resolve rule-owned template and dependency
labels in the wrapper module. Reuse RCR component record generation and ament
resource rules instead of introducing another plugin registry. Keep component
source files unchanged until a build or behavior check demonstrates a need.

Validation order (build pause lifted; proceed sequentially):

1. Build the converter's complete target surface with Cyclone DDS.
2. Run both existing targets with no-agnocast, requiring all six cases and no
   dependency-related skips.
3. Run its standalone RCLCPP behavior test.
4. Prepare the existing kernel/heap-hook/bridge prerequisites and run the
   standalone enabled behavior test without preloading the Python driver.
5. Update the same journal entry with real results, commit the validated module
   and its necessary wrapper/root support, then prepare stop filter and
   twist2accel as separate module tasks. Their inspected test inventories are
   seven and twelve cases respectively; enumerate their required behavior
   checks before writing their definitions.

All three package gates passed with Cyclone DDS: six converter, seven stop-filter
and twelve acceleration cases, with no skips, plus eight standalone cases
across RCLCPP and Agnocast (both acceleration input modes). Shared dependencies
were built once and reused; native children alone received heap preload.
The installed 2.4.1 kernel/device is accessible from host execution. No source
compatibility patches were required. RCR debug messages 1.16.0-1.rcr.0 and
nav_msgs 5.9.3-1.rcr.0 are validated inputs for stop filter. The expanded bridge
library required a costly initial compile; subsequent node checks reused it.

Package test sources live under test/, alongside their non-Bazel peers. Root
integration test targets and runtime composition share bazel/tests/.
Each validated package retains a separate commit and reproducible journal entry.
No Fast DDS, broad communication matrix or container sweep was run.
Phase 5.2.2 follows this slice.

#### Phase 5.2.2: Localization (complete)

Proceed in package boundaries: localization utilities, gyro odometer, EKF
localizer, geography utilities and GNSS poser. Introduce remaining TF/message
providers only when the selected source requires them. Keep the full existing
RCLCPP suites, including tests CMake omits when Agnocast is enabled; add targeted
enabled standalone checks for each node as in Phase 5.2.1.

All five package gates passed sequentially with Cyclone DDS:

| Package | Complete existing suite | Additional runtime gate |
| --- | --- | --- |
| autoware_localization_util | 33 cases across four targets | Existing suites exercise the library behavior. |
| autoware_gyro_odometer | 35 fusion/diagnostics/node cases | One standalone case per backend: all four outputs, covariance and healthy diagnostics. |
| autoware_ekf_localizer | 49 C++ cases across ten targets | Original XML launch behavior and exit assertions under each backend. |
| autoware_geography_utils | 21 cases across controlled grid-present/absent targets | Real pinned EGM2008 grid; no dataset-related skips. |
| autoware_gnss_poser | 44 node/characterization cases | One standalone case per backend with real geoid height conversion. |

This totals 182 C++ cases plus eight standalone/launch assertions, with
no failures or dependency-related skips. Keep the existing RCLCPP fixtures under
no-agnocast; enabled node behavior is validated separately. The only source
correction is EKF launch discovery synchronization in 5515a02e; production node
code is unchanged. Each package has its own integration commit and journal.

Validated inputs added for these targets are tf2_eigen 0.45.10-1.rcr.0,
sensor_msgs 5.9.3-1.rcr.0, RCR Lanelet2 extension 0.7.2-3.rcr.2 and its
transitive Lanelet2 1.2.1-8.rcr.2, plus the official egm2008-1 archive pinned by
SHA256 in the geography module. Reuse rosdistro's existing GeographicLib provider.
The real grid is a declared runtime/test resource, not a host install. Geography
exports geoid_grid and runtime_dynamic_deps for its consumers; no metarepository
label enters the component graph. Root bridge selection adds only the needed
Imu, DiagnosticArray and NavSatFix interface closures.

No Fast DDS, broad communication matrix or container rerun was required.
Phase 5.2 and Phase 5 overall remain in progress; the next slice after
point-cloud sensing/CPU perception is Phase 5.2.4 map loading/localization.

#### Phase 5.2.3: Point-cloud sensing and CPU perception (complete)

Use package gates in dependency order, reusing existing geometry, TF,
vehicle-info, wrapper and message support. Start with point types (33 existing
cases across two targets), then debug utilities (six static test declarations
across five case files plus their main) and crop-box filtering (ten algorithm
cases and one integration case). Add PCL utilities when a selected consumer
requires them (seven existing cases), then downsampling (twelve), ground
filtering (24), CPU clustering/object detection (25) and object conversion
(six). Inventory parameterized/expanded case totals and resource/runtime
requirements per module before implementation; these counts are source
inventories until commands validate them.

Point types uses only PCL common and the published point-cloud message wrapper;
do not pull the broad PCL shared provider into this first header-only boundary.
Add PCL conversion/filter/search providers when later nodes actually use them.
Select pinned Bazel dependencies instead of host apt/pip/rosdep installations.
CUDA targets and GPU behavior remain Phase 6. These particular filter/perception
nodes currently register ordinary RCLCPP components, so preserve their declared
backend rather than converting them to Agnocast as part of build adoption.
Run each complete existing suite with Cyclone DDS plus standalone/component
behavior appropriate to its executable outputs. No automatic Fast DDS or full
communication-matrix gate is added.

Point types passes all 33 cases with no skips using PCL common and the published
point_cloud_msg_wrapper 1.0.7-6.rcr.2. This pin is validated; no source patch or
broad PCL provider was needed. Debug utilities also pass all six existing
cases, validating autoware_internal_msgs 1.16.0-1.rcr.0 and the utility
1.4.2-4.rcr.2 counterpart with a local override. Crop-box filtering also passes
all eleven existing cases and one standalone cloud/polygon/published-time
check, preserving its ordinary RCLCPP component. Downsampling passes all twelve
existing cases and both standalone voxel/random behavior checks. Its native
expected header/manifest transition matches AWF core PR #1539; no algorithm
change is required by the pinned C++23 toolchain. Pinned pcl_conversions
2.10.0-1.rcr.0 and pcl_ros 2.10.0-1.rcr.1 provide the tested CPU PCL closure.
The local-only autoware_test_utils prerequisite also passes all 21 existing
cases plus its snapshot-executable service/YAML check; explicit fixture-owned
ROS shutdown fixes an observed process-exit failure. Ground filtering passes
all 24 existing cases and a standalone obstacle/published-time check after
portable float-constant and message-filter QoS fixes. Its existing rcpputils
expected header is available; no extra expected transition is needed. CPU
clustering/object detection passes all 25 existing cases plus both executable
centroid/debug-cloud checks without source changes. Object conversion passes
all six existing cases and its real executable field/UUID/twist check without
source changes.

Completion gate: 138 existing C++ cases across the eight package boundaries
above, plus eight standalone executable checks, all with Cyclone DDS and no
skips. Reuse the recorded module results; this is not an aggregate middleware
matrix or evidence for CUDA/GPU behavior. Source compatibility fixes remain
separate commits, associated with upstream where a matching fix exists. Phase
5.2.4 map loading/projection and map-dependent localization is next; introduce
its prerequisites only when the selected target needs them.

Source inspection finds no autoware_utils_pcl include/dependency in the selected
sensing/perception packages. Defer that utility's local adoption until an actual
consumer (such as NDT in Phase 5.2.4) requires it; this group uses native PCL
and conversion providers directly where its sources need them.

#### Phase 5.2.4: Map loading and map-dependent localization (in progress)

Start with map projection, whose current source requires component-interface
specifications. Neither package has a published RCR module, so introduce local
modules in dependency order. Preserve the specifications package's full domain,
concept, manifest, type-hash and C++17 compatibility tests, including its opt-in
hash freshness check. Do not silently skip or refresh a dependency-skew failure.
Then validate the projection loader's two C++ suites and four original launch
suites with RCLCPP, plus relevant enabled Agnocast behavior. Reuse the existing
Lanelet2 extension/geography providers until a current consumer exposes a need
for local source adoption. Continue with both map loaders, height fitting and
map visualization, then NDT scan matching and pose initialization after their
actual prerequisites. Inventory each package's complete suite before coding it.
The specifications prerequisite passes all 30 existing cases, including C++17,
manifest regeneration and explicitly enabled type-hash freshness against its
unchanged committed fixtures. No source patch or fixture refresh is needed. Projection loader also passes
all fourteen C++ cases, four original ROS launch suites (eight cases), and both
RCLCPP/Agnocast publication probes without a production source patch. Its enabled
gate uses the existing MapProjectorInfo bridge plugin and built heap hook.
The map loader requires the unpublished local Lanelet2 utility library next.
Its first build exposes a missing current traffic-rule header in the newest
published extension 0.7.2, so select the local 1.3.0 extension snapshot. Preserve
the published target labels and Lanelet 1.2.1-8.rcr.2 family. All19 active extension
cases pass, plus 100 cases in its existing complete geography/projection/GNSS/test-
helper consumer suites, original projection launches, snapshot behavior and both
GNSS/projection backends. No extension source patch is needed. The utility's full 173-case gate now passes across all eleven suites. A focused
test-only fix replaces unspecified nearest-lanelet ID ties with distance/geometry
assertions; production selection stays unchanged. Plotting examples and Python
maintenance tools remain separate consumers from this compiled-library boundary.
Both map loaders now pass the complete 55-case C++ suite, both original launch
suites (four cases) and four executable pub/sub/service checks across RCLCPP and
Agnocast. No map-loader source patch is needed. Preserve the full C++ RCLCPP
contract rather than omitting its conditionally excluded node/service tests;
enabled behavior uses the real executable/hook/bridge. Height fitting and map
visualization are next. The legacy external-API map-hash Python node remains
Phase 5.3 system/API composition, separate from these two C++ loaders.
The central-interface dependency refactor is complete before height fitting.
It preserves the existing 22 metadata roots and 21 C++/shared provider pairs in
one declaration list, forwarding providers through all_interfaces. Narrow
sensor/diagnostic selections remain unchanged. Template checks, full generated
plugin compilation/linking and all eleven bridge-plugin runtime cases pass with
Cyclone DDS, with no skips. The manifest contains 207 interfaces (180 messages,
27 services); service-event factories remain included.

Height fitting is complete: all fifteen kernel and two fitting/TF cases pass,
as do whole-cloud and partial-service executable probes in both backends.
The opt-in MultiThreadedExecutor fallback preserves its CMake contract while
existing callers retain SingleThreadedExecutor. The internal-localization
1.16.0-1.rcr.0 pin is validated by these builds and service checks. Its consumed
PoseWithCovarianceStamped service and the parameter interface family are
registered only in the central declarations; AsyncParametersClient needs all
six parameter services. The expanded manifest has 229 interfaces. Wrapper
regressions pass (37 C++ cases per backend and 66 Python cases), along with all
eleven bridge-plugin cases. No duplicate plugin dependency list is required.
Map visualization is the next package boundary; Phase 5.2.4 remains in progress.
Keep package commits separate and heavy builds sequential; routine Cyclone DDS
only. Phase 5.3 system/API composition and Phase 6 GPU integration stay separate.

## Later phases

### Incoming phase identifiers

- **Phase 6 — CUDA and TensorRT on x86.** Introduce pinned Bazel dependencies
  when a selected package requires them.
- **Phase 7 — Runtime packaging and headless planning simulation.** Complete
  install/resource closure and exercise the packaged scenario.
- **Phase 8 — JetPack 7.2.1 software compatibility (Thor-supported release).**
  Cross-build the selected aarch64 targets against a pinned JetPack-compatible
  user-space dependency set and check binary architecture, link/resource
  closure, and applicable non-GPU behavior where runnable. Hardware execution,
  GPU behavior, drivers, performance, and real-time characteristics remain
  unverified until suitable hardware becomes available; they are not Phase 8
  completion requirements.

The Phase 8 planning baseline is JetPack 7.2.1 / Jetson Linux 39.2.1, with
Ubuntu 24.04, CUDA 13.2.2, and TensorRT 10.16.2. NVIDIA lists Thor among its
[supported platforms](https://developer.nvidia.com/embedded/jetpack/downloads)
and records the release in the
[JetPack archive](https://developer.nvidia.com/embedded/jetpack-archive)
(checked 2026-11-15). This is a researched target, not a validated Bazel input
or a claim of working Thor deployment. Pin and validate the actual SDK inputs
when Phase 8 begins; any baseline change must be explicit and journaled.

These are scope markers, not implementation decisions. Refine each phase only
when the preceding phase meets its exit criteria.

From phase 1 onward, runnable slices use Cyclone DDS by default. Propose Fast
DDS compatibility checks at major milestones after the Cyclone DDS exit
criteria pass; run them only after explicit user approval.

Potential topics such as OSQP compatibility, OpenMP, CMake parity,
runtime-image deduplication, and remote caching will be evaluated when a current
target requires them.

## Commit and publication policy

Use signed-off Conventional Commits and validate each task in proportion to its
behavior. Work directly on local branch `bazel` and push validated commits to
`paulsohn/bazel`. Use force-with-lease only when intentionally replacing remote
history and only against an explicitly observed remote SHA.
