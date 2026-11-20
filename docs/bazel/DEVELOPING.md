# Developing Autoware with Bazel

This guide describes day-to-day development after Bazel has been adopted across
the project. During staged adoption, it applies only to packages and targets
that already have Bazel support. It is not an adoption or migration guide. To
add Bazel to an unsupported Autoware area, follow PLAN.md and the chronological
milestones and experiments in JOURNAL.md.

## Prerequisites

Use Ubuntu 24.04 x86_64 and install Bazelisk as `bazel`. The repository's
`.bazelversion` selects the tested Bazel release. Bazel supplies the ROS and C++
build dependencies; do not source a host ROS installation for Bazel commands.
Do not satisfy target dependencies with apt, pip, rosdep, or the repository's
Ansible roles. Declare them in the Bazel graph. Platform kernel drivers and the
minimal host tools needed to launch Bazel are host prerequisites, not target
dependencies.

Put machine-specific limits in the ignored `user.bazelrc`, for example:

```text
build --jobs=6
build --local_resources=cpu=6
```

Choose limits appropriate to the machine and run heavy builds sequentially.

## Daily build, test, and run loop

Run Bazel from the repository root in a clean caller environment:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build //path/to/package:target
```

Build the narrowest useful target while iterating. Before committing, build the
package or runnable slice affected by the change.

Run its tests with:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test //path/to/package:target_test
```

Use `--nocache_test_results` when the validation requires proof that a test
executed during the current task:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results //path/to/package:target_test
```

Run an executable and pass application arguments after `--`:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel run //path/to/package:node -- --ros-args
```

A successful build proves compilation and linking only. Use `bazel test` for
test behavior and `bazel run` for an end-to-end node or system check.

## Middleware configurations

Cyclone DDS is the default development middleware. No custom Cyclone DDS XML is
needed for local loopback communication. Add network configuration only for a
topology that demonstrates the need.

Fast DDS is the Tier 2 compatibility configuration. Routine validation uses
Cyclone DDS. Run Fast DDS checks only at major milestones after obtaining the
user's explicit approval for the check. After approval, add `--config=fastdds` to
the same build, test, or run command at the compatibility gate:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds //path/to/package:target_test
```

Do not let a Fast DDS-only workaround determine the primary Cyclone DDS path.

## Agnocast runtime tests

Agnocast support is compiled into supported wrappers unconditionally.
`--config=no-agnocast` selects their RCLCPP backend at runtime. Verify the
complete wrapper C++ suite with both runtime selections:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results \
  @autoware_agnocast_wrapper//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_heaphook \
  --test_env=ENABLE_AGNOCAST=1 --nocache_test_results \
  @autoware_agnocast_wrapper//:unit_tests
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test @autoware_agnocast_wrapper//:launch_tests
```

The enabled command builds the heap hook and preloads it from declared
runfiles using repository mapping. The component owns its Cargo setup in
`@agnocast_heaphook`; run allocator unit tests separately with
`bazel test @agnocast_heaphook//:unit_tests`, without production preload.
The enabled command requires a compatible Agnocast kernel module already loaded
and an accessible `/dev/agnocast`. Use local TestRunner execution because Agnocast
shares PID-named objects in the host `/dev/shm`; reused sandbox PIDs can collide.
Compile actions remain sandboxed. Check the GTest log for all 37 C++ cases and the pytest log for all 66
launch-action cases passing with no skips. Do not preload Python test drivers. Run Fast DDS variants only after approval at a major milestone.

## Interface declarations and bridge plugins

BUILD declarations define the interfaces supported by Bazel. Register an
interface and its dependencies explicitly when a consumer needs it. Undeclared
source files remain outside the build graph. An all-interface collection is
the declared dependency closure of its selected product roots.

Maintain the top-level selection in `bazel/interfaces/BUILD.bazel`. Each
`ros_interface_package(cc, shared, roots = ...)` entry pairs the package's
explicit C++ and shared-provider targets once. Its default selection is the
whole `cc` interface closure; supply `roots` to select individual interfaces.
For example:

```starlark
ros_interface_package(
    "@your_interfaces//:cc",
    "@your_interfaces//:cc_shared",
    roots = ["@your_interfaces//msg:YourMessage"],
)
```

`ros_interface_set(packages = [...])` combines those entries. Its optional
`roots` override can select application targets; collection follows their
declared `deps`, not actual C++ type usage. The corresponding package providers
must still be declared explicitly. No labels are guessed from interface names.

`//bazel/interfaces:all_interfaces` exposes the metadata closure, merged
`CcInfo`, shared-library provider and runtime files. A compiled consumer can
use that target in both `deps` and `dynamic_deps`; a metadata-only consumer can
read `RosInterfaceSetInfo`. Agnocast declares only `interface_set` on its plugin
macro and supplies its own Agnocast/RCLCPP dependencies internally. Package
registration belongs only in the central selection, not the plugin BUILD file.
The collection has no Agnocast dependency. The plugin rule iterates the interface
provider during analysis and uses native `expand_template` actions to produce
typed C++ factories under that target's output directory, then compiles and
links `libagnocast_bridge_plugins.so` in the same graph. It reuses the upstream
C++ templates and helpers without invoking the ROS verb, a Python renderer,
or a generated CMake package. On Lyrical, each service also gets a pub/sub
plugin for its synthesized `<Service>_Event` message, using the service header.
It accepts message and service interfaces; action bridges are unsupported.
Interface discovery does not use `AMENT_PREFIX_PATH`.

Test the plugin library with the kernel module available:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime --nocache_test_results \
  //bazel/agnocast:bridge_plugins_test
