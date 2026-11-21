# Autoware + Bazel integration experiment

## Goal

Build a useful Autoware subset with Bazel on Ubuntu 24.04 x86_64, using ROS 2
Lyrical packages from the ROS Central Registry where practical. The experiment
should produce pinned, repeatable builds and a relocatable native runtime
bundle, with thin OCI packaging of the same application closure, and eventually
a planning simulator working through AWF's documented user workflow, backed
by repeatable headless behavior checks. Native building and execution should
not require a container. After the x86 experiment, target software build
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

## Before starting your integration

Use this checklist before the first BUILD file in a non-Bazel checkout. Follow
[the working rules](../../AGENTS.md#working-rules) and the current decisions below;
the linked journal entries explain their evidence without repeating it here.
Historical entries describe the conventions used at the time. Apply current
conventions from the outset, but introduce technical dependencies and patches
only when your current target demonstrates the need.

- [ ] Select local dependency snapshots and inventory missing Bazel support.
  [Root declaration layout](DEVELOPING.md#dependency-conventions);
  [published external providers](JOURNAL.md#2026-11-22-phase-732--published-angles-dependency);
  [evidence: local interface adoption](JOURNAL.md#2026-11-17-local-interface-adoption--autoware_adapi_v1_msgs).
- [ ] Establish source-preservation and added-test locations.
  [Evidence: tests outside component packages](JOURNAL.md#2026-11-17-keep-added-integration-tests-outside-component-packages).
- [ ] Separate component ownership from product-level composition.
  [Dependency layers and root selection](DEVELOPING.md#repository-dependency-layers);
  [evidence: reusable dependency relocation](JOURNAL.md#2026-11-18-reusable-dependency-layers-under-third_party).
  [Deployment-selectable asset convention](../../assets/README.md);
  [evidence: map relocation](JOURNAL.md#2026-11-21-deployment-asset-boundary-and-map-provider-relocation);
  [logical asset dependencies and colcon](JOURNAL.md#2026-11-21-general-map-asset-identity-and-colcon-resource-installation).
  [Evidence: root-owned interface consumers](JOURNAL.md#2026-11-15-build-root-owned-typed-agnocast-plugins-from-the-interface-graph).
  [Canonical interface identity](DEVELOPING.md#interface-declarations-and-bridge-plugins);
  [evidence: nested source categories](JOURNAL.md#2026-11-21-phase-732--nested-interface-source-namespaces).
  [Native plugin registration](DEVELOPING.md#native-plugin-libraries);
  [combined-plugin type identity](JOURNAL.md#2026-11-22-phase-732--distinct-motion-plugin-internal-types);
  [node-owned parameter lifetime](JOURNAL.md#2026-11-22-phase-732--cruise-pid-plugin-reload);
  [multi-package dependency identity and link closure](../../third_party/grid_map/README.md);
  [evidence: stop-line plugin](JOURNAL.md#2026-11-17-phase-525--stop-line-plugin).
- [ ] Preserve executable contracts and unique library identities in composed indexes.
  [Runtime resource convention](DEVELOPING.md#launch-resource-composition);
  [Python entrypoint evidence](JOURNAL.md#2026-11-22-phase-732--registered-python-executable-closure);
  [published container providers](JOURNAL.md#2026-11-18-phase-531--default-ad-api);
  [evidence: original core launches](JOURNAL.md#2026-11-17-phase-525--core-planning-launch-composition).
  [Runtime-only dlopen resources and ABI closure](DEVELOPING.md#tensorrt-sdk-and-native-tests);
  [evidence: TensorRT SDK](JOURNAL.md#2026-11-18-phase-62--tensorrt-binary-sdk).
  [Vendor C++ ABI boundaries](DEVELOPING.md#sparse-sdk-and-tensorrt-plugins);
  [evidence: sparse SDK](JOURNAL.md#2026-11-19-phase-641--sparse-convolution-sdk-compatibility).
- [ ] Declare caller-owned resources for dynamic runtime package selection.
  [Launch resource convention](DEVELOPING.md#launch-resource-composition);
  [evidence: global loader](JOURNAL.md#2026-11-18-phase-532--global-parameter-loader).
  [Indexed tutorial resources](DEVELOPING.md#tutorial-vehicle-and-map-resources);
  [evidence: official map](JOURNAL.md#2026-11-21-phase-733--official-tutorial-map-and-scenario-resources).
- [ ] Preserve original suites' source-layout and tool prerequisites in declared fixtures.
  [Test adapter convention](DEVELOPING.md#original-test-source-layout);
  [evidence: interface lint](JOURNAL.md#2026-11-18-phase-534--interface-spec-lint).
  [Evidence: textually included C++ implementation](JOURNAL.md#2026-11-22-phase-732--static-obstacle-avoidance-scene-plugin).
- [ ] Inventory complete module suites, runtime prerequisites and required
  middleware configurations before implementing each task.
  [Backend executor convention](DEVELOPING.md#switchable-standalone-nodes);
  [evidence: complete Agnocast test inventory](JOURNAL.md#2026-11-15-phase-41--adopt-the-complete-agnocast-library-test-inventory).
- [ ] Declare solver generation and its tool/runtime inputs in the build graph.
  [Developer convention](DEVELOPING.md#mpc-solver-generation);
  [evidence](JOURNAL.md#2026-11-22-phase-732--acados-native-and-code-generation-provider).
- [ ] Declare embedded-Python runtimes and preserve interpreter/object ownership
  through shutdown. [Embedding convention](DEVELOPING.md#embedded-python-and-plotting-tests);
  [evidence: model lifetime](JOURNAL.md#2026-11-21-phase-731--embedded-model-interpreter-lifetime).
- [ ] Define service readiness, dependency-loss policy and ordered shutdown.
  [Supervisor convention](DEVELOPING.md#s6-core-image);
  [evidence: supervised core](JOURNAL.md#2026-11-21-phase-722--s6-core-service-supervision).
- [ ] Separate builder-host, build-environment and runtime prerequisites; declare
  runtime resources once and reuse them for packaging.
  [Packaging and deployment requirements](#runtime-packaging-and-deployment-requirements);
  [distribution convention](DEVELOPING.md#native-core-distribution);
  [evidence: native runtime closure](JOURNAL.md#2026-11-20-phase-71--native-headless-core-distribution).
  [OCI image convention](DEVELOPING.md#oci-core-distribution);
  [evidence: payload and image execution](JOURNAL.md#2026-11-20-phase-72--thin-oci-core-distribution).
- [ ] Separate immutable release/domain resources from persistent vehicle records,
  and define activation/state compatibility before updating them.
  [Application update convention](DEVELOPING.md#ostree-application-distribution);
  [evidence: local update/rollback](JOURNAL.md#2026-11-21-phase-724--ostree-application-tree-distribution).
- [ ] Define editable model modules and portable runtime resource paths together.
  [Model repository/path convention](#model-repository-and-runtime-path-convention);
  [evidence and required experiment](JOURNAL.md#2026-11-18-model-subtree-layout-and-portable-launch-path-requirements);
  [validated model storage/index pattern](JOURNAL.md#2026-11-19-phase-644--repository-owned-yolox-models).
- [ ] Inspect build-system-only shims before creating empty Bazel modules.
  [Working rule](../../AGENTS.md#working-rules);
  [evidence: CUDA foundation](JOURNAL.md#2026-11-18-phase-61--cuda-utils-and-pinned-toolkit).
- [ ] Check dependency API and header-layout compatibility before changing source.
  [Evidence: solver input selection](JOURNAL.md#2026-11-17-phase-525--osqp-interface).
  For interface changes, check generated wire types and compiled consumers.
  [Developer convention](DEVELOPING.md#editing-interface-packages);
  [evidence](JOURNAL.md#2026-11-21-phase-732--generated-interface-wire-checks).
- [ ] Choose validated package boundaries for commits, keeping source fixes
  separate from build declarations.
  [Evidence: first local package module](JOURNAL.md#2026-11-14-build-the-first-local-autoware-package-module).
- [ ] Prepare a reproducible task record, including commands and observable
  results, and maintain this checklist as lessons become general rules.
  [Evidence: independently reproducible journal](JOURNAL.md#2026-11-14-make-the-experiment-journal-independently-reproducible).

## Current decisions

- With arrows meaning "supplies inputs to", keep dependency direction
  (BCR/RCR modules + third_party/) -> src/ -> bazel/, plus assets/ -> src/ and bazel/.
  Deployment-selectable resource providers and metadata live under assets/;
  the current git-tracked map module owns its index, archive pin and overlay.
  The root selects that module with an adjacent bazel_dep/local_path_override
  pair and retains registry version selection. The external-injection/deployer
  design is only a [sketch](../../assets/README.md), not a new deployment milestone.
  Product composition, standalone integration tests and interface collection
  belong to bazel/; component definitions remain in src/; reusable dependency/SDK support and
  patches belong to third_party/. Direct product-to-third-party dependencies
  are allowed; reverse dependencies are not. Assets may use generic build rules
  but must not depend on application implementations or product targets.
  Asset module/resource package names use the _asset suffix. Components may
  consume their public targets/indexes through logical module dependencies;
  the root selects the concrete repository/revision.
  Source-owned resources and model repository ownership remain unchanged.
  The root owns version selection from third_party/, registry releases and
  local source overrides and architecture/product configuration. See
  [dependency layers](DEVELOPING.md#repository-dependency-layers).

- Keep existing source packages unchanged except for Bazel definitions and
  required build support. Source/original-test changes need a demonstrated
  integration/testing blocker (including Lyrical/Clang compatibility) or a
  genuinely incorrect implementation. Put all new tests, adapters and fixtures
  under bazel/tests/<package>/; leave upstream test sources in place. Root
  composition consumes package targets without package-to-root dependencies.

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
  under `third_party/rclpy/`. Its extension must use the existing ROS/DDS shared
  targets, so importing rclpy alongside rosbag does not duplicate DDS globals
  and corrupt interpreter shutdown. The CLI suite verifies normal exit;
  UPSTREAM_RCR.md records the published overlay and reproducible evidence.
- Committed Bazel inputs include the module lockfile. Registry ordering and the
  read-only RCR CI cache are repository configuration.
- Required dependencies present under src/ use their local snapshots. Add
  missing package-local Bazel definitions transitively, including message
  packages, rather than selecting an older registry version with an overlay.
  Registry sources supply dependencies absent locally; explicit source-selection
  comparisons are separate experiments requested by the user.
- A local Autoware ROS package is a package-local Bazel module selected by a
  root `local_path_override`; its root bazel_dep uses 0.0.0, with version
  metadata retained in the package-local module.
  Registry comparisons require explicit user direction and compatible temporary
  version selection without changing consumer BUILD files.
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
- For wrapper-derived nodes, Agnocast is compiled into Bazel targets
  unconditionally. `ENABLE_AGNOCAST` is a runtime choice: Bazel run and test
  actions select Agnocast by default, while `--config=no-agnocast` selects the
  RCLCPP backend from the same binaries. Existing plain rclcpp::Node components
  retain their native ROS contract and use the RCR's native component launcher;
  their validation selects RCLCPP without claiming Agnocast support.
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

- Package source selection belongs in the root module. Phase 1 compared local
  and registry sources through overrides. Current adoption uses root version
  0.0.0 for local modules; registry comparisons require an explicit source and
  version selection (see Current decisions).
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

## Phase 5: Remaining autoware_core runnable slices (complete)

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

### Phase 5.2: Runnable dependency slices (complete)

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

Original package tests remain alongside their source. Added tests, adapters
and fixtures live under bazel/tests/<package>/ with root runtime composition.
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

#### Phase 5.2.4: Map loading and map-dependent localization (complete)

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
Map visualization is complete: all seven existing C++ cases, the original
launch suite, both executable publication/late-subscriber checks and eleven
bridge-plugin cases pass. MarkerArray is selected centrally; the expanded
manifest contains 234 interfaces (198 messages, 36 services). The original
SingleThreadedExecutor fallback and full RCLCPP test contract are preserved.
No production source compatibility patch is needed.

Continue in dependency-related work groups, preparing and validating prerequisites
and their consumer in one step. Retain package-scoped completion gates and
separate eventual commits. The NDT group integrates autoware_utils_pcl
and autoware_ndt_scan_matcher together. All seven PCL utility cases and all
52 NDT C++ cases across seven targets pass, together with the original two-case
NDT launch suite and actual activation/map-loading/alignment/shutdown in both
backends. Use the pinned LLVM 21.1.8 OpenMP library and preserve the x86 SSE4.2
contract on kernel/node/tests. The transitioned LLVM target supplies headers
but its linker inputs are lost through the native shared-library graph aspect;
a package-local cc_import of its declared archive restores native ownership and
passes compilation, linking and both runtime gates. No host OpenMP dependency,
production source patch or central-interface expansion is required.
Pose initialization is the current three-package group: component-interface
utilities, motion utilities and the pose initializer. Their complete existing
C++ gates pass (25, 118 and 23 cases). Preserve the manifest fragments, service/
subscription registration and introspection checks, and motion benchmark case.
The callback adaptor needs a current Clang/libc++ compatibility fix: move its
named member-function pointer into std::bind so RCLCPP can match its traits.
Keep this source correction separate from eventual build-definition commits.
The executable gate checks DIRECT/AUTO initialization, reset/state publication,
trigger order, empty-input rejection and clean exit in both backends. Register
only LocalizationInitializationState from the ADAPI package centrally and run
the expanded bridge's full eleven cases. Both executable gates pass, including
trigger sequencing and state/reset publication through ordinary ROS peers, and
all eleven bridge regressions pass. The central manifest contains 235 interfaces
(199 messages, 36 services). Phase 5.2.4 is complete. Phase 5.2.5 planning/control is next.
Keep package commits separate and heavy builds sequential; routine Cyclone DDS
only. Phase 5.3 system/API composition and Phase 6 GPU integration stay separate.


#### Phase 5.2.5: Planning and control (complete)

The first related group integrates native control endpoints using the already
validated motion and component-interface utilities: autoware_adapi_specs,
autoware_command_gate and autoware_simple_pure_pursuit. Both executables derive
from rclcpp::Node in the current source. Preserve that contract with the RCR's
rclcpp_components_register_node macro, shared component metadata and native
single-threaded launcher rather than introducing a middleware source change.
Use the checked-in ADAPI message snapshots at 1.9.2 and add their missing
package-local generator declarations. The registry overlay is not a substitute
for local sources. The same audit selects local internal debug, localization
and legacy messages at 1.16.0; validate their affected consumer closure.

The complete 18-case command-gate and 13-case pure-pursuit suites pass, including
real component discovery/loading. Actual executable checks pass for four mode
changes and rejection of an unknown mode without publishing, and for straight
tracking, terminal braking, empty-trajectory fallback and lateral correction.
Existing package indexes expose the Python peers' C shared/type-support closure;
tests explicitly set LD_LIBRARY_PATH=lib. No additional runtime index is needed. No production source patch or change
to the central Agnocast interface set is required. These targets are native
RCLCPP/Cyclone DDS consumers. Local provider selection is also covered by the
root regression suite and all eleven bridge cases.

The next related group is autoware_pyplot, the optional visualization export
from autoware_test_utils, autoware_trajectory and autoware_route_handler. Preserve
PLOT compilation and enable trajectory's existing ENABLE_TEST_PLOT switch.
Acceptance includes pyplot's original test, all 20 trajectory test sources,
all six route-handler test sources, all 19 CMake-declared trajectory examples,
and the existing test-helper suite. Preserve route-handler's one upstream
DISABLED_ case and report it separately; add no filters or dependency skips.
Use a root-owned embedded-Python launcher with the selected Python toolchain,
pinned Matplotlib wheels and Agg rendering. Tests write figures to Bazel's
undeclared-output directory. Keep new harness sources outside components.
This group is complete: pyplot's one case, all 293 trajectory cases, 158 active
route-handler cases (one upstream-disabled case retained), all 21 test-helper
cases and all 19 trajectory examples pass. Trajectory and route-handler each
produce 87 SVG figures; pyplot produces two PNGs and one SVG. The GNU wheel
runtimes are pinned Bazel archives, with native loader tracing confirming their
use. Trajectory keeps its upstream C++17 implementation contract and needs
current-target expected/TF header fixes plus a four-ULP floating-point test
comparison; the journal and UPSTREAM_AWF.md record scope and evidence.
The next larger group covers six packages: autoware_planning_test_manager,
autoware_planning_factor_interface, autoware_objects_of_interest_marker_interface,
autoware_planning_topic_converter, autoware_path_generator and
autoware_mission_planner. Inventory their complete existing suites together:
two manager targets, one factor target, one marker target, two converter targets,
one five-source path-generator target and four mission-planner targets. Preserve
original test mains, assertions and resource/configuration inputs. Validate
planning-factor in both wrapper backends; the other components retain native
RCLCPP. Root-owned checks exercise all four produced executables. Select the
RCR generated-parameter macro for the path generator's original YAML schema,
using Bazel-resolved Python tools rather than host generation or a handwritten
replacement header. The accepted input is generate_parameter_library
1.3.0-1.rcr.0, matching the current RCR snapshot; its Python generator and native
runtime helpers pass generation, compilation and executable parameter checks.
All 108 original cases across 11 targets pass with Cyclone DDS. Planning-factor's
seven cases additionally pass with ENABLE_AGNOCAST=1 and the built heap hook;
its corrected test uses the actual backend executor. Four root-owned executable
checks also pass. The group is complete, with no skipped/disabled cases. Source
changes are limited to the demonstrated mission-planner TF include blocker,
quaternion endpoint comparison and planning-factor callback dispatch in tests.
Keep each package and source compatibility fix in separate commits.
Next take the OSQP/QP and velocity-smoothing dependency group together, followed
by behavior/motion planner foundations and plugins.

The solver/smoothing group integrates autoware_osqp_interface,
autoware_qp_interface and autoware_velocity_smoother together. Preserve the
existing OSQP 0.6 API using BCR 0.6.3.bcr.3; multiple_version_override retains
RCR's 1.0.0.bcr.3 selection for its separate consumers. Package-local generated
forwarding headers supply the ROS vendor's osqp/ include namespace. This avoids
source/API changes while the current solver tests validate the chosen inputs.
Only the legacy ABI is linked into these consumers; combining both OSQP ABIs in
one process is not a supported result of this experiment. ProxSuite
0.7.3-1.rcr.0 supplies the QP interface's second backend.
The complete gate is both solver suites, all five CMake-registered smoother
targets plus its existing stop-velocity test file (omitted from CMake), and the
actual smoother executable under JerkFiltered, L2, Linf and Analytical in both
backends. Keep original C++ suites on RCLCPP, matching the upstream node-test
contract; use the prepared heap hook, kernel module and bridge composition for
the enabled executable gate. Do not omit original node-interface tests or turn
their RCLCPP-only acceptance into a claim of enabled C++ suite coverage.
This group is complete: eight OSQP-interface, 16 QP-interface and 67 smoother
C++ cases pass without failures/errors/skips/disabled cases. Each of the four
algorithms also passes through the actual executable in both RCLCPP and enabled
Agnocast modes, including velocity-limit acknowledgement and clean shutdown.

The next group covers local autoware_object_recognition_utils, behavior-planner
common, behavior planner, stop-line plugin and motion-planner common. Preserve
all eight object-recognition test sources, six behavior-common sources, two
planner test targets, three stop-line sources and three motion-common sources.
Preserve motion-common's original DISABLED_Benchmark separately.
Build both standard and experimental planner executables and the original
plugin XML's two factories. Native RCLCPP/Cyclone DDS is the selected gate;
root-owned executable checks must prove plugin discovery, initial loading,
unload/reload, original YAML parameter values and clean shutdown for both
factories. Keep test helpers outside the production library closure. This group
is complete: all 137 active existing cases and both executable/plugin lifecycle
checks pass, with no failures or added skips. Motion-common's one original
disabled benchmark remains separate. Source changes are limited to the proven
recognition-test TF include and behavior-common test shutdown corrections.
The experimental planner gate validates assembly/lifecycle; full experimental
scenario parity remains outside this gate. Next take the motion planner and
obstacle-stop plugin group with their actual dependencies.
The next group integrates motion velocity planner and its obstacle-stop plugin.
Keep the node's actual native RCLCPP contract despite its CMake wrapper macro.
The complete gate is both original planner targets (14 cases), all seven plugin
test sources (39 cases), and the real executable loading/unloading/reloading the
plugin with original config values and clean shutdown. Register original maps
and YAML fixtures, and expose the plugin index to the node suite. This group is
complete: all 53 original cases and the executable's active-duplicate rejection,
unload/reload, parameter and clean-shutdown check pass on Cyclone DDS. Reloading
needed one production correction: detect active module instances rather than
cached pluginlib classes. Source and build definitions remain separate commits.
Next inspect core planning launch/resource composition and the remaining
Phase 5.2.5 completion criteria; full scenario simulation remains Phase 7.
The final adoption gate composes autoware_core_planning and autoware_core_control
through their original XML and config resources. All six planning nodes start
together; control's default and command-gate-enabled branches pass separately.
The affected complete suites pass all 167 original C++ cases, seven existing
standalone checks and six new active/shutdown launch assertions. Original XML
needed the existing vehicle parameter file passed to three omitted consumers;
Bazel resource fixes preserve libexec names and unique component library names.
The journal records exact commands, failures, fixes and the related AWF PR.
All planning/control packages now have Bazel support. Phase 5.2.5 and Phase 5.2
are complete, reusing prior unchanged module/backend evidence. This gate does
not claim full scenario simulation, an Agnocast aggregate launch or a new
Fast DDS compatibility run.

### Phase 5.3: System and API composition (complete)

Inventory the current system/API executable dependency graphs and existing
suites, starting with local packages not yet exposed through Bazel. Introduce
only the selected runtime closure and validate complete package suites plus
relevant API/state transactions. Compose remaining real launch/resource
packages when a runnable consumer needs them; do not create artificial targets
for empty grouping metapackages. Keep system-designer packages out of scope.
Phase 5 is complete; runtime images and full driving simulation are
Phase 7, with GPU dependencies introduced separately in Phase 6.

#### Phase 5.3.1: AD API and RViz adaptors (complete)

Integrate local autoware_adapi_adaptors, autoware_default_adapi and the real
core API launch/resource package together. Preserve all 13 adaptor-helper and
15 API-conversion cases, both custom test mains and the original interface-
version launch suite. Register the five original wrapper executables, with
MultiThreadedExecutor fallbacks where CMake selects them, and package-qualified
physical component names. Compose the published container runtime provider
with wrapper launch support. Introduce no manifest-only Flask/global-parameter
runtime when the selected source path does not use it.

The complete native suites and original launch pass. The original core API XML
also passes routing/localization/adaptor transactions and clean shutdown in
both RCLCPP and enabled Agnocast configurations, with process/preload assertions
proving the three-process container and five-process standalone paths. No
source fix, new dependency version or product-interface selection was needed.
Global/model context and the separate loader-based test XML are covered in
Phase 5.3.2. Full vehicle behavior is not established by these API fixtures.

#### Phase 5.3.2: Global parameters and remaining core launch context (complete)

Select global-parameter/model resources and remaining real core launches from
their executable/parameter dependencies. Prefer local descriptions and introduce
xacro or other providers only when the selected launch actually needs them.
Inventory complete existing suites and declare configuration/state gates before
implementation. Keep empty grouping packages and system-designer out of scope;
full vehicle simulation remains Phase 7.

The current group covers six local resource modules: sample vehicle description,
global parameter loader and core sensing, perception, map and localization.
They have no registered runtime suites. Their declared acceptance is the
original combined XML/Python context with real model and map resources, global
vehicle/clock inheritance, map publications, typed sensing-to-localization relay,
component discovery and clean exits, with use_sim_time false and true. A
path-generator probe receives vehicle data solely through the global loader.
The original relay and typed data gate validate the published topic_tools
1.5.0-1.rcr.2 runtime provider for the absent local dependency. Pin that release
and its lockfile inputs. All six native test targets passed; the loader-based
API XML also passed with enabled Agnocast and verified process/preload behavior.
No source patch was needed. Clock selection is tested without advancing /clock.
A possible future improvement is a model-specific parameter-loader target for
each vehicle model. Each target would fix the model choice and explicitly depend
on that description's resource index, making the model dependency static and
letting applications select it through a Bazel label. This adds per-model
boilerplate; a shared helper could retain common loading logic. Keep this as an
unimplemented option while the current loader accepts runtime model selection.
Vehicle/sensor description rendering and the top-level GUI entry point remain
separate investigation boundaries; this group does not assert full driving.

#### Phase 5.3.3: Vehicle and sensor descriptions (complete)

Inspect the remaining local core resource packages and original top-level launch
includes. Identify the minimum vehicle/sensor description rendering consumer and
its complete tests before introducing xacro providers. Keep dynamic model choices
explicit in the product bundle and preserve original packages. Separate headless
resource/launch closure from GUI execution and Phase 7 full simulation.

The module group is local velodyne_description, both local sensor-kit
descriptions and autoware_core_vehicle. Their original resource-only CMake
packages register no runtime suites. The gate includes the original vehicle
XML for each sensor kit, requiring rendered URDF, resolvable mesh resources,
calibrated static transforms and clean shutdown. The original vehicle launches
validate RCR xacro 2.1.1-3.rcr.3 and
robot_state_publisher 3.5.6-1.rcr.0; pin both inputs. Both sensor-kit variants
passed rendered URDF, mesh lookup, static transform and shutdown assertions.

#### Phase 5.3.4: Top-level headless launch and final package closure (complete)

Finish the six remaining local packages: autoware_component_interface_admission,
autoware_marker_utils, autoware_interface_spec_lint, autoware_test_node,
autoware_testing and the top-level autoware_core resource package. Preserve
three original admission suites, the original marker-conversion suite (including
its custom main), and all original interface-lint pytest files, supplying real
sibling specs/configuration and the built manifest generator rather than skips.
Exercise the example node's original launch and native component registration;
run autoware_testing's original generic smoke suite against that real executable.
No component code or Python test needs a metarepository label.

The original top-level core launch unconditionally starts RViz. Demonstrate that
headless startup is blocked, then add a default-on launch_rviz option as a
separate source fix. Run that original launch with RViz disabled and all eight
functional groups enabled: real model/map resources, expected node graph,
vehicle parameters, typed sensing/localization relay, planning/control graph,
API version request and clean shutdown. Cyclone DDS/native RCLCPP is the declared
aggregate configuration; reuse earlier enabled Agnocast module/API evidence.
No Fast DDS run is authorized or required for this gate. GUI execution, driving
simulation, hardware/sensor drivers and image packaging remain later scope.

Close Phase 5 only after every package gate above passes without dependency
skips or deferred failures, the all-74-package declaration inventory is complete,
and source changes, definitions, journal and steady-state conventions are
committed at their respective boundaries.

All gates passed: 204 cases across ten test targets, with zero failures, errors,
skips or disabled cases. All 74 local core packages now have module/build
rules and root local selections. The complete original aggregate starts and
shuts down headlessly with all eight functional groups enabled; the default-on
launch_rviz option preserves the GUI launch default. The root Bazel index
currently supplies the headless closure; GUI consumers must add RViz runtime
resources explicitly. No full driving behavior or GUI rendering is claimed.
Prior unchanged Agnocast/module evidence remains applicable; no new middleware
matrix is required to close this phase. Phase 6 is next.

## Phase 6: CUDA and TensorRT on x86 (complete)

Bring the required GPU SDK and local CUDA consumers into the Bazel graph,
without host toolkit discovery or apt/pip installation. GPU drivers/devices
remain execution prerequisites. The foundations, CUDA transport and selected
consumer group below are validated on x86. Additional consumers and target
architectures remain separate experiments. Pin accepted versions and patches
only after current consumers pass their complete suites.

### Initial decisions and validation boundaries

- Keep the current LLVM/libc++ 21.1.8 C++ toolchain. First try nvcc with that
  hermetic host compiler; do not bundle a project-wide LLVM upgrade into CUDA
  adoption. Keep CUDA language flags local to the targets that require them.
  The accepted vendor spconv C++ ABI closure uses GNU 13.3.0 through a configured
  shared-library boundary; application code remains on libc++.
- Try CUDA 13.2.2, matching the JetPack 7.2.1 planning baseline. Fetch the NVIDIA
  [redistributable manifest](https://developer.download.nvidia.com/compute/cuda/redist/redistrib_13.2.2.json)
  and required component archives with checksums. Add SDK components when an
  actual consumer requires them, rather than reproducing a provisioning list.
- Evaluate upstream rules_cuda 0.4.0 against RCR's 0.3.0.rcr.0 before selecting
  the rule implementation. Inspect their toolchain/linking differences and
  exercise the current compiler/runtime closure. Do not pre-apply architecture,
  component-patching, CUDA-header or reproducibility patches.
- Start with explicit sm_120 code generation for the available x86 GPU. Keep
  that product selection separate from package rules. The broader architecture
  matrix, SBSA cross-build and sm_110 verification belong to Phase 8.
- Try TensorRT 10.16.1.11-1+cuda13.2 from checksum-pinned NVIDIA binary package
  payloads. Extracting .deb data archives through Bazel is acceptable and does
  not require apt installation. The public Ubuntu 24.04 x86_64/SBSA indexes
  checked on 2026-11-18 do not list 10.16.2, although JetPack 7.2.1 specifies
  it. Preserve this version gap explicitly; x86 validation cannot establish
  compatibility with the Phase 8 SDK. Do not assume an arbitrary patch-level
  substitution is equivalent.
- Declare TensorRT builder-resource libraries loaded at runtime, plus its
  required GNU runtime libraries. Audit GLIBC/GLIBCXX requirements and pin the
  user-space runtime closure instead of silently using host libstdc++/libgcc.
  Keep platform downloads lazy and runtime paths repository-mapping aware.
  Do not change the existing core ABI-floor claim based on an unchecked vendor
  binary. Engine generation/compatibility is specific to the execution target;
  do not assume an x86 test engine can be reused on Thor.
- Run complete original module suites on the actual GPU; inspect case counts
  and reject dependency-related skips or zero-case successes. Preserve existing
  test mains and any CUDA test-object registration needed by the linker.
  Separate successful compilation, GPU execution and reproducibility claims.
  Measure reproducibility before adding compiler wrappers or deterministic-output
  patches. The accepted CUDA/libc++ header corrections and their actual gates
  are recorded in Phase 6.1; later consumers establish additional needs separately.

### Phase 6.1: CUDA foundation and direct-provider experiment (complete)

Select the minimal pinned SDK/rules closure and integrate local
autoware_cuda_utils, including every original CUDA test source and its existing
main. First inspect autoware_cuda_dependency_meta's build exports: it contains
no implementation or runtime payload and its CMake extras add -fno-lto to C/C++
flags to avoid duplicate fatbinData symbols. Try direct rules_cuda toolkit,
CUDA runtime and CCCL dependencies in the consumer, without a separate Bazel
module for that shim. Inspect emitted compile/link actions for LTO and required
flags and pass the complete consumer suite, including Thrust coverage, before
accepting the omission. If a required effect is missing, express it in the
owning CUDA target/toolchain or add the smallest justified compatibility target.
Preserve the original package/manifest; this is a Bazel mapping experiment, not
permission to discard real local libraries, interfaces or package resources.

Accepted result: upstream rules_cuda 0.4.0, CUDA 13.2.2 redistributables and the
existing LLVM/libc++ 21.1.8 toolchain pass all 17 original CUDA-utils cases on
sm_120, with no skips. Only cccl, crt, cudart, nvcc and nvvm are needed here;
use shared cudart. Keep the toolkit/root architecture settings in
third_party/cuda/cuda.MODULE.bazel and .bazelrc, and the header library/tests package-local.
No dependency-meta Bazel module is needed for this validated non-LTO build.
The configured compile/link actions contain no LTO-enable flags; revalidate
that conclusion if LTO or device-link settings change.

CUDA 13.2 explicitly rejects libc++ on x86; the product CUDA flags acknowledge
that unsupported combination. A pinned CRT 13.2.86 override applies narrow
math-header compatibility patches after the observed pow/isnan/classification
failures. Use standard http_archive plus override_repo rather than forking
rules_cuda for a patch tag. The replacement exports only the required CRT
component file/header interfaces and preserves its vendor payload/license.
This is tested compatibility, not NVIDIA-supported libc++ or an SBSA result.
No rule, architecture, culibos or nvcc reproducibility patch was required by
this consumer. The complete native headless core regression also passed.

### Phase 6.2: TensorRT foundation (complete)

Integrate local autoware_tensorrt_common against the selected binary SDK and
explicit runtime closure. Expose the real ONNX fixture and run its complete
original suite, including engine builds on the GPU. Inspect any CMake-only
find-module/meta dependency for required effects before creating Bazel modules;
validate concrete headers, definitions, linkage and runtime behavior directly.

The declared gate is all 13 original TensorRT-common cases, using the real
checked-in dummy ONNX fixture and GPU engine builds, plus the complete unchanged
CUDA-utils suite as the affected SDK regression. Build the public shared-library
export, not only the static library. SDK inputs are the six pinned x86 NVIDIA
10.16.1.11-1+cuda13.2 header/runtime payloads, with declared builder-resource and
GNU runtime libraries; audit ELF ABI/dependency requirements. Try the existing
pinned GNU 12.2 runtime before changing it. Keep CMake-only finder/meta packages
out of the graph only if direct providers preserve their required effects.
New SDK/test support stays with its module; no model-repository import or DDS
matrix is part of this foundation gate.

Accepted result: the pinned x86 TensorRT 10.16.1.11-1+cuda13.2 SDK passes the
complete 13-case original suite through the real shared export, plus all 17
CUDA-utils cases. The SDK module owns six NVIDIA package URL/hash inputs,
shared imports, system header classification and runtime-only builder data.
The native test launcher uses resolved runfiles and pinned GNU 12.2 runtimes;
an engine-build process snapshot confirms those mappings. Inspected imported
libraries require at most GLIBC 2.27/GLIBCXX 3.4.22/CXXABI 1.3.11. Keep the
NVIDIA driver host-provided and leave runtime-image/platform validation later.

Two source corrections are required: profiler's direct `<sstream>` include and
filesystem-header availability detection. Record them separately from Bazel
rules and associate the useful findings in UPSTREAM_AWF.md. The CMake finder
and CUDA meta shim need no separate module for this direct-provider closure.
The 256-byte ONNX test fixture remains local; model-repository adoption is not
part of this gate. JetPack's 10.16.2 and SBSA are still unvalidated targets.

### Phase 6.3: CUDA transport (complete)

Integrate local negotiated interfaces/library/examples and cuda_blackboard in
dependency order, at package commit boundaries. Inventory all current original
suites and example/component behavior before implementation. Preserve native
versus switchable node contracts and require the specified Agnocast worker/
backend checks with their actual runtime prerequisites.

Package boundaries and gates:

- **6.3.1 — negotiated_interfaces (complete):** all four original messages, their nested
  dependencies and complete generated target surface; no original runtime
  tests are registered. Validate consumers in the negotiation-library gate.
- **6.3.2 — negotiated (complete):** both original sources, public/shared exports and all
  76 existing cases (12 combinations, 32 publisher, 32 subscription) with
  Cyclone DDS. Preserve actual negotiation/renegotiation and callback behavior.
  These are native rclcpp nodes, not Agnocast-switchable nodes.
- **6.3.3 — negotiated_examples (complete):** eight original component executables and
  the lifecycle publisher, original launch resources and representative
  standalone/composed, remapping, type-adapted and lifecycle behavior. There
  are no original runtime test targets; define added behavior gates before
  implementation and keep their adapters under bazel/tests/.
- **6.3.4 — cuda_blackboard (complete):** library, publisher/subscriber components and all
  eight original cases (four mailbox, two publisher, one async publisher and
  one worker announcement). Require real GPU execution with no device skips;
  the worker-announcement case explicitly selects ENABLE_AGNOCAST=1 and needs
  its IPC listener available. Scope backend and component behavior checks
  before implementation; enabled kernel-backed execution needs its actual
  module/device and heap hook. Runtime prerequisites are not build prerequisites.

Accepted results: all 52 example targets and ten delivery/shutdown scenarios
pass. Blackboard's eight original cases, enabled publisher/async repeats, exact
GPU image/point-cloud round trips and cross-process compatible image delivery
pass in both backends. The original composed image pipeline preserves pixels
and metadata after a separately validated demo-header fix. CUDA-utils' 17 and
TensorRT's 13 original GPU cases pass on NVIDIA 595.99.02. Direct Agnocast tests
use the declared heap hook with the bridge off; no bridge matrix is claimed.
Select cv_bridge 4.1.0-3.rcr.2 at root: its 0.0.0 placeholder has no BUILD rules.
The library/examples remain CPU-only; real GPU gates belong to CUDA consumers.

Keep each validated package boundary and its evidence in a separate commit.
No Fast DDS compatibility check is part of these routine gates.

### Model repository and runtime-path convention

Import model repositories as squashed Git subtrees under src/models/, retaining
upstream URL and immutable revision. Each repository owns one Bazel module,
one ament package index, its weights and associated metadata, and its own
MODULE.bazel/BUILD.bazel definitions. Package metadata and resource registration
stay inside that repository boundary; do not introduce a root-owned aggregate
model-data package. The root selects local modules and composes concrete bundle
targets as runtime dependencies. This exercises editable model-engineering
inputs without maintaining upstream forks. The same repository boundaries can
later be checked out independently through submodules or a hypothetical pinned
models.repos manifest. Keep that manifest idea as an option, not a mandatory
per-model Bazel adoption step. The first supported repository is tensorrt_yolox;
no models.repos manifest is implemented.

Add one package.xml and minimal ament_cmake CMakeLists.txt per imported model
repository, alongside its Bazel files, so both build paths share a package name
and metadata. package.xml enables colcon package discovery; Bazel can create an
ament-index marker without it, but shared metadata is the chosen convention.
The model repository need not already be a ROS package upstream. Validate
colcon discovery/build and indexed resource lookup with --symlink-install before
claiming support or that large model payloads are not copied. Colcon users must
obtain and build the model packages needed by their selected launch and source
the resulting environment. A future models.repos could select those checkouts;
Ansible hf downloads into HOME alone no longer supply the required indexes.
The tensorrt_yolox package validates this build/index pattern; apply it to each
subsequently selected model repository.

Choose subtree subpaths from existing model-data-directory-relative paths where
practical; decide remaining subpaths with each consumer. Special case: use
src/models/diffusion_planner without a version directory, importing only the
revision selected by autoware_launch. The inspected diffusion_planner.param.yaml
selects v5.0; confirm the active consumer when importing, pin its exact commit,
and omit unused per-version imports. Version identity belongs in provenance,
not this subtree's directory name. Preserve the upstream LFS/Xet storage format,
materialize actual model bytes and prove a fresh-checkout/build cycle, including
retrievable payloads for locally changed weights; importing pointers is insufficient.

Update every affected autoware_launch model-path argument/parameter with its
resource provider. Resolve each bundle exclusively through its own indexed
share directory, e.g. $(find-pkg-share <model_package>); source checkout paths
are not runtime paths. Remove the common model data_path argument and its
forwarding/defaults rather than maintaining a second external-directory branch.
Do not inject absolute workspace paths, retain HOME defaults or provide an
Ansible-directory fallback. Keep model files and associated metadata addressable
as one declared bundle, updating root and independently invoked nested launch
entry points. Diffusion resolves its single flat indexed bundle; no legacy
version-directory branch is required.

Consequence: this changes the existing Ansible hf-download-to-HOME plus colcon
workflow. Developers must also obtain the model repositories, materialize their
large files, colcon-build the packages and source their installation before
launching consumers. Missing model package indexes are missing required
dependencies, not a reason to fall back to HOME or reintroduce data_path. Declare
the corresponding model-package runtime dependencies in launcher packages.
Keep repositories/ and ansible/ reference-only: do not add mandatory
repositories/models.repos entries, remove artifact downloads or delete the role
as part of each Bazel model integration. Those roles remain unchanged, but their
HOME downloads alone do not satisfy the new index-only launcher contract.

A future models.repos must carry an explicit caveat: current upstream Hugging
Face refs do not contain the package.xml/CMake/build support added to our
subtrees. A raw checkout is therefore not automatically colcon-buildable; it
needs the corresponding metadata/support and validation. MODULE.bazel marks
Bazel adoption independently of that manifest. package.xml and a supported build
definition enable colcon discovery/build; successful validation establishes
compatibility. If a manifest is later introduced, test its checkout/materialized
payload/build-support procedure before claiming a working collaborative setup.

Each model task must test indexed lookup from a relocated/materialized
bundle, its complete consumer behavior, launcher runtime dependency declarations
and colcon build/symlink-install/environment sourcing. The diffusion migration
must additionally validate its flat single-revision layout.
A hypothetical manifest checkout is a separate future compatibility experiment,
not a required marker of Bazel adoption.
Initial inspection found 26 launch files with HOME model-root defaults and one
active diffusion version suffix. Adopt remaining model providers and their
affected launcher paths together; do not claim global argument removal before
all corresponding providers land. Phase 6.2 keeps its dummy ONNX fixture;
introduce pretrained model repositories when a selected consumer requires them.

### Phase 6.4: Representative GPU consumers (complete)

Select autoware_tensorrt_plugins, autoware_tensorrt_classifier and
autoware_tensorrt_yolox as a related GPU/inference group. Validate each complete
dependency closure and existing suite. Kernel-only reference checks may be an intermediate experiment,
but cannot close an otherwise partially integrated plugin package. Introduce
additional vendor/model dependencies only when that consumer needs them. Apply
the model repository/runtime-path convention above to the complete selected
consumer closure.

Package boundaries:

- **6.4.1 — Sparse-convolution providers (complete):** inspect the pinned cumm/spconv
  artifacts used by the plugin sources; validate their CPU/CUDA/C++ ABI and
  device support before accepting inputs. Keep reusable providers under
  third_party/ and root-owned version selection. A vendor download is not
  proof that its C++ API can be linked safely with this toolchain.
- **6.4.2 — autoware_tensorrt_plugins (complete):** build the entire seven-kernel cuda_ops
  library and all eleven plugin creators, including sparse-convolution plugins.
  Run all original parameterized reference-kernel cases on the GPU, plus added
  library loading/creator and real TensorRT execution/serialization checks.
- **6.4.3 — autoware_tensorrt_classifier (complete):** expose the full classifier and GPU
  preprocessing library. No original runtime suite is registered; add inference
  and CPU/GPU preprocessing behavior checks with explicit expected results.
  The deterministic test-only ONNX fixture needs no pretrained model module;
  introduce production model resources only at their real consumer boundary.
- **6.4.4 — autoware_tensorrt_yolox (complete):** integrate its required local utility and
  interface closure, real model bundles and launcher paths. Preserve all label,
  detector and ROS node tests, including GPU/model-dependent cases without
  skips. Check actual detection/segmentation outputs and node resources.

Dependencies and model imports remain separate validated package/repository
commit boundaries. Refine exact behavior fixtures before each implementation;
the accepted results follow. Do not widen into unrelated perception packages
or alter the existing runtime/backend contract.

Accepted results: the complete plugin library (seven CUDA/twenty-four C++
sources, eleven creators) passes all eighteen original cases and two added
libc++/TensorRT loading/serialized-engine checks. The classifier passes five
added inference/preprocessing cases; YOLOX passes all fourteen original cases,
including real FP16 detection, segmentation and Cyclone ROS delivery. Its local
utility/interface/decompressor closure passes complete suites. Launcher tests
resolve twelve YOLOX model/metadata paths in five files through the repository's
own index and check both affected manifests. No dependency-related skips remain.

Accepted sparse SDK inputs: spconv 2.3.8/cumm 0.5.3, cu130-rev1 amd64 release,
checksum-pinned under third_party/spconv. Its container API requires GNU C++ ABI
and GLIBCXX 3.4.32; build the pinned GNU 13.3.0 runtime and isolate that consumer
closure behind TensorRT's versioned interfaces. A narrow C++17 TensorView
conditional patch permits Clang 21 compilation, with positive/rejected conversion
and real SDK GPU-sort checks. Keep the default application toolchain unchanged.
The full patch/provenance/commands are in the Phase 6.4.1 journal.

The first model subtree, tensorrt_yolox v1.0 at
424d6b9cb94c939ae9ce6a22380e46c13f76b1da, owns its module/ament/colcon metadata.
All five LFS payload hashes match; a fresh independent checkout retrieves them
from the project remote and passes Bazel resource/relocation and colcon symlink
checks. YOLOX consumers have no HOME-model fallback. Other model repositories
and removal of the remaining shared data_path plumbing are future consumer
migrations, not a completed whole-launcher conversion.

Phase 6's selected boundaries are complete. Individual sparse-plugin numerical
accuracy, INT8 calibration, performance, full perception/GUI launches and bit-
identical application binaries are not established by these gates. Runtime
bundles/full driving remain Phase 7; Thor-compatible software builds remain
Phase 8. No new Fast DDS acceptance run was performed; it still requires
explicit approval. The x86 TensorRT 10.16.1 versus JetPack 7.2.1's 10.16.2 gap
remains explicit for Phase 8.

## Phase 7 — Runtime packaging and headless planning simulation (in progress)

Follow the [packaging and deployment requirements](#runtime-packaging-and-deployment-requirements).
Complete each distribution boundary before expanding the application closure.

1. **7.1 — Native headless core bundle (complete, 2026-11-20).** The original core
   launch and selected sample indexes are exported through rules_pkg 1.3.0's
   runfiles support, with a separate verification companion. Both original cases
   pass natively after relocation and in a read-only, unprivileged, network-
   isolated Ubuntu 24.04 filesystem without ROS/Python installs or source/cache
   mounts. Parent/child executable and loaded-library audits allow only the
   loader/glibc outside the application. The owning wrapper exports its mains'
   logging provider; root composition selects real GL dispatch, pinned GNU and
   source-built zlib runtime providers. Scope is x86_64 CPU/Cyclone DDS with
   Agnocast disabled. See the [reproducible gate](JOURNAL.md#2026-11-20-phase-71--native-headless-core-distribution).
2. **7.2 — Runtime distribution variants and service supervision (complete, 2026-11-21).**
   rules_img 0.3.22 adds the unchanged native payload at /opt/autoware to
   digest-pinned Ubuntu 24.04. OCI and Docker-load exports contain both layers;
   assembly needs no container daemon or image-side apt/pip/rosdep. Payload
   parity, all image
   structure assertions, both existing behavior/shutdown cases, and SIGINT
   shutdown through the normal unprivileged image entrypoint pass on native
   Docker Engine with read-only roots and no network/workspace/cache mounts.
   Scope remains x86_64 CPU/Cyclone DDS with Agnocast disabled. See the
   [reproducible gate](JOURNAL.md#2026-11-20-phase-72--thin-oci-core-distribution).
   The single-entrypoint variant is complete; the s6, systemd and OSTree
   variants below are additional completion gates. A pinned QEMU guest is an
   optional additional OCI packaging check; a bootable OSTree deployment would
   require its own guest boot gate. Neither establishes real-time or GPU support.
3. **7.3 — AWF planning simulation (in progress).** Establish the exact tutorial/source/input
   contract, then demonstrate the original planning_simulator.launch.xml
   workflow and repeatable headless functional assertions. Integrate the
   selected autoware_universe planning/launch dependency closure first where
   required by that workflow. A smaller core-only experiment is optional and
   does not satisfy the AWF workflow gate by itself.

Phase 7.2 task boundaries:

- **7.2.1 — Single-entrypoint OCI image (complete, 2026-11-20).** Retain the
  validated native bundle, image exports and existing complete image gate.
- **7.2.2 — s6 service supervision (complete, 2026-11-21).** The separate OCI variant uses
  s6-overlay/s6-rc as PID 1, the unchanged core bundle and a small dummy
  prerequisite service. Use explicit readiness notification to gate core
  startup. On unexpected prerequisite loss, stop the managed core and expose
  the failure; recovery requires an explicit product restart. Define orderly
  reverse shutdown; startup dependencies alone do not implement this policy.
  SHA-256-pinned 3.2.3.2 release inputs live under third_party/s6/; product
  definitions stay under images/ and tests under bazel/tests/. Both original
  core cases and all readiness/failure/restart/shutdown checks pass with a
  read-only image, UID/GID 1000, dropped capabilities and no network/source/cache
  mount. The complete affected native/plain-image regressions also pass.
- **7.2.3 — systemd service supervision (complete, 2026-11-21).** The
  separate OCI variant uses pinned Ubuntu systemd 255.4-1ubuntu8.19 as PID 1,
  Type=notify readiness and strict core-to-prerequisite requirements. Both
  original core cases and the complete readiness/failure/recovery/shutdown
  contract pass. Applications run as UID 1000; the manager runs as root with
  a private cgroup namespace, SYS_ADMIN and a container-local AppArmor override.
  The entrypoint makes only its private cgroup mount writable; no host-cgroup
  bind mount, host manager or privileged-container mode is used. See the
  [systemd gate](JOURNAL.md#2026-11-21-phase-723--systemd-core-service-supervision)
  for the precise runtime contract and dependency/failure-policy findings.
- **7.2.4 — OSTree native distribution (complete, 2026-11-21).** Reuse the native core
  payload and the systemd/core-plus-dummy service composition for versioned
  delivery through pinned OSTree 2024.5-1build2. The selected scope is application
  trees on a fixed supported OS: the OCI systemd variant is an isolated test
  harness, not the delivery artifact. Immutable checkouts plus atomic active
  selection, followed by an explicit product restart, implement activation.
  Whole-OS/bootc deployment is deferred. No host OS conversion or QEMU boot is
  required for this application-only scope. Pin generic OSTree tools/dependencies
  under third_party/; keep product assembly with root runtime/distribution
  definitions and added tests under bazel/tests/. No target-side apt/pip/rosdep
  repair is allowed. The full gate passes for A→B→A, including 23,665 native
  file/link comparisons, integrity rejection, interrupted preparation, all
  managed core behavior and preserved synthetic vehicle records. See the
  [OSTree evidence](JOURNAL.md#2026-11-21-phase-724--ostree-application-tree-distribution).

The OSTree gate must exercise two immutable, distinguishable revisions: verify
repository integrity and provenance, install/activate the first revision, update
to the second, and roll back to the first. Check the active revision, complete
core behavior, the dummy prerequisite relationship and clean shutdown after
each transition. Demonstrate that interrupted preparation leaves the previous
revision usable and that writable configuration/state is preserved under the
documented compatibility policy. Use an isolated disposable runtime; if the
selected scope includes OS deployment, actually boot/reboot into the selected
deployment and verify the booted revision. An application-only demonstration
must not claim bootloader/kernel rollback or whole-OS update support. Select
the exact inputs and verification commands before implementation. This is a
local deployment/update experiment, not production OTA or fleet orchestration.
In the state-preservation fixture, distinguish synthetic vehicle identity and
calibration records from replaceable domain resources. Preserve the former
across updates/rollback; full domain switching remains deferred as described
under [later priorities](#later-phases). A bootc-compatible OCI OS image is a
candidate for the bootable scope, subject to base/ABI and boot validation.
See the upstream [OSTree overview](https://ostreedev.github.io/ostree/introduction/)
and [deployment contract](https://ostreedev.github.io/ostree/deployment/), reviewed
2026-11-20; the selected application-only inputs and runtime gate were validated
2026-11-21. No bootable OS deployment is established.

For both supervised variants, the dummy daemon is product demonstration support,
not a change to core packages. It announces readiness and remains alive while
the core runs. The acceptance gate must demonstrate delayed readiness without
early core startup, normal operation, prerequisite failure stopping the core,
explicit restart recovery, plus reverse shutdown with no surviving children.
Reuse the existing core functional assertions against the supervisor-started
core, with a root-owned external-runtime adapter where necessary; do not launch
a second independent core graph to stand in for the supervised application.
Keep nodes, parameters, map/URDF delivery, velocity relay, version API, graph
endpoints and executable/library closure checks. Validate the new image
structure/configuration and reuse unchanged native payload evidence. Each
variant's full gate must pass before its module task is complete.

Implement and validate these tasks sequentially with Cyclone DDS and Agnocast
disabled, respecting user.bazelrc. Fast DDS requires fresh approval; GPU,
mixed-deployment frameworks and Phase 7.3 implementation are outside these
tasks. QEMU is conditional on the selected OSTree deployment scope. Commit the
validated boundaries separately with their complete gates and journal entries.

Phase 7.3 task boundaries (selected 2026-11-21):

- **7.3.1 — Simulation primitives (complete, 2026-11-21).** Integrated local
  autoware_learning_based_vehicle_model, autoware_simple_planning_simulator,
  autoware_dummy_perception_publisher and autoware_vehicle_door_simulator.
  All ten original simulator and both original SDF cases pass, alongside root
  embedding/move/teardown and original-launch motion/perception/door checks:
  20 cases in six targets, zero failures/skips. Interpreter lifetime corrections
  are separate source commits; package build boundaries are committed separately.
  [Evidence](JOURNAL.md#2026-11-21-phase-731--vehicle-door-simulator).
- **7.3.2 — Selected Universe implementation closure.** Adopt the default
  rule-based planning, control, simulated perception and supporting system/API
  dependencies in related groups, starting from their actual local launch and
  source declarations. The mission-planning group is complete (2026-11-21):
  local tier4_planning_msgs/RerouteAvailability, tier4_external_api_msgs/SetPreferredLane,
  autoware_mission_planner_universe and autoware_manual_lane_change_handler.
  Its complete gate passes 42 cases in six targets, plus collector/template
  regression checks and the full 252-interface/289-instantiation plugin build.
  Runtime coverage is CPU/Cyclone DDS with ENABLE_AGNOCAST=0, including real
  routing, goal echo, arrival, preferred-primitive update/reset and shutdown.
  [Evidence](JOURNAL.md#2026-11-21-phase-732--manual-lane-selection).
  Shared traffic-light and boundary-departure libraries are also complete:
  all 68 original cases pass, with the boundary expected-header correction
  independently verified against matching merged AWF PR #13622.
  [Library evidence](JOURNAL.md#2026-11-21-phase-732--shared-boundary-departure-library).
  The freespace library/Python binding gate is complete (14 cases), followed by
  the interface/RTC prerequisite group: nine local Tier IV provider surfaces
  expose 89 registered interfaces and pass 115 generated wire-type round trips.
  The standard-service baseline, existing payload/namespace/template checks and
  the complete original RTC lifecycle suite pass together: 15 test targets,
  zero failures/skips. The full 339-interface/388-instantiation bridge library
  also builds, CPU/Cyclone DDS with ENABLE_AGNOCAST=0.
  [Collector and gate evidence](JOURNAL.md#2026-11-21-phase-732--simulator-interface-collector-and-bridge-compilation).
  Behavior-path planning is complete for the selected profile: the shared
  library, planner node/test helpers and all eight factories (static avoidance,
  avoidance-by-lane-change, left/right lane-change, goal/start planning,
  side-shift and bidirectional traffic) pass their complete original suites and
  individual executable gates. The joint gate loads all factories in one actual
  planner, checks configuration updates and clean shutdown. Preserve the original
  disabled cases; bidirectional traffic has no implemented parameter-update
  callback. [Shared-library evidence](JOURNAL.md#2026-11-21-phase-732--shared-behavior-path-planning-library),
  [planner](JOURNAL.md#2026-11-22-phase-732--behavior-path-planner-node),
  [selected-scene composition](JOURNAL.md#2026-11-22-phase-732--goal-planner-scene-plugin).
  Its sampler-common/Frenet/Bézier prerequisites are complete. Sampler-common
  preserves two original explicit edge/benchmark skips; the added native
  consumers exercise actual trajectory generation. Goal-planner opts into
  libc++ PSTL locally; its pinned backend is serial. Lane-change's direct fmt
  include matches merged AWF #13594 and is a separate source fix.
  [Sampler evidence](JOURNAL.md#2026-11-22-phase-732--sampler-common-library),
  [Frenet](JOURNAL.md#2026-11-22-phase-732--frenet-planning-library),
  [Bézier](JOURNAL.md#2026-11-22-phase-732--bézier-sampler-library),
  [header alignment](JOURNAL.md#2026-11-22-phase-732--lane-change-scene-plugins).

  Behavior-velocity planning has its existing core planner/stop-line support,
  the local RTC scene helper and four selected plugins available: traffic light,
  detection area, virtual traffic light and no-stopping area. Complete original
  suites and both standard/experimental executable lifecycles pass; the launcher
  selects the experimental factories. The four-plugin group passes 47 active
  cases without failures/skips. [RTC evidence](JOURNAL.md#2026-11-22-phase-732--behavior-velocity-rtc-scene-support),
  [signal/area group and command](JOURNAL.md#2026-11-22-phase-732--traffic-light-behavior-velocity-plugin).
  Crosswalk, walkway, intersection and blind spot are also complete for the
  selected profile: fourteen compatible original cases, eight executable
  lifecycles and one UUID regression pass. Blind spot preserves upstream
  unregistered obsolete utility coverage; no pass is claimed for that file.
  [Scene gate and limitations](JOURNAL.md#2026-11-22-phase-732--crosswalk-behavior-velocity-plugin).
  The grid-map 2.5.0 provider is complete for core/CV/ROS/messages: all 109
  original-suite/factory cases pass. Its source archive, Eigen hooks and runtime
  resources are pinned under third_party/grid_map. The local iterator helper
  also passes its complete seven-case suite and builds its benchmark; its
  const-correctness source fix is separate from build rules.
  [Iterator evidence](JOURNAL.md#2026-11-22-phase-732--local-grid-map-polygon-iterator). [SDK evidence](JOURNAL.md#2026-11-22-phase-732--grid-map-dependency-provider).
  Keep these generic dependency inputs below source components.
  Its concrete interface providers are registered at the root: canonical
  descriptor/template regressions and full 345-interface/398-instantiation
  plugin compilation pass in the CPU/Cyclone DDS configuration.
  [Composition evidence](JOURNAL.md#2026-11-22-phase-732--grid-map-interfaces-in-product-composition).

  Path smoothing and scenario selection are complete: all four original native
  cases and indexed startup/script checks pass. The wrapper preserves ROS-aware
  executor selection; registered Python executables retain their runfiles.
  [Evidence](JOURNAL.md#2026-11-22-phase-732--scenario-selector).
  External velocity limit selection also passes its complete original
  arbitration/parameter-update launch suite and clean shutdown.
  [Evidence](JOURNAL.md#2026-11-22-phase-732--external-velocity-limit-selection).
  Selected slow-down, cruise and dynamic-obstacle-stop motion plugins are
  complete: their seven original cases and four real planner profiles pass.
  Cruise preserves updated node parameters across plugin reload.
  [Evidence](JOURNAL.md#2026-11-22-phase-732--obstacle-slow-down-motion-plugin).
  The remaining five motion factories are complete: run-out, out-of-lane,
  obstacle velocity limiter, experimental boundary prevention and road-user
  stop. Their original suites plus affected cruise/slow-down suites and nine
  individual/one joint planner profiles pass 49 cases in 16 targets. Distinct
  internal type identities prevent cross-plugin destructor collisions; limiter
  parameters survive reload. The joint gate uses component-default configs and
  does not establish simulator driving behavior.
  [Complete motion group](JOURNAL.md#2026-11-22-phase-732--run-out-motion-plugin),
  [composition defect](JOURNAL.md#2026-11-22-phase-732--distinct-motion-plugin-internal-types).
  The local Universe utility dependency is complete: 118 active original
  cases and the core/Universe conversion regression pass, preserving four
  original disabled cases. Its duplicate global TF definition is corrected
  separately from the build definitions.
  [Evidence](JOURNAL.md#2026-11-22-phase-732--local-universe-utility-library).
  Selected parking costmap/freespace nodes are complete: twenty active original
  cases plus indexed executable/affected adapter checks pass in eight targets.
  Two original disabled freespace cases remain disabled. The costmap uses the
  current message_filters headers; standalone freespace declares logger-service
  introspection typesupport. These gates do not establish simulator parking.
  [Parking evidence](JOURNAL.md#2026-11-22-phase-732--costmap-generator).
  Remaining-distance/time, hazard-light selection and planning evaluation are
  available. The complete 30-case original evaluator suite, both evaluator
  executables, live hazard arbitration and affected indexed-node/costmap checks
  pass 51 active cases in 12 targets. The remaining-distance startup gate does
  not establish full-route numeric accuracy. Keep that distinction for driving
  acceptance. [Support group evidence](JOURNAL.md#2026-11-22-phase-732--planning-evaluator).
  Continue in dependency order through shared planner helpers, the
  preset-selected behavior/motion plugins and controller libraries, then
  simulated perception and the selected system/API closure. The selected local
  path optimizer and its acados native/code-generation dependency are complete:
  all 6836 SDK assertions in seven native cases, the generated numerical solve,
  original optimizer node case and installed startup/script checks pass.
  [Evidence](JOURNAL.md#2026-11-22-phase-732--local-path-optimizer-with-generated-mpc). Preserve
  the control preset's enabled lane-departure, validator, AEB and collision gates;
  obstacle-collision and predicted-path checkers are disabled unless another
  selected implementation actually depends on their libraries. CMake-only shims
  are not standalone implementation targets; preserve their actual effects
  through native providers. Default-disabled planners, monitors and control
  checkers do not enter the selected profile merely because they have launch
  declarations. The complete selected closure must pass before 7.3.2 closes.
  Refine each group from the actual selected launch/source declarations;
  do not replace the Universe stack with the smaller core-only launch.
  Execution scope agreed 2026-11-22: continue through the remaining selected
  implementation dependencies and their complete component gates, then pause
  immediately before full simulator composition/startup in 7.3.3. Individual
  module launches and behavior tests remain part of 7.3.2 validation. This does
  not authorize substituting a smaller stack or starting the deferred Pixi
  comparison; preserve the original simulator entrypoint for the next task.
- **7.3.3 — Original launch and resources (in progress).** The first resource
  group is complete (2026-11-21): the tutorial's local sample_vehicle_description
  and sample_sensor_kit_description own their original model/calibration/mesh
  resources, and the independent autoware_map_asset module owns the official
  tutorial archive and resource index. Root selects its local override; source
  consumers may declare its public module/index dependency. The supported core
  launch and native/OCI/s6/systemd/OSTree products resolve that indexed map
  without map_path injection or a separate runtime map mount.
  SHA-256 5536fce7bb8db7688fdf94ec004118b898637ad0d5b6175108b10989dd6e93b9
  pins the otherwise mutable map URL; use the documented tutorial input rather
  than a substituted test map. [Map provenance](../../assets/map/README.md).
  A short MGRS/54SVE scenario is pinned and accepted by the real Universe planner
  through lanelets 9102/9540; PCL reads all 1757841 cloud points. Resource,
  relocation and affected description-launch gates pass 12 cases in seven
  targets, CPU/Cyclone DDS with ENABLE_AGNOCAST=0.
  [Evidence](JOURNAL.md#2026-11-21-phase-733--official-tutorial-map-and-scenario-resources).
  Compose the actual planning_simulator.launch.xml after the remaining 7.3.2
  implementation closure is available; this resource gate does not close that
  dependency or establish full simulator startup. Keep default rule-based
  behavior; migrate any selected model provider and affected launcher paths
  together under the indexed-resource convention.
- **7.3.4 — Lane-driving acceptance.** Demonstrate the original RViz workflow
  and a repeatable equivalent headless case, including initial pose, route,
  autonomous engagement, vehicle motion, bounded goal completion and clean
  shutdown. Retain the runtime closure/packaging checks for the expanded graph.

The version 1.4.0 tutorial and the current
[lane-driving page](https://docs.autoware.org/main/simulation/planning-simulation/lane-driving/)
were compared 2026-11-21: both use sample_vehicle/sample_sensor_kit, the original
entrypoint, pose/goal selection and ChangeOperationMode. The local entrypoint
selects Universe launch packages, rule-based planning, simulated perception,
vehicle simulation and localization_sim_mode=api; default scenario_simulation
and use_sim_time are false. The legacy HOME/data_path argument still present in
unadopted launcher paths is not a new supported resource convention. Resolve
that boundary when those consumers enter the graph, without unrelated edits now.
Routine 7.3.1 validation is CPU/Cyclone DDS with Agnocast disabled. No Fast DDS,
GPU, full communication matrix, QEMU or deployment-framework work is scheduled.

The local standalone autoware_simple_planning_simulator manifests do not require
Universe planning implementations. Its
[standalone repository](https://github.com/autowarefoundation/autoware_simple_planning_simulator)
and AWF's [extraction discussion](https://github.com/orgs/autowarefoundation/discussions/6892)
support investigating a core-only closed loop with that simulator and explicit
simulation inputs. This is source-level feasibility, not runtime evidence.
The current autoware_launch/planning_simulator.launch.xml selects the Universe
launch stack; preserving that exact workflow requires its actual dependency
closure. Review that boundary before implementing 7.3, rather than substituting
a core-only launch and declaring the documented simulator milestone complete.

## Later phases

### Incoming phase identifiers

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

Priority agreed 2026-11-21: finish the current 7.2 distribution variants, then
use 7.3's original planning-simulator workflow to integrate its selected
autoware_universe closure. Broaden Universe coverage in groups of related
packages with complete package suites and relevant behavior gates. Preserve
existing phase identifiers; refine broader coverage tasks when that closure is
known. Vehicle-specific provisioning and operating-domain redeployment remain
deferred until substantial Universe coverage is available; do not introduce
Ansible deployment roles, fleet tooling or a domain-switching framework now.
Retain the ownership distinction between software releases, model/map/parameter
resources, individual vehicle identity/calibration records and runtime state.
Any later redeployment milestone should prove A → B → A resource selection,
compatibility rejection and recovery while preserving vehicle records. This
does not expand Phase 7.2 beyond its agreed distribution/service demonstrations.

From phase 1 onward, runnable slices use Cyclone DDS by default. Propose Fast
DDS compatibility checks at major milestones after the Cyclone DDS exit
criteria pass; run them only after explicit user approval.

Potential topics such as OSQP compatibility, OpenMP, CMake parity,
runtime-image deduplication, and remote caching will be evaluated when a current
target requires them.

### Deferred Bazel/Pixi comparison

Priority agreed 2026-11-22: complete the full autoware_universe Bazel integration
before starting a direct Pixi comparison. The selected planning-simulator
dependency closure alone does not satisfy this prerequisite. Continue Phase 7.3
and broader Universe adoption in related package groups; keep the comparison
outside current phase gates and preserve the existing phase identifiers.
Do not introduce Pixi manifests, recipes, packaging adapters or benchmarks now.

Once full Universe integration is complete, define an equivalent product and
test scope using the same source revisions, models/maps and behavior checks.
Record toolchain/dependency differences separately so they are not mistaken
for build-system effects. Compare:

- Fresh-machine setup, required host inputs and clean-build time.
- Small-edit rebuild/test time and reuse of cached outputs on another machine.
- Upstream-update effort and ongoing build-definition/package maintenance.
- Product/resource selection and native runtime dependency closure.
- Native deployment and OCI/application-only OSTree assembly, including the
  same isolation, startup/shutdown and applicable update/rollback checks.

Use [AWF's Pixi report](https://github.com/orgs/autowarefoundation/discussions/7050)
as a starting reference, alongside the documented
[ROS package backend](https://pixi.prefix.dev/latest/build/backends/pixi-build-ros/)
and [native packing tools](https://pixi.prefix.dev/latest/deployment/pixi_pack/).
Their documented capabilities are not evidence that an equivalent Autoware
deployment or performance comparison has passed. Decide from measured benefits
and maintenance cost; no comparative performance or total-cost advantage has
been established by this experiment.

## Runtime packaging and deployment requirements

Phase 7 establishes the runtime distribution gates below; incomplete experiments
do not establish deployment support. Production OTA remains deferred. Phase
7.2.4 demonstrates local OSTree application installation/update/rollback.

### Environment responsibilities

| Environment | Responsibility |
| --- | --- |
| Builder host | Kernel, hardware/device access and optional container engine. GPU/kernel-backed tests need their execution prerequisites here. |
| Build environment, native or containerized | Minimal audited bootstrap tools to start Bazel; target toolchains, generators, libraries and SDKs come from pinned Bazel inputs. |
| Runtime, native or containerized | Application bundle plus an explicit OS/ABI baseline, configuration, device access and required host services/drivers. No Bazel installation or source workspace is required on the deployed device. |

Prefer native builds and native execution. Keep build containers and runtime
containers independently optional; use containers for isolation and distribution
without duplicating dependency resolution through rosdep, apt or pip. Audit the
remaining bootstrap and system dependencies rather than claiming that pinned
SDK archives alone establish hermetic execution. images/dev.Dockerfile is a
build-validation environment, not a runtime image or deployment prerequisite.
For existing kernel/runtime alternatives, see
[the deferred platform options](#deferred-cross-platform-and-kernel-runtime-options).

### Deployment scope decision

Keep the native bundle as the primary product artifact and OCI as an optional
single-host packaging form of the same application closure. Preserve ROS launch
as the owner of the ROS node graph. The s6 and systemd image variants demonstrate
management of surrounding services, with core launch as one managed service;
they do not require one service unit or container per ROS node.

Do not pursue multi-container composition, multi-host placement profiles,
mixed-backend configuration generation or a universal deployment abstraction in
this experiment. Those remain possible later options when an actual isolation,
runtime-environment or operational requirement justifies their maintenance cost.
Service dependencies and differing library dependencies alone do not establish
that requirement. This closes the deployment-profile investigation; the explicit
Phase 7.2 supervisor and application-only OSTree demonstrations are complete
for the current CPU/Cyclone core product.

### Declare once, package from the runtime closure

Component-local targets own executables, shared libraries, plugins, Python
modules and ament resources, including launch/configuration files. Product
composition under bazel/ selects entry points and concrete runtime resources
such as vehicle/sensor descriptions, maps and model modules. Package that
selected declared closure; do not require a second handwritten inventory of
every package's files in a product packaging target. Retain component-to-root
independence and the [model resource convention](#model-repository-and-runtime-path-convention).

Investigate collecting existing runfiles/ament/runtime providers transitively.
Compilation dependencies alone are insufficient for dlopen, plugin discovery,
Python imports or dynamically selected package resources. Developers must
explicitly declare these runtime edges; undeclared dynamically constructed
filenames cannot be inferred reliably. Do not scan entire source directories
to conceal missing declarations. Fail packaging on conflicting destination
paths and unresolved declared inputs. Verify provider coverage before choosing
a packaging rule or introducing a new provider.

Start by preserving the working runfiles/index structure, materializing its
files and symlinks into a self-contained bundle. Preserve repository mappings,
executable permissions, shared-library identities and resource lookup. A
bundle-relative launcher must work without absolute build/cache paths or an
inherited ROS/Python/library search environment. Audit the ELF interpreter,
shared libraries (including dynamically loaded libraries) and Python runtime;
document what is bundled versus supplied by the supported OS baseline.

### Native bundle and OCI filesystem layout

The bundle is the primary deployable application artifact. A native installer
or unpack operation chooses its device directory; the launcher resolves paths
relative to the bundle. OCI assembly places the same application closure at a
chosen image prefix, for example /opt/autoware, on a digest-pinned compatible
base containing only required OS dependencies. A minimal base is a goal, not
a claim that the current binaries can run in an empty image.

Use packaging mappings for destination prefixes, preserved relative paths and
necessary explicit renames. Image workdir/entrypoint settings do not copy files
or discover their runtime closure. Phase 7.1 selects rules_pkg 1.3.0 for native
archives; Phase 7.2 selects rules_img 0.3.22, already in the RCR graph, for
digest-pinned base pulls, image assembly and OCI/Docker-load exports. No separate
rules_oci dependency is required. Keep image prefixes, entry points
and device-side bind mounts in product/deployment configuration, outside
component BUILD definitions. Separate immutable application/model resources
from writable state, logs and device-specific configuration; mount persistent
state explicitly and make its compatibility part of release/rollback design.

### Phase 7 packaging validation

1. Inventory existing runtime providers and remaining system/ABI prerequisites.
   Export one native bundle for the original headless core launch, selecting
   sample vehicle/sensors and the declared indexed map asset.
2. Extract/materialize it at a different path. Execute with no source workspace,
   Bazel cache or host ROS installation available and without inherited
   AMENT_PREFIX_PATH, PYTHONPATH, LD_LIBRARY_PATH or LD_PRELOAD. The launcher
   establishes only the bundle's required paths. Run from outside the bundle
   directory as well. No build-tree symlinks may be needed.
3. Reuse the behavior and shutdown assertions of
   [the current headless core test](../../bazel/tests/core_system/test_launch.py),
   declared by //bazel/tests:core_system_headless_launch_test in
   [the root test BUILD](../../bazel/tests/BUILD.bazel). Check nodes, parameters,
   map delivery, robot description, velocity relay, version API, graph endpoints
   and process exit. The existing test is a Bazel-runfiles gate, not evidence
   of a deployed artifact or full driving simulation.
4. Run the same application bundle and behavior gate inside its OCI image with
   only declared mounts/devices and the documented OS baseline. Inspect loaded
   libraries and indexed resources to detect accidental host dependencies.
5. For GPU/Agnocast-enabled products, additionally verify their actual device,
   driver/module and permission prerequisites plus selected behavior. An image
   cannot supply a running host kernel or make incompatible drivers compatible.
   Keep routine middleware Cyclone DDS; Fast DDS still needs explicit approval.

Repeat extraction and behavior checks when runtime packaging changes. Missing
runtime registrations must fail the relevant packaged behavior gate; passing
bazel test or successfully building a tar/image alone does not close this gate.
The AWF simulator milestone below remains a separate Phase 7 behavior goal.

### AWF planning-simulator milestone

A clear Phase 7 milestone is a working planning simulator following AWF's
[official planning-simulation tutorial, version 1.4.0](https://docs.autoware.org/1.4.0/tutorials/ad-hoc-simulation/planning-simulation/),
reviewed 2026-11-19. Use its lane-driving workflow as the initial acceptance
scenario: load the sample map and vehicle/sensor configuration, initialize the
ego pose, choose a goal, obtain a route/trajectory, enter autonomous mode and
observe simulated vehicle motion. Require goal completion within recorded
functional tolerances and clean shutdown as our additional assertions.

Demonstrate the documented RViz interactions on a suitable native or graphical
runtime; add repeatable headless checks through the corresponding ROS interfaces
against the same launch/configuration. Preserve the original
planning_simulator.launch.xml entry point's behavior. Replace installation and
artifact provisioning with our declared Bazel/runtime resources, including the
existing indexed-model convention; do not reintroduce Ansible or HOME paths.
The earlier core headless test is a packaging stepping stone, not this milestone.

Before implementing the scenario, reconcile that versioned tutorial with the
local source snapshots and current AWF documentation. Record the exact tutorial
revision, launch arguments, map/model identities, route/poses and observable
pass criteria. Do not claim support for every tutorial scenario from one
lane-driving result; enumerate additional scenarios separately when scoped.

### QEMU packaging check

Consider a clean QEMU system-emulation guest as the isolated runtime for the
Phase 7 packaging gate. Pin the guest OS/kernel/image and QEMU configuration,
including CPU architecture/features, accelerator, devices, memory and networking.
Supply only the built bundle and declared test inputs, without workspace/cache
mounts or guest-side package installs that repair missing application resources.
Run a native bundle directly in the guest; to check an OCI artifact, use an
explicitly provisioned container runtime inside the guest. An OCI image is not
itself a bootable VM disk. See [QEMU system emulation](https://www.qemu.org/docs/master/system/introduction.html).

Check loading, resource discovery, startup, functional behavior and shutdown.
Real-time factor, latency, throughput, jitter and meeting control deadlines are
not acceptance gates under emulation. Allow bounded test timeouts appropriate
to the guest, use simulation time where the selected simulator supports it,
and record any timing/configuration changes; do not silently suppress functional
failures. The initial CPU/native-ROS gate can use Cyclone DDS with Agnocast
disabled, explicitly without establishing enabled-backend or GPU coverage.

The full AWF simulator demonstration need not run inside QEMU when its selected
closure needs unavailable hardware. Keep that milestone separate from the
applicable guest packaging tests. GPU and kernel-backed paths require separately
provisioned guest devices/drivers or native tests; CPU guest success does not
establish NVIDIA, Agnocast, Thor or real-time compatibility. This is a proposed
validation environment, not a claim of an existing QEMU runner.

### Deferred OTA deployment

Revisit this after the [Universe coverage priority](#later-phases). The proposed
ownership boundary is recorded now; no additional deployment framework is being
implemented in Phase 7.2.

| Layer | Proposed responsibility |
| --- | --- |
| Bazel | Build identified application/model/resource artifacts from pinned inputs; never delegate application compilation or dependency resolution to target provisioning. |
| OS image pipeline | Own the supported base OS, kernel/drivers and fixed service definitions. bootc is an option for a separately validated bootable OCI OS image; current application images are not bootc OS images. |
| Optional Ansible provisioning | Bootstrap/configure machines and supply machine-specific networking, permissions, identities/secrets and calibration references. Consume already-built releases or invoke the chosen update agent. Keep future provisioning playbooks separate from the existing ansible/ dependency-installer reference. |
| systemd or s6 | Own local service readiness, ordering, failure and shutdown, with ROS launch owning the internal node graph. |
| Update agent | Own authenticated staging, compatibility/health decisions, safe activation and recovery. Ansible can coordinate operations but is not itself the transactional update protocol. |

Keep four independently owned inputs explicit: immutable software release;
versioned domain resources (models/maps/parameters); persistent per-vehicle
identity/calibration; and writable runtime state/logs. A domain change selects a
compatible resource manifest and restarts affected services, while preserving
vehicle records. Image/application rollback must not blindly replace identities
or calibrations. State-schema compatibility and any migrations need an explicit
policy before recovery can be claimed. The later milestone's acceptance gate
is described under the priority above; this is not a new incoming phase.

These are design choices for this experiment. Reference capabilities:
[Ansible playbooks](https://docs.ansible.com/projects/ansible/latest/playbook_guide/playbooks_intro.html)
provide configuration/orchestration, while [bootc](https://bootc.dev/bootc/)
targets bootable host OS updates. Neither is currently a required runtime layer.

An OCI release is delivered by immutable image digest, with authenticated release
metadata identifying application/configuration/model versions, target hardware
and required host ABI/driver/module compatibility. Separately delivered models
must also have exact identities pinned by the release. Select signing and update
tooling when an OTA experiment is authorized. Phase 7.2.4 supplies local
application selection/update tests, not an authenticated OTA agent.

A host-side agent/supervisor, independent of the application container, would
verify the release, download/stage it while retaining the working version,
check storage and compatibility, and activate it at a safe operational boundary
(for a vehicle, parked with operation inhibited). Health checks must establish
required functional readiness before accepting the release; failure restores
the previous image and compatible configuration/state. Design interruption and
power-loss recovery, rollback-compatible persistent data and release status
reporting before claiming OTA support.

Application OCI updates and host OS/kernel/NVIDIA-driver/Agnocast-module updates
are separate coordinated release paths. Images do not update the running host
kernel. A first OTA PoC can use one device, a registry, a supervisor and retained
current/previous releases, demonstrating authenticated staging, activation,
health failure and rollback. Fleet orchestration is later scope.

## Deferred cross-platform and kernel-runtime options

Recorded 2026-11-18 for later design work. These are options, not implemented
rules or additional phase completion requirements. Revisit execution/packaging
choices in Phases 7 and 8; preserve Phase 8's software-compatibility goal without
requiring physical Thor hardware. Introduce each mechanism only when a selected
target or test needs it.

Separate the machine executing build tools, the CPU/OS targeted by their output,
and the environment executing a test. Cross-compiling an application does not
require loading its target kernel driver on the build host. Current wrapper
binaries compile Agnocast support unconditionally; the loaded-module requirement
belongs to ENABLE_AGNOCAST=1 execution, not the shared binary's compilation.

| Option | What enters the build graph | What remains an execution prerequisite |
| --- | --- | --- |
| Existing host Linux | User-space binaries, heap hook, bridge resources and a runtime-check executable. | Compatible loaded host driver, accessible device and required permissions. |
| Cross-compiled Linux user space | Target CPU/OS platform, cross toolchain, pinned sysroot/ABI inputs and target libraries, including selected JetPack inputs. | A matching execution environment for target behavior tests; building does not prove execution or GPU compatibility. |
| Complete Linux system image | Pinned kernel/configuration, matching modules, application/root filesystem and image assembly. Existing kernel build machinery can be wrapped rather than rewritten. | Booting the generated kernel in a VM or device; driver built-in/autoload configuration must be designed and tested. |
| Small OS/RTOS or bare-metal firmware | Kernel where applicable, drivers, application, startup code, linker script and freestanding cross toolchain, linked into an ELF/flashable image. | Emulator or board runner, device access and board-specific flashing/boot support. This is a separate portability investigation, not an Autoware/ROS/Agnocast support claim. |

Suggested incremental path for the current host-kernel case:

1. Consider a declared runtime bundle/provider consumed by enabled test/launch
   macros, collecting the heap hook, bridge resources and prerequisite checker.
   Make the checker run in the same host/device context immediately before the
   actual test. Check device access and driver/userspace compatibility, with a
   clear failure message; leave module installation/loading to host provisioning.
2. Treat requires-agnocast-kmod as descriptive metadata. A test listed in deps
   or data is not automatically executed first; invoke the checker through the
   test launcher rather than depending on a separate *_test target.
3. Prevent mutable host state from being hidden by a cached success. Consider
   the external test tag (forces execution), alongside appropriate local/device
   execution settings. Keep ordinary compilation cacheable. Confirm behavior
   against the selected Bazel/test-rule implementation before adopting it.
4. When multiple workers exist, consider an execution-platform capability or
   dedicated runtime toolchain/provider to select provisioned Agnocast workers.
   Platform declarations advertise a capability; they do not detect or load a
   module. Retain the runtime check, since a worker can reboot or lose device
   access after provisioning. Verify how the chosen rule schedules its test
   action instead of assuming compilation constraints also govern execution.

For target-owned kernels, a proposed test can depend on the generated bootable
image plus emulator and launch that image to exercise kernel/driver/application
behavior together. Producing a .ko alone does not establish compatibility with,
or loading into, the currently running host kernel. An OCI container shares its
host kernel; packaging a driver in its filesystem does not supply a running
kernel. A VM/system-image test can instead boot the generated kernel. Hardware
runners remain separate from build artifacts, with explicit board reservation,
flash/boot and cleanup steps. Emulation does not establish physical GPU, driver,
performance or real-time behavior.

Before selecting an option, pin the target architecture/ABI and relevant kernel
inputs, identify required privileges/devices, define build versus runtime gates,
and decide how to keep environment-dependent results fresh. Validate one small
boot/driver/application path before expanding scope.

References reviewed during the discussion:

- [Bazel platforms and toolchains](https://bazel.build/reference/be/platforms-and-toolchains)
  model declared execution/target capabilities.
- [Bazel common/test attributes](https://bazel.build/reference/be/common-definitions)
  document execution tags and external test-result behavior.
- [Kleaf](https://android.googlesource.com/kernel/build/+/refs/heads/main-kernel/kleaf/docs/kleaf.md)
  demonstrates Linux/Android kernel builds under Bazel.
- [Pigweed toolchain setup](https://pigweed.dev/toolchain/get_started.html)
  demonstrates Bazel cross-toolchain/platform selection for embedded firmware.

## Proposed milestone — reproduce scoped PointCloud2 loans under Bazel

Reproduce the Lyrical colcon result on a provisioned x86_64 Agnocast worker
as an opt-in communication experiment. Use the same three-process Universe
crop fixture, scoped shared allocation at both CPU writers, receiver-owned
Buffer views and blocking wait after graph initialization. Pin the experimental
RMW and its rclcpp changes from the colcon branch. Keep native Agnocast and
the longer publisher loan as paired controls. Validate cold Cyclone diagnostic
delivery, 60 verified frames and 60 publisher loans with zero private payload
copies on each scoped edge. Use 1.20× native median and p95 as an engineering
milestone. A single crossing in either direction cannot establish a regression
or readiness. Compare paired runs in rotated order, report their spread and
pooled results, and record child CPU, RSS, drops and extra payload copies.
When an effect is smaller than run-to-run variation, report it as unresolved.

Colcon met both pooled latency targets. Median exceeded the target in one
ordering. This milestone covers build and behavior reproduction on this
pipeline. Selecting a wrapper default requires separate workload evidence.
[Colcon implementation, commands and measurements](https://github.com/paulsohn/autoware/blob/e1699e0e4c7654c01f6dcfa8436851e3d63aa295/experiments/rmw-agnocast/README.md#scope-the-agnocast-allocation-to-point-storage).

## Commit and publication policy

Use signed-off Conventional Commits and validate each task in proportion to its
behavior. Work directly on local branch `bazel` and push validated commits to
`paulsohn/bazel`. Use force-with-lease only when intentionally replacing remote
history and only against an explicitly observed remote SHA.