```

After approval, add `--config=fastdds` for a major-milestone compatibility gate.
The test uses the production BridgeLoader to select the declared
shared library and checks payloads in
both directions for standard/local messages, a service, and its event message.
Publisher peers run in separate processes because bridge subscriptions ignore local
publications. Add returned bridge callback groups to the mixed executor
explicitly; the factories create them with automatic registration disabled.
Run the complete template naming checks with
`bazel test //bazel/agnocast:template_tests`.
Run `bazel test @agnocastlib//... --local_test_jobs=1` without a global
preload. The five mocked unit groups and mocked executor integration stay
unpreloaded; kernel integration groups use their own component-owned heap-hook
launchers, and allocator integration supplies its mock before the hook.
Inspect explicit upstream skips in the test XML instead of treating a green
target as coverage for unsupported behavior.

The root runtime index composes the plugin library, heap hook, discovery agent,
configurator and component resources. Keep these payloads in test/application
`data`, not a `--run_under` tool's data, to preserve target RMW selection.
`with_runtime` sets resource and library paths before final-process startup;
Python launch tests use `launch_suite.py` to apply preload only after Python's
bootstrap. Use explicit nonempty `export_name` values on root ament indexes.
Component modules register executable aliases under `lib/<package>` using
`register_ament_files`, so launch and automatic agent startup find them.

Run all seven functional launch targets, the communication matrices and service
arrangements with the same runtime wrapper and `--local_test_jobs=1`. The
matrix targets are `//bazel/agnocast:matrix_1to1` (128 QoS/order/transport
configurations) and `//bazel/agnocast:matrix_2to2` (three modes × eight process
layouts); `//bazel/agnocast:launch_services` covers nine arrangements. Matrix
logs, immutable configuration copies and per-case JUnit reports are retained
under each target's `test.outputs` directory. They do not edit source YAML or
invoke the upstream colcon shell runners.

Keep Python extension metadata consistent with setup.py/setup.cfg. Build and
run `//bazel/agnocast:ros2` for the product's CLI/runtime closure; package-level
`@ros2agnocast//:ros2` supplies the extensions without the product resource
assembly.

## Source and package layout

Source snapshots under `src/` are ordinary tracked files imported as squashed
git subtrees. Edit them in place. Do not run `vcs import` or `vcs pull` into
`src/`.

Keep:

- workspace-wide Bazel support under `bazel/`;
- package-owned `MODULE.bazel` and BUILD files beside package source;
- original upstream tests in their existing package directories;
- all newly added tests, adapters and fixtures under `bazel/tests/<package>/`;
- root-level integration test targets and runtime composition under `bazel/tests/`;
- development and runtime container definitions under `images/`;
- integration strategy and evidence under `docs/bazel/`.

Interface collection and product plugin assembly belong under `bazel/`.
Assemble interface sets with `//bazel/interfaces:interfaces.bzl`; bridges,
loggers, and other root-module consumers can share the same provider. A
separate autoware_bazel source repository is deferred until a concrete need
justifies it. Components export their own inputs and never depend back on
metarepository paths. The workspace owns release selection and local
overrides; resolve rule-owned labels in their defining module.

Do not extend `repositories/`, `docker/`, or `ansible/` for Bazel work; they are
reference implementations for the existing build and deployment.

## Dependency conventions

### Repository dependency layers

| Location | Owns | May depend on |
| --- | --- | --- |
| bazel/ | Product composition, interface collections and standalone integration tests | src/ component modules and third_party/ providers |
| src/ | Component-specific source/build definitions and original upstream tests | Other component modules and reusable third-party providers |
| third_party/ | Independent SDK/dependency definitions, generic runtime/test helpers and dependency patches | Registry/toolchain dependencies and other reusable providers |

The root owns version selection from third_party/, registry releases and local
overrides. Consumers use logical module targets, not paths into their parent's
bazel/ directory. Root selects the CUDA fragment from
third_party/cuda/cuda.MODULE.bazel and the local TensorRT module from
third_party/tensorrt; .bazelrc keeps product/compiler/architecture selection.
Reusable pins and compatibility support live with the dependency they describe;
this does not transfer product selection to components or SDK consumers.
TensorRT is a standalone module; CUDA remains a root-included configuration
fragment with reusable build/patch support, without an artificial facade module.
Generic SDK test launchers belong with the SDK; Autoware-specific integration
adapters and fixtures remain under bazel/tests/.

In the root MODULE.bazel, list registry dependencies first and keep local modules
at the end, after extension and toolchain configuration. Within each dependency
group, place prerequisites before consumers where possible. Each local bazel_dep uses version
0.0.0 and is immediately followed by its matching local_path_override.

Use the local source snapshot for every supported package present under src/.
Use the ROS Central Registry for dependencies absent from the source checkout.
A local Autoware package owns a package-local Bazel module. The root module
selects the local snapshot with `local_path_override`; version metadata lives in
the package-local module.
Package-local dependency versions remain `0.0.0`; the root selects registry
releases for external dependencies and source overrides for local ones.

Removing a local override requires an explicitly requested source-selection
comparison and a compatible registry version. It is not part of the normal
developer loop. Add missing Bazel definitions when adopting a required local
package; absence of those definitions is not a reason to use registry sources.

For a C++ consumer using generated ROS libraries, pair compile and runtime
dependencies:

- depend on the generated package's `:cc` target;
- add its `:cc_shared` target to `collect_transitive_dynamic_deps`.

This preserves the RCR shared-library layout required at runtime.

NDT's kernel, node and C++ test consumers use the same SSE4.2 options on x86_64
because Eigen/SIMD settings affect their shared data layout. Keep those options
consistent when adding a direct C++ consumer. The OpenMP runtime is a Bazel-built
LLVM archive, imported by the package-local `:openmp_runtime` target; do not add
host `-lomp` search paths. Its kernel compiles with `-fopenmp`.

## Editing interface packages

To add a message or service to a supported package:

1. Add the `.msg` or `.srv` source and a matching `ros_message` or `ros_service`
   declaration in its interface directory's BUILD.bazel. Declare every
   referenced interface target in that rule's `deps`.
2. Add that target (for example, `//msg:NewMessage`) to the package-root
   `default_generators(deps = [...])` list. This registers it in the generated
   aggregate `:cc` target. Preserve existing entries.
3. For a newly referenced external package, update package.xml, MODULE.bazel,
   and `default_generators.dependencies` as appropriate.
4. Build the package's generated targets and affected consumers. If its `:cc`
   target is already a collector root, the new interface appears in
   `//bazel/interfaces:all_interfaces` through that dependency path. Build
   `//bazel/agnocast:plugins` to instantiate the expanded plugin set.

For example:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @your_package//... //bazel/interfaces:all_interfaces \
  //bazel/agnocast:plugins
```

Mixed packages containing nodes and interfaces work the same way. When the
collector root is a node or library, the new interface must be reachable
through that target's `deps`. If a package is not reachable from any root,
add or update its central `ros_interface_package` entry, including the selected
roots and explicit compile/shared providers. The plugin consumer needs no
matching list update.
Keep collection roots independent of the plugin library to avoid a cycle.
An interface source that is not registered remains outside the Bazel build.

When changing a `.msg` or `.srv` file, keep three dependency declarations
consistent:

1. retain manifest-level package dependencies in the package-local
   `MODULE.bazel`;
2. list every directly referenced interface target in the individual
   `ros_message` or `ros_service` rule;
3. list every package referenced by generated types in
   `default_generators.dependencies`.

Do not add a manifest-only dependency to generator inputs when no current
interface references it.

Build the complete generator surface, not only a C++ consumer:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_package_name//...
```

Then run a consumer that constructs or transports the changed type. At an
approved major milestone, repeat the bounded build and consumer test with
`--config=fastdds`.

## Editing C++ utility packages

Build and test a locally selected package through its external-repository
label, which exercises the same module boundary used by its consumers:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results @autoware_utils_math//...
```

Package-owned tests must declare their test framework as a normal direct module
dependency. Bzlmod ignores `dev_dependency` declarations in a module when that
module is consumed from the root workspace, including through a
`local_path_override`.

For C++ tests that do not supply their own main, depend on
`@googletest//:gtest_main`. Use `@googletest//:gtest` when the existing test
sources already define main.

A `cc_library` declares the RCR compile target, such as
`@rclcpp//:rclcpp_library`, in `deps`. Put the corresponding shared-library
target, such as `@rclcpp//:rclcpp`, in the `collect_transitive_dynamic_deps`
closure of each final `cc_binary` or `cc_test`. The library rule itself does not
accept `dynamic_deps`.

Apply the same pairing to generated services and directly used C libraries.
For example, compile logging utilities with `@logging_demo//:cc` and
`@rcutils//:rcutils_library`, then include `:cc_shared` and `:rcutils` in the
final executable's dynamic dependency closure. Declare dependencies referenced
by a public header directly even when another library also brings them into the
transitive graph.

Use a ROS dependency version of `0.0.0` in a package-local module only when the
root graph also selects a concrete release. A standalone `0.0.0` RCR module can
be a source-only compatibility stub with no BUILD overlay. If analysis reports
that the external repository has no BUILD package, inspect the resolved module
and pin the matching concrete RCR release at the root rather than inventing
labels for the stub.

When a header-only library sits above a local compiled library, its final
executables must still collect the shared ROS closure needed by that compiled
library. Static `cc_library` propagation supplies compile and link inputs, but
does not replace the RCR `cc_shared_library` runtime providers.

For Boost headers already mapped by the pinned rosdistro, use its narrow target
rather than adding the BCR umbrella module. For example, UUID consumers use
`@rosdistro//cc:boost.uuid`.

Package resources accessed through ament_index_cpp or ament_index_python need
an `ament_index` target from `@rosdistro//ament:defs.bzl`, with `package_xml`
and the required resources in `data`. Add that index to the consuming test or
binary's runtime `data`; it supports package-share lookup from runfiles without
sourcing a host ROS installation. For example, run the vehicle calculator with
its default indexed YAML:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel run @autoware_vehicle_info_utils//:min_turning_radius_calculator
```

Geography consumers can use `@autoware_geography_utils//:runtime_dynamic_deps`
for its shared-library closure and `:geoid_grid` as a declared data file. Set
GEOGRAPHICLIB_GEOID_PATH to the grid's runfiles directory in the consuming
process when EGM2008 is selected. Do not rely on an apt-installed geoid dataset.
The geography suite runs both grid-present and deliberately grid-absent cases;
the GNSS suites run against the real grid without skipping height conversion:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results @autoware_geography_utils//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results @autoware_gnss_poser//...
```

Ordinary RCLCPP component behavior tests need no Agnocast preload or bridge.
For root Python consumers of generated messages, set LD_LIBRARY_PATH=lib on
the py_test so transitive C introspection libraries resolve from the declared
runfiles prefix at process startup. Keep component resources and shared
libraries in data through its ament_index. For example:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --strategy=TestRunner=local \
  //bazel/tests:crop_box_filter_test
```

Tests using the compiled helpers or parser from autoware_test_utils declare
`@autoware_test_utils` in deps and `@autoware_test_utils//:runtime_dynamic_deps`
in dynamic_deps. Add its `:ament_index` to data for config, parser fixtures and
map URI resolution; do not use checkout-relative resource paths. Its optional
visualization header is not currently exported by these compiled-helper targets.
Use a test fixture to own ROS init/shutdown when the source has no custom main,
so context cleanup also runs after failed assertions.

## Generated parameter libraries

For an already-supported package using generate_parameter_library, load its
macro from `@generate_parameter_library//:defs.bzl`, pass the original parameter
schema as yaml_file, and depend on the generated target from the component.
The macro emits include/<package>/<target>.hpp using a Bazel execution tool.
Keep parameter defaults, validation and runtime configuration in the original
schema/config files; do not check in a manually generated replacement header.
Consumers using its runtime helpers include the generator package's rsl and
rclcpp_lifecycle shared providers in their dynamic dependency closure.

## Native ROS component nodes

For a component deriving directly from rclcpp::Node, use
`rclcpp_components_register_node` from `@rclcpp_components//:defs.bzl`. Supply
its shared component and plugin, and set `node_name` to generate the executable.
Register `<name>_record` in the package's ament_index and install the executable
through register_ament_files. Keep package-specific shared-library names so
libraries from multiple components can coexist in a runtime prefix.

Python integration peers need their C message type-support libraries available
to dlopen as well as their generated Python modules. Declare the package
ament_index in test data and check that its runfiles expose the exercised shared
libraries. Set the test's `LD_LIBRARY_PATH` to `lib`; `--run_env` settings
alone do not configure a native py_test environment. Native ROS-only nodes do
not interpret ENABLE_AGNOCAST as a backend switch.

When a launch starts an RCR component container, declare its executable-bearing
runtime provider. For the selected RCR, @rclcpp_components//:ament_package
supplies component_container_mt and its libexec layout; :ament_index contains
only test component metadata. A package exporting such a launch should compose
the runtime provider in its resource closure, so consumers need no host ROS.

## Switchable standalone nodes

Tests of wrapper endpoints must dispatch callbacks with the selected backend's
executor. Agnocast dispatches in spin(), not inherited RCLCPP spin_some(). For
bounded tests, spin asynchronously, cancel and join before inspecting received
data or destroying endpoints, and synchronize callback-written state.


For a wrapper-derived node using AgnocastOnlyCallbackIsolatedExecutor and a
SingleThreadedExecutor fallback, load `autoware_agnocast_node` from
`@autoware_agnocast_wrapper//:defs.bzl`. Supply the shared component, plugin class,
package name and executable name. The ROS fallback defaults to SingleThreadedExecutor;
set `ros2_executor = "MultiThreadedExecutor"` when the component needs callbacks
in separate groups to progress during a blocking service operation. Match the
component's existing executor contract. Add its `<name>_components` and `<name>_plugin`
records to the package's ament_index; register the executable under lib/<package>
with register_ament_files. Other executor pairs need explicit support before use.

Keep added behavior sources and fixtures under bazel/tests/<package>/. Root
targets select product runtime dependencies; package BUILD files expose original
upstream suites without introducing component-to-root labels.
Run ordinary RCLCPP fixture suites with --config=no-agnocast. Standalone
behavior targets explicitly select and verify their native child's backend:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors \
  --local_test_jobs=1 --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:vehicle_velocity_converter_agnocast_test
```

The enabled child needs /dev/agnocast, the declared heap hook and root bridge
runtime; the Python peer stays unpreloaded. The matching `_rclcpp_test` target
selects the RCLCPP backend. Check unittest logs for case totals; the acceleration
targets each run two cases while Bazel's fallback XML records one invocation.

## Embedded Python and plotting tests

Build native embedding code against `@rules_python//python/cc:current_py_cc_headers`
and `:current_py_cc_libs`. Use `@autoware_pyplot` for the C++ plotting API and
`@autoware_test_utils//:visualization` for its optional plotting helpers.

Run the original suites with the root runtime launcher:

```bash
bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --run_under=//bazel/tests:with_python \
  @autoware_pyplot//... @autoware_trajectory//... @autoware_route_handler//...
```

The launcher supplies Bazel's Python and wheel paths, pinned GNU wheel runtimes,
headless Agg rendering and a writable font cache. Its Linux extension-loader
configuration isolates wheel symbols from the native LLVM C++ runtime. Figures
are retained in each test's undeclared outputs; GTest writes the normal XML
report. Keep PLOT enabled and trajectory's ENABLE_TEST_PLOT=1 setting. The original
route-handler suite includes one upstream-disabled case, reported separately.

## Editing Python and launch packages

Declare the Python library's complete source set and direct runtime imports in
its package-local BUILD file. Reuse RCR Python targets and its pinned pip hub;
do not install dependencies with host pip. Keep generated ROS Python interfaces
as namespace packages: the workspace disables implicit initializer creation
across the graph, because an empty transitive __init__.py can hide generated
message imports.

Keep CLI/launch extension metadata in the runfiles dependency closure and
consistent with setup.py or setup.cfg. Adding a Python class alone does not
register a command or frontend extension. The Agnocast CLI builds its metadata
from setup.py; the wrapper carries its frontend registration as explicit BUILD
data and supplies setup.cfg to its install metadata.

Run each package's complete Python suite, for example
`bazel test @ros2agnocast//:tests` or
`bazel test @autoware_agnocast_wrapper//:launch_tests`. Check the pytest summary
in test.log for case totals; the current RCR pytest launcher leaves Bazel a
single fallback XML case for the invocation. Package resource metadata does
not replace a Python provider dependency such as `@agnocast_cie_config_msgs//:py`.
For recording/schema consumers, also include each selected package's
`:ament_index` in runtime data. Local `rosidl_ament_package` declarations depend
on their own generator's `:ament_index` to retain generated IDL and resource
markers. Python imports alone do not provide message definitions to rosbag.

Use `bazel run //bazel/agnocast:ros2 -- agnocast version` for the composed CLI.
Verify discovery, echo and recorded schema/payload with:

```bash
bazel test --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime \
  --nocache_test_results --test_output=errors //bazel/agnocast:cli_runtime_test
```


Python launch drivers must start without the allocator preload. Apply it after
interpreter startup to the launched native processes, as the root launch
adapter does. Preserve the host PID namespace for kernel-backed launches and
run configurator/socket tests sequentially so they do not compete for their
well-known Unix-domain socket.

## Development image

Build the pinned development image when its definition changes:

```bash
docker build -f images/dev.Dockerfile -t autoware-bazel-dev:bootstrap .
```

Use a named cache volume and mount the source read-only. Disable convenience
symlinks because Bazel cannot create them in the mounted workspace:

```bash
docker volume create autoware-bazel-bootstrap-cache
docker run --rm \
  -v autoware-bazel-bootstrap-cache:/home/ubuntu/.cache \
  -v "$PWD:/workspace:ro" \
  autoware-bazel-dev:bootstrap \
  bazel test --experimental_convenience_symlinks=ignore \
  --nocache_test_results //path/to/package:target_test
```

Add `--config=fastdds` to the Bazel command for the compatibility gate.

## Dependency patches

Keep dependency patches with their owner under `third_party/<dependency>/`
(optionally in a `patches/` subdirectory) and apply them through a pinned module
or repository override. Before the diff in each patch file, document the affected
module/version and target, observed failure, why the change fixes it, its scope,
verification commands/results or their journal reference, and upstream status
or tracking reference. Update this preface when the patch or its validation
changes. Verify that Bazel can still apply the documented patch.

## Validation and commits

Validate in proportion to the change:

- BUILD or module change: build every affected target surface;
- interface definition or declaration change: build generated targets and test
  affected consumers;
- library change: build and run the affected module's complete test suite;
- executable or communication change: run a behavior check;
- container change: build the image and run a representative target with the
  source mounted read-only;
- documentation-only change: run targeted documentation checks and state that
  no runtime validation was performed.

Use Cyclone DDS routinely. Run the complete changed module suite and relevant
behavior checks; do not repeat the entire communication matrix for every
commit or phase. Reserve full system matrices for communication-layer changes
or explicitly scoped milestones, and run Fast DDS only with approval. Reuse
passing results when code and configuration are unchanged. Document the actual
scope; an interrupted sweep is not a passing sweep.

Run only targeted format and static checks for files changed by the task. Do
not run pre-commit for this experiment.

Use signed-off Conventional Commits. Keep source compatibility changes in a
separate `fix(src/<repository>): ...` commit from Bazel build definitions. Do
not commit `user.bazelrc`, Bazel output trees, or convenience symlinks.

## Solver dependencies

The current OSQP/QP interfaces use the OSQP 0.6 API. Their osqp_legacy repository
alias selects BCR 0.6.3.bcr.3, while the root multiple_version_override preserves
RCR's OSQP 1.x selection for separate consumers. Keep the package-owned forwarding
headers for the osqp/ include namespace. Do not combine both ABIs in one process
without a separate compatibility design and validation. ProxQP uses the pinned
ProxSuite provider. Run each solver's full module suite after dependency changes;
for smoother runtime changes run both velocity_smoother backend tests under
//bazel/agnocast:with_runtime, with --config=no-agnocast for the RCLCPP target.
The enabled target requires the kernel module/device and built heap hook. See
[the solver/smoother experiment](JOURNAL.md#2026-11-17-phase-525--osqp-interface)
for input rationale, complete commands and coverage limits.

## Native plugin libraries

Keep the original plugin XML and its exported base-package category. Build a
cc_shared_library with shared_lib_name matching the XML library path, then
register it through ament_index(libraries=[...], plugin_categories={"plugins.xml":
"base_package"}). The map key is the XML label; the value is the package passed
to pluginlib::ClassLoader. Consumers declare the plugin package's :ament_index
as data so the XML, resource record and library are available in runfiles.
Do not add a component-to-root label or scan the host ament environment.

Validate each factory through a real consumer, including discovery and clean
shutdown. For behavior planners, loading uses the fully-qualified plugin class;
unloading uses the module name (stop_line). Run
//bazel/tests:behavior_velocity_planner_standard_runtime_test and
//bazel/tests:behavior_velocity_planner_experimental_runtime_test with
--config=no-agnocast after changing their plugin assembly.
Keep existing test helper sources in testonly libraries rather than linking
planning test managers into production components.

For the motion planner, the configured fully-qualified class name is also its
module name: use it for both load and unload. Run
//bazel/tests:motion_velocity_planner_runtime_test with --config=no-agnocast
after changing its obstacle-stop plugin assembly. Declare the real plugin index
on node tests that request it, alongside any intentionally unavailable plugin
names used to check error handling.

## Launch resource composition

Keep ROS executable names stable even when Bazel target labels differ. Use
register_ament_files renames under lib/<package> to expose the names declared by
original launch files/CMake. Rootpath-based binary checks do not verify this
contract. Give component shared libraries package-qualified shared_lib_name
values: resource indexes share one lib directory, so generic physical names
can select a different package's factory. Existing launcher/component macros
consume the actual shared-library provider; preserve their stable target labels.

For a package containing real launch/config resources, expose them through
ament_index and compose its runtime dependencies' indexes through deps. Declare
vehicle/model parameter inputs at launch boundaries. Run the original XML
through launch_test, checking configuration/remaps and clean exits. Core planning
uses //bazel/tests:core_planning_launch_test; core control uses
//bazel/tests:core_control_launch_test and
//bazel/tests:core_control_gate_launch_test, all with --config=no-agnocast.

Dynamic launch arguments do not create Bazel dependency edges. The global
parameter loader resolves `<vehicle_model>_description` at runtime; keep that
loader independent of a particular vehicle. The application or test bundle must
explicitly include its selected description's `:ament_index` in runtime `data`
and select that module in its dependency graph. For example,
`vehicle_model=autoware_sample_vehicle` requires
`@autoware_sample_vehicle_description//:ament_index`. A root `bazel_dep` and
`local_path_override` select the local source, but the runtime data edge is what
makes its resources available to the launched process. Include all supported
model descriptions when a bundle permits choosing among several models.

Vehicle rendering uses `@autoware_core_vehicle//:ament_index` plus the selected
vehicle and sensor-description indexes in the caller's runtime data. That
index composes the robot-state-publisher runtime provider and the xacro index,
which publishes `bin/xacro` for the original launch command substitution.
Do not rely on a host-installed xacro. The sample and AWSIM description checks
are `//bazel/tests:vehicle_description_sample_launch_test` and
`//bazel/tests:vehicle_description_awsim_launch_test` with `--config=no-agnocast`.

## Original test source layout

Keep original tests in their source packages and expose their inputs through
package-local rules. When a suite assumes sibling source directories, a root
adapter may stage declared runfiles into a temporary directory with the original
package names. Do not resolve that assumption through undeclared checkout paths
or accept dependency-related skips. Supply real generator executables through
runtime data/arguments. The interface-lint suite uses
`//bazel/tests:interface_spec_lint_tests`; it includes all original cases and
its committed-spec/manifest checks. Its original config determines active gates.

The example node uses native RCLCPP component registration, with its original
ROS executable name registered under lib/autoware_test_node. Its index owns
that executable registration; avoid an executable-to-same-index data edge.
Use `//bazel/tests:autoware_test_node_launch_test` for standalone/component
checks and `//bazel/tests:autoware_testing_original_smoke_test` for the original
generic smoke helper, both with `--config=no-agnocast`.

## Headless core composition

The original autoware_core launch accepts `launch_rviz=false`; it defaults to
true for existing GUI callers. `@autoware_core//:ament_index` supplies the
headless runtime/resource closure. Include concrete vehicle and sensor indexes
in the caller's data and pass their model names explicitly; GUI bundles must
also provide an RViz runtime index. Map bundles contain lanelet2_map.osm,
map_projector_info.yaml and pointcloud_map.pcd (plus metadata for multi-file maps).
Validate aggregate startup, configuration, map/model publications, velocity relay,
API service and clean shutdown with
`bazel test --config=no-agnocast //bazel/tests:core_system_headless_launch_test`.
This gate does not assert a full driving scenario.

## Native core distribution

The isolated runtime gate supports Docker Engine directly. Select a working
context and make the pinned base image available in that daemon's image store;
switching contexts does not transfer cached images. Docker Desktop is optional.
After adding Docker group membership, start a new login session or use
`sg docker -c 'docker version'` so the command receives the updated groups. See
[Docker's Linux post-installation instructions](https://docs.docker.com/engine/install/linux-postinstall/).
Keep context, credential-helper and service preferences in local host settings.

Build the application and its separate verification companion with the bundled
Python bootstrap configuration:

```sh
bazel build --config=runtime --config=no-agnocast \
  //bazel/runtime:core_bundle //bazel/runtime:core_bundle_tests
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors //bazel/tests:core_system_headless_launch_test \
  //bazel/tests:core_bundle_structure_test
```

The application archive contains the headless original core launch and sample
vehicle/sensor providers. Its executable-adjacent runfiles preserve the declared
ament indexes, libraries, Python and resource closure. Add component runtime
edges to the owning component's index/data; select concrete product resources
in bazel/runtime/BUILD.bazel. Do not add a second package-by-package file list.
Conflicting archive destinations fail. Build distributions with --config=runtime;
ordinary Python bootstraps otherwise require python3 before reaching their
declared interpreter.

Keep generated standalone mains' runtime providers in their helper package's
index too. A dependency used by a main can be absent from its component library's
closure; a working build-tree RPATH or an installed host library can hide that
omission. The wrapper index exports its shared logging provider for this reason.

The root native_runtime target composes real GLVND dispatch libraries and their
X11 load dependencies with the existing pinned GNU C++/GCC runtimes and the
registry's source-built zlib. Its libz.so.1 alias serves native Python extensions.
The [graphics provider](../../third_party/graphics_runtime/README.md) locks binary
payloads by SHA-256; Bazel extracts them without a host package manager. Registry
GL stubs are link inputs and cannot substitute for runtime implementations.
The supported runtime baseline is Ubuntu 24.04 x86_64, rather than a claim that
the compiler's lower glibc floor covers every runtime input. A graphics vendor
backend/display stack is outside this headless product's support.

Extract at any directory and invoke its run executable from another working
directory, passing map_path:=/absolute/path/to/map. The map directory must contain
the three files described above. The launcher establishes its own runtime paths
and fixes the headless sample product to Cyclone DDS with Agnocast disabled.
Supply writable HOME/ROS_LOG_DIR state separately from the read-only application.
This product does not contain RViz, GPU execution, a simulator or an Agnocast
kernel/runtime setup.

Extract core_bundle_tests.tar.gz separately to run runtime/verify with the
application's absolute run path as its first argument. Set TEST_TMPDIR to an
existing writable state directory. The companion owns its peer/fixture runtime;
the product resolves its own independent index. The same two core test cases
check behavior and shutdown, including nested child exits, loaded library paths
and surviving processes when testing an external bundle. Keep this companion
out of the deployed application. See the
[Phase 7.1 journal](JOURNAL.md#2026-11-20-phase-71--native-headless-core-distribution)
for the recorded relocation/isolation commands and supported OS baseline.

## CUDA targets and tests

The product root owns version selection from third_party/: it includes the
pinned CUDA configuration in third_party/cuda/cuda.MODULE.bazel, while .bazelrc
selects the product GPU architecture and compiler. Consumer modules declare rules_cuda and
use its toolchain extension's `cuda` repository; they must not auto-detect a host
CUDA installation or depend on metarepository labels. The current toolkit is
CUDA 13.2.2 with upstream rules_cuda 0.4.0, nvcc and hermetic Clang/libc++ 21.1.8.
The default product GPU target is sm_120. This x86 libc++ configuration uses an
explicit policy override and pinned CRT compatibility patches; it is tested
here but not vendor-supported. Revalidate the patches when changing either SDK
or standard library. ARM/SBSA has not been validated.

For the existing CUDA-utils header target, depend on `@autoware_cuda_utils`.
Its direct CUDA runtime/CCCL providers replace the CMake-only dependency-meta
shim for the current non-LTO configuration. If LTO is introduced, inspect the
actual compile/link actions and rerun the complete affected suite before
assuming the original shim's -fno-lto effect is still satisfied.

Compile CUDA test sources with cuda_library and retain static-initializer test
registration with alwayslink. Keep the original test main in a cc_test and
link gtest without a second main. Run the complete module gate with
`bazel test --config=no-agnocast --nocache_test_results --test_output=errors @autoware_cuda_utils//...`.
The GPU test runs locally with device access (no-remote/no-sandbox/requires-gpu).
The host provides the compatible NVIDIA driver/device, not a build toolkit.
Check its XML for all 17 cases and zero skips/errors/failures; a missing-device
skip or zero registered cases is not a completed module gate.

## TensorRT SDK and native tests

Consumers declare `@tensorrt` for the selected headers/shared libraries and the
CUDA runtime explicitly. The current local SDK module pins TensorRT
10.16.1.11-1+cuda13.2 on Linux x86_64; it has no ARM or JetPack 10.16.2 support
claim. Vendor headers are system includes, leaving package warnings intact.
Keep CMake-only finder logic out of Bazel when direct headers/libraries/version
providers satisfy the actual consumer.

For native GTest targets that build engines, load `tensorrt_cc_test` and
`TENSORRT_VERSION_MAJOR` from `@tensorrt//:defs.bzl`. The helper supports source,
dependency/dynamic-dependency, data and local-definition inputs; its native
binary uses the SDK-owned runtime launcher. Declare model fixtures with data and
rootpath definitions. The launcher resolves runtime anchors through runfiles,
adds the SDK/GNU directories for bare-name builder dlopen, and preserves native
GTest XML/exit status. Never link the builder-resource libraries directly; their
SONAMEs intentionally prohibit that use. The helper's runtime data includes
pinned GNU runtimes and the builder resources, not incidental host SDK installs. A
caller-owned fixture executable can be supplied through run_under; it receives
the native command after SDK runtime setup. Keep application fixture preparation
under bazel/tests/, and set timeout="long" for genuine multi-minute model builds.
The wrapper must preserve native exit/XML results and keep engine-cache outputs
in writable temporary storage.

Run `bazel test --config=no-agnocast --nocache_test_results --test_output=errors @autoware_tensorrt_common//...` on a provisioned GPU host.
Check XML for all 13 cases with no skips. The separate CUDA-utils regression
remains all 17 cases. Root SDK selection, GPU code generation and driver
prerequisites follow the preceding CUDA convention. Ordinary applications need
the equivalent declared runtime/resource loading contract; a successful link
alone does not prove engine-building runtime closure.

## Sparse SDK and TensorRT plugins

The pinned spconv 2.3.8/cumm 0.5.3 SDK exposes GNU C++ containers. Use
@spconv//:defs.bzl's spconv_cc_shared_library or spconv_cc_test at its consumer
boundary to compile that closure with the pinned GNU 13.3.0 platform; do not mix
its C++ headers/container API into libc++ code. Keep ordinary implementation
cc_library targets beneath the configured shared target, with dynamic C++
runtimes and explicit dynamic_deps for separately exported libraries.

Native libc++ consumers load @autoware_tensorrt_plugins//:plugins as runtime data
through dlopen, as TrtCommon does, and exchange only TensorRT's versioned opaque
interfaces. Check getInterfaceInfo rather than using cross-runtime RTTI. Do not
call the registry's loadLibrary API on this self-registering plugin DLL. No
STL/container ownership crosses the standard-library boundary.

For a native TensorRT test requiring this plugin, supply
libstdcxx_runtime="@spconv//:gnu_runtime" to tensorrt_cc_test and declare the DLL
in data. The helper resolves that source-built runtime before the vendor SDK's
bare-name builder loads; it does not search a host GCC installation. Other
TensorRT tests retain the default runtime. Run the complete package and added
interop gates sequentially on a supported GPU:

```sh
bazel test --config=no-agnocast --nocache_test_results --test_output=errors @spconv//:abi_test @autoware_tensorrt_plugins//...
bazel test --config=no-agnocast --nocache_test_results --test_output=errors //bazel/tests:tensorrt_plugins_test
```

The original suite has eighteen cases; the added suite has two, covering all
eleven creator names and an actual serialized Argsort engine. These do not
establish numerical coverage for every sparse plugin or ARM compatibility.

## Perception utilities

Consumers using only mask run-length encoding depend on
@perception_utils//:run_length_encoder_library and its :run_length_encoder
shared provider. The full :perception_utils_library/:perception_utils pair
composes that same codec with remapping and suppression utilities. Select the
needed target rather than making a codec-only consumer own unrelated geometry
and planning dependencies. Run @perception_utils//... after changes: all eight
original cases across three targets must pass.

## Model resources

src/models/tensorrt_yolox owns module/package autoware_tensorrt_yolox_models.
Its :models filegroup exposes weights/metadata and :ament_index registers their
original filenames in its own share directory. Runtime consumers declare that
index and resolve it through ament_index or $(find-pkg-share autoware_tensorrt_yolox_models); do not construct source-checkout or HOME paths.
Test consumers copy ONNX files to writable temporary storage for engine caches.

Install Git LFS before checkout and materialize its objects before building.
The parent repository uses its own normal remote LFS storage, retaining the
upstream nested .gitattributes. Model edits require updated SHA256SUMS and
associated metadata; push LFS objects together with the Git changes. Do not
redirect the whole repository to the upstream Hugging Face LFS endpoint, which
cannot receive this project's edited weights. MODEL_PROVENANCE.md inside the
model repository records its immutable import revision.

Run //bazel/tests:yolox_model_resources_test to check all five payload hashes and
a relocated standalone index. Colcon users build the model package with
--symlink-install and source its install/setup.bash; the model files remain
symlinks. Upstream HF refs alone lack the added package/build metadata.
The repositories/ and ansible/ reference downloads are not runtime resource providers.
Other model repositories are adopted at their actual consumer boundaries.
The five affected YOLOX launcher files use the indexed package exclusively;
run //bazel/tests:yolox_launch_paths_test after changing their defaults or
runtime dependencies. It resolves actual launch substitutions and checks the
two launcher manifests. Remaining model families retain their migration work;
this test does not launch the full perception stack.

## TensorRT classifier

Use @autoware_tensorrt_classifier//:autoware_tensorrt_classifier_library for C++
headers/link inputs and :autoware_tensorrt_classifier for dynamic_deps; its index
contains the classifier and GPU preprocessing libraries. CPU preprocessing uses
the pinned LLVM OpenMP archive. Supply model paths as declared runtime inputs,
with writable engine-cache copies for tests.

Run //bazel/tests:tensorrt_classifier_test with --config=no-agnocast on a supported
GPU. All five cases must pass; the fixture generator beside the test reproduces
the tiny checked-in ONNX model. doInference currently always selects CPU
preprocessing, including with cuda=true; direct CUDA preprocessing is a separate
entry point with RGB/default-normalization behavior. The test checks both
contracts independently, not parity or INT8 accuracy.

## YOLOX inference

Use @autoware_tensorrt_yolox//:autoware_tensorrt_yolox_library and its matching
shared provider; node_library/node_component own the ROS component. The package
index registers both original executables/components, configuration, launch
files and the original test image. Declare the debug-message :cc_shared provider
when using its debug publishers: static generated code alone does not supply
ROS's dlopen typesupport closure. Tests linking image_transport also need its
indexed plugin resources, composed by the YOLOX package index.

Run the whole original suite on a supported GPU with materialized model data:

```sh
bazel test --config=no-agnocast --local_test_jobs=1 --nocache_test_results --test_output=errors @autoware_tensorrt_yolox//... //bazel/tests:yolox_detector_test //bazel/tests:yolox_node_test
bazel test --config=no-agnocast --nocache_test_results --test_output=errors //bazel/tests:yolox_fixture_adapter_test
```

Expect ten label cases, three detector cases and one ROS-node case with zero
skips. The root fixture adapter copies declared ONNX/metadata resources into
temporary storage and sets the original suites' model-directory override. The
separate adapter test ensures skipped/empty native results fail the gate. The
real-model tests use FP16; they do not establish INT8 calibration accuracy.

## Negotiated and CUDA blackboard transport

The negotiated library and examples use native rclcpp and need no GPU. Run all
library cases with @negotiated//... and all original example launch/standalone
contracts with //bazel/tests:negotiated_examples_tests, using --config=no-agnocast
and serialized tests. The example module owns its executable/ament records;
added behavior adapters remain under bazel/tests/negotiated_examples/.

CUDA blackboard requires an actual compatible GPU/driver for its GPU cases.
It compiles Agnocast support unconditionally; the compatible publisher and async
worker select their backend at runtime. Its original demo components remain
native rclcpp components. Preserve the single shared blackboard library so
components in the same process see the same GPU buffer registry.

Run the complete module and added payload/component checks sequentially:

```sh
bazel test --config=no-agnocast --local_test_jobs=1 --nocache_test_results \
  --test_output=errors @cuda_blackboard//:unit_tests \
  //bazel/tests:cuda_blackboard_payload_test //bazel/tests:cuda_blackboard_component_test
bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --run_under=//bazel/agnocast:with_heaphook --test_env=AGNOCAST_BRIDGE_MODE=off \
  @cuda_blackboard//:agnocast_tests //bazel/tests:cuda_blackboard_payload_test
```

The worker-announcement unit case explicitly sets ENABLE_AGNOCAST=1. Enabled
transport needs the loaded compatible module/device and heap hook; these direct
transport tests disable the bridge, so they do not establish bridge behavior.
Do not apply the heap-hook runner to targets without its declared runtime data.
Check XML for all eight original cases, three enabled repeats, three added
payload cases per backend and two component cases, without skips. The component
test invokes the original image launch and validates exact pixels, frame and
stamp. The payload test checks image/cloud device round trips and a separate
process receiving the async publisher's image through the selected backend.
