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

Interface identities use package/kind/Name, where kind is msg, srv or action
from the source extension. Nested source categories such as msg/object_recognition/
do not add a ROS namespace; consumers use the generated package/msg headers.

Maintain the top-level selection in `bazel/interfaces/BUILD.bazel`. Each
`ros_interface_package(cc, shared, roots = ...)` entry pairs the package's
explicit C++ and shared-provider targets once. Its default selection is the
whole `cc` interface closure; supply `roots` to select individual interfaces.
An explicit roots list remains a fixed subset when other interfaces are added
to the package. Expand that list or select the whole provider when the product
starts consuming more of the package.
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

Python/introspection consumers use `//bazel/interfaces:all_interfaces_python`.
The same package entries supply generated Python bindings and resource indexes;
there is no second package list. Ordinary `:cc` providers default to sibling
`:py` and `:ament_index` targets. Supply `python` and `index` explicitly for
nonstandard providers, as for grid_map. These are package-wide providers even
when metadata roots select fewer types. Declared interface dependencies forward
their indexes transitively, so nested message definitions remain discoverable.
This optional projection does not add Python dependencies to C++ consumers.

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
- root-level integration test targets and verification archives under `bazel/tests/`;
- runnable product composition, launchers and runtime configuration under `bazel/runtime/`;
- native bundles, OCI/OSTree assembly, install layouts and service definitions under `bazel/deployments/`;
- only the optional builder bootstrap recipe, `dev.Dockerfile`, under `images/`;
- integration strategy and evidence under `docs/bazel/`.

Deployment targets consume the product's declared runtime closure; maintain
one selection of application resources. Runtime products and source components
must not depend on deployment formats or test targets. Tests may consume both
products and deployment artifacts, with their verification companions marked
testonly. Reusable SDK/build support stays with its third-party owner.

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
| bazel/ | Product composition, interface collections and standalone integration tests | src/ component modules, assets/ resource providers and third_party/ providers |
| src/ | Component-specific source/build definitions and original upstream tests | Other component modules, public asset modules and reusable third-party providers |
| assets/ | Deployment-selectable resource inputs, metadata and resource indexes | Generic registry/build providers; no application or product targets |
| third_party/ | Independent SDK/dependency definitions, generic runtime/test helpers and dependency patches | Registry/toolchain dependencies and other reusable providers |

With arrows meaning "supplies inputs to", the layers are
(BCR/RCR + third_party/) -> src/ -> bazel/, plus assets/ -> src/ and bazel/.
Asset modules/resource packages use the `_asset` suffix. A component may declare
a logical asset-module dependency and consume its public index; keep physical
asset paths/revision selection in root overrides, with no component-to-root labels.
See the [asset convention and deployment sketch](../../assets/README.md).

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

### Recursive discovery and IDE targets

The root .bazelignore excludes src/, assets/ and standalone third-party module
directories from the main repository's recursive package discovery. Those
directories are consumed through local_path_override and their logical module
labels. Keep this list updated when adding a standalone module outside the
already excluded src/ and assets/ trees; keep metarepository-owned third-party
helpers available under their normal //third_party/... labels.

Do not load the same standalone BUILD through its physical parent-repository
path. For example, use @spconv//:spconv or @spconv//..., rather than
//third_party/spconv:spconv. The latter applies the root repository mapping to
module-owned .bzl loads, so dependencies declared only by spconv can appear
missing. Adding private SDK dependencies to the root does not correct that
repository boundary.

For IDE integration, select the runnable/library targets being developed and
their dependency closure, or explicitly include the relevant @module//...
roots. Root //... discovers metarepository packages; it does not enumerate
external modules, including local overrides. Excluding a physical directory
from root discovery leaves its module targets available. These are Bazel label
conventions; configure the IDE's target selection accordingly.
See Bazel's [target patterns](https://bazel.build/run/build#specifying-build-targets)
and [.bazelignore](https://bazel.build/run/bazelrc#bazelignore) documentation.

### Dependency declarations

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

For an external registry package that resolves to 0.0.0 without BUILD rules,
inspect registry metadata and published overlays before introducing a provider.
Pin a usable published release at root while keeping component requirements at
0.0.0; validate that dependency's original suites and its affected consumers.
The source-only placeholder is not evidence that every registry release lacks
support.

Removing a local override requires an explicitly requested source-selection
comparison and a compatible registry version. It is not part of the normal
developer loop. Add missing Bazel definitions when adopting a required local
package; absence of those definitions is not a reason to use registry sources.

When changing a registry/provider version, inspect its transitive build-rule
requirements as well as its source version. Avoid incidental upgrades of shared
rule/toolchain baselines; validate the accepted dependency and affected consumers.
Header-only providers must declare transitively included template files such as
.tcc/.ipp, alongside .h/.hpp files; export the complete upstream include tree
where appropriate.

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
For a mixed package, select the generated providers directly, for example:

```starlark
ros_interface_package(
    "@autoware_planning_validator//:cc",
    "@autoware_planning_validator//:cc_shared",
)
```

The package's node/plugin library remains a separate target. Its generated
interfaces, owned package index and implementation may share one Bazel module;
collecting the generated provider does not require linking the node into the
bridge. Register service-only providers too, such as the AD API version service.
The descriptor test checks canonical identities and selected supported types;
it does not check source-directory completeness or discover undeclared files.

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

For a registered interface provider, keep a product-owned serialization check
under `bazel/tests/<package>/BUILD.bazel`:

```starlark
load("//bazel/tests/ros_interfaces:defs.bzl", "interface_serialization_test")

interface_serialization_test(package = "your_package")
```

Run `bazel test --config=runtime --config=no-agnocast
//bazel/tests/your_package:all` alongside affected native consumers. The helper
round-trips the generated public message types and service Request/Response/Event
types through native CDR support. Preserve package-specific payload tests too;
default-value round trips do not establish nondefault payload or service behavior.
This check does not inspect undeclared source files or enforce registration.

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
with register_ament_files. For a component deriving from rclcpp::Node, set
`agnocast_only = False` to select CallbackIsolatedAgnocastExecutor and its
get_node_base_interface contract; do not cast it to wrapper::Node. Other
executor pairs need explicit support before use.

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

For the learning-based vehicle connector, InterconnectedModel owns its Python
interpreter. Create the model before using Python APIs, and do not wrap it in a
second scoped_interpreter. Python-owned submodels must be released before
interpreter shutdown. Run its root integration fixture with the same launcher:

```sh
bazel test --config=runtime --config=no-agnocast --nocache_test_results \
  --run_under=//bazel/tests:with_python \
  //bazel/tests/autoware_learning_based_vehicle_model:embedding_test
```

## Simulation primitives

The simulator, dummy perception and door simulator expose their original native
executables and launch/parameter resources through package-owned ament indexes.
Run each module's complete original suite and its root launch gate together:

```sh
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_simple_planning_simulator//... \
  @autoware_dummy_perception_publisher//... @autoware_vehicle_door_simulator//... \
  //bazel/tests/autoware_simple_planning_simulator:launch_test \
  //bazel/tests/autoware_dummy_perception_publisher:launch_test \
  //bazel/tests/autoware_vehicle_door_simulator:launch_test
```

Use the separate embedded-Python command above for the learning connector.
When including simple_planning_simulator.launch.py, supply its caller-owned
initial_engage_state, motion_publish_mode and vehicle_model_pkg configurations,
and the vehicle_info_param_file from the selected indexed description. Keep
these choices in launch composition. The primitive fixture uses the core
sample description; original AWF workflow adoption belongs to the
[simulator milestone](PLAN.md#awf-planning-simulator-milestone).

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

The optional base image provides bootstrap tools for running Bazel builds.
Source, Bazel-managed dependencies and application outputs are supplied or
created when the container runs. Keep deployment-image rules under
bazel/deployments/; images/dev.Dockerfile is the only file under images/.
Do not maintain an image containing a prebuilt workspace or populated Bazel
cache. A source mount and persistent cache volume support normal incremental
development; an offline/prepared image needs a separate concrete requirement.

Build the development image when its definition changes:

```bash
docker build -f images/dev.Dockerfile -t autoware-bazel-dev:bootstrap .
```

The Dockerfile pins its Ubuntu base and Bazelisk version, but its bootstrap apt
packages come from live repositories. Record the built image digest when an
exact bootstrap environment must be preserved. Target dependencies remain
selected by Bazel; this image does not replace their pins or host kernel/device
prerequisites.

Use a named cache volume and mount the source read-only for build validation.
Use a writable source mount when editing inside the container. Disable
convenience symlinks for a read-only workspace:

```bash
docker volume create autoware-bazel-bootstrap-cache
docker run --rm \
  -v autoware-bazel-bootstrap-cache:/home/ubuntu/.cache \
  -v "$PWD:/workspace:ro" \
  autoware-bazel-dev:bootstrap \
  bazel test --experimental_convenience_symlinks=ignore \
  --nocache_test_results //path/to/package:target_test
```

Fast DDS compatibility checks require explicit approval before adding
`--config=fastdds` to the Bazel command.

## Dependency patches

Keep dependency patches with their owner under `third_party/<dependency>/`
(optionally in a `patches/` subdirectory) and apply them through a pinned module
or repository override. Before the diff in each patch file, document the affected
module/version and target, observed failure, why the change fixes it, its scope,
verification commands/results or their journal reference, and upstream status
or tracking reference. Update this preface when the patch or its validation
changes. Verify that Bazel can still apply the documented patch.

## Expensive targets

Costly system checks and distribution assembly are disabled by default. Enable
them explicitly with --config=expensive, then select only the targets needed
for the current change. The configuration unlocks targets; it does not schedule
an entire suite or change the middleware.

The opt-in covers native application/verification archives and their dependent
OCI/OSTree artifacts and checks, plus the original Universe simulator launch
suite. Ordinary package tests and runnable products such as
//bazel/runtime:planning_simulator remain available without this configuration.
Wildcard builds/tests skip disabled targets. Explicitly requesting a disabled
target reports an incompatible-target error; enable the configuration to run it.
Disabled or skipped tests do not count as passing a required completion gate.

Use affected package builds/tests for routine development. Run the relevant
expensive gate when changing product composition, runtime resources, packaging
layout, service supervision, export rules or a gate's behavior, or when an
explicit milestone requires it. Reuse passing results for unchanged inputs;
docs, labels or configuration-only changes can often use analysis with --nobuild
and focused lightweight checks. Identical artifact hashes can support reuse of
the existing runtime evidence. Full simulator driving and OCI export assembly
are not automatic follow-ups to every task or commit. Preserve complete module
acceptance whenever that module's behavior or runtime closure is affected.

For example, when simulator distribution behavior needs verification:

```sh
bazel test --config=expensive --config=runtime --config=no-agnocast \
  --local_test_jobs=1 --test_output=errors \
  //bazel/tests:planning_simulator_native_runtime_test
```

For an export-label change with unchanged assembly rules and payloads, analyze
the export without producing another large archive:

```sh
bazel build --config=expensive --config=runtime --config=no-agnocast --nobuild \
  //bazel/deployments:planning_simulator_oci
```

The shared --config=expensive sets //bazel:enable_expensive_targets=true.
An optional local default can be set in ignored user.bazelrc:

```text
# Enable only while working on costly system/deployment targets.
build --//bazel:enable_expensive_targets=true
```

Omit that override for normal development. Pass
--//bazel:enable_expensive_targets=false to disable it for an individual command.
Keep machine resource limits in user.bazelrc independently of this opt-in.
Fast DDS still requires separate explicit approval.

## Validation and commits

Validate in proportion to the change:

- BUILD or module change: build affected target surfaces; label/selection-only
  changes can use analysis and focused checks when action inputs and runtime
  behavior are unchanged;
- interface definition or declaration change: build generated targets and test
  affected consumers;
- library change: build and run the affected module's complete test suite;
- executable or communication change: run a behavior check;
- builder bootstrap change: build the development image and validate an
  affected target with the source mounted read-only;
- deployment change: select the relevant artifact/behavior gates using the
  [expensive-target convention](#expensive-targets);
- documentation-only change: run targeted documentation checks and state that
  no runtime validation was performed.

Use Cyclone DDS routinely. Run the complete changed module suite and relevant
behavior checks; do not repeat the entire communication matrix for every
commit or phase. Reserve full system matrices for communication-layer changes
or explicitly scoped milestones, and run Fast DDS only with approval. Reuse
passing results when code and configuration are unchanged. Document the actual
scope; an interrupted sweep is not a passing sweep.

For feature-gated native dependency tests, preserve the original driver as well
as its executable. Check that required backends actually ran: an exit-zero
"unsupported" path does not satisfy the gate. Preserve driver contracts such as
argv[0], working directory, repetitions and fixture paths when adapting them to
runfiles. Keep generic adapters with the dependency owner.

For ROS time-window behavior, cover the epoch and exact horizon/retention
boundaries. Preserve zero-based fixtures. Compare ages as signed Durations;
avoid creating negative absolute Time cutoffs while history is warming up.

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
When a native component uses the broad `@autoware_utils` facade, include
`@autoware_utils//:runtime_dynamic_deps` in its dynamic-dependency aggregate.
Header-facing dependencies can still pull static SDK objects into a DSO;
PCL/conversions/VTK globals must remain in their existing shared owners.
Check actual ELF exports/dependencies and unload multiple components together;
a standalone process exiting once does not exercise cross-DSO ownership.

Keep existing test helper sources in testonly libraries rather than linking
planning test managers into production components.

For dependency repositories containing several ROS packages, give each ament
index and interface declaration an explicit ROS package identity (`export_name`
and `package` in RCR rules). Keep original package/library names independent
of Bzlmod repository names. Publish a transitive dynamic-dependency provider for
native consumers of inline/plugin APIs; a raw shared-library target alone need
not link their direct references to its dependencies. See the
[grid-map provider conventions](../../third_party/grid_map/README.md).

The CPU laserscan occupancy target is
@autoware_probabilistic_occupancy_grid_map//:laserscan_based_occupancy_grid_map_node.
Its resource index declares the original launcher and static converter/container
providers. Run //bazel/tests/occupancy_grid:launch_test for original-launch
conversion/grid/shutdown checks. GPU pointcloud/fusion targets are not declared
by this CPU surface.


For the motion planner, the configured fully-qualified class name is also its
module name: use it for both load and unload. Run
//bazel/tests:motion_velocity_planner_runtime_test with --config=no-agnocast
after changing its obstacle-stop plugin assembly. Declare the real plugin index
on node tests that request it, alongside any intentionally unavailable plugin
names used to check error handling.

A plugin's parameter lifetime belongs to its hosting node. If that node supports
unload/reload, initialize parameters with read-or-declare semantics and preserve
current values. Verify a real reload after a parameter update; constructing a
plugin only once cannot expose repeated-declaration failures.

Keep plugin-private C++ types unique within the process, using a module namespace
or a distinct type name. Separate DSOs do not isolate incompatible definitions
with the same C++ identity: exported weak constructors/destructors can bind to
another plugin's layout. Validate selected factories together as well as alone.
For subprocess log capture, read a shared regular-file descriptor with os.pread;
seeking it also changes the child's write offset and can corrupt captured output.

## Launch resource composition

For file-only inspection, export the required XML/configuration files and
manifest from their owning source package with exports_files. A package-local
MODULE.bazel can expose that resource-only boundary without acquiring every
node referenced by its launch files. Consumers under bazel/ select logical
module labels and compose their fixtures there; avoid checkout-relative src/
paths in a metarepository filegroup. The root selects the local module override.
For example, @autoware_perception_launch currently exports only the YOLOX
launcher-check inputs. It has no runtime :ament_index target; adding that
target requires its selected runtime dependency closure and launch gate.

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

Before ending a composed launch test, poll each container's
composition_interfaces/srv/ListNodes service for the complete selected set.
A node can appear in graph discovery while its constructor is still running.
For standalone nodes, request a parameter response to establish executor
readiness, and provide real constructor dependencies such as map-loader
parameter services. Require clean exit codes from every launched process.

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

Graphical consumers also exercise the platform driver's native ABI closure.
The declared graphics runtime pins GLVND/X11 and GNU libgcc/libstdc++ 14.2; the
runtime platform supplies the display and its compatible rendering driver.
Run //third_party/graphics_runtime:all with authenticated DISPLAY/XAUTHORITY when
changing that boundary. An EGL dispatch-only check does not prove that a Mesa
or vendor GLX driver can load and create RViz's visual.

## RViz plugin packages

Each supported plugin package owns its MODULE/BUILD declarations, original
plugin XML, icons/fonts/meshes and one indexed runtime. Build its implementation
with qt_cc_library and publish its original library filename through
qt_cc_shared_library. Use the selected Qt moc rules, explicit Q_OBJECT headers
and shared ROS/Qt/Ogre providers; preserve original QT_NO_KEYWORDS/visibility
flags. If moc headers share a basename, isolate their compilations in separate
qt_cc_library targets rather than renaming the source headers.

Keep compile providers and shared owners distinct. Collect concrete SDK dynamic
dependencies with collect_transitive_dynamic_deps and pass that aggregate to the
shared library. Consumers of Ogre's legacy UTFString ABI use the SDK-provided
char_traits_patch.h where required. Do not introduce a different character-traits
implementation or duplicate SDK globals across plugin libraries.

Register the original XML with ament_index(plugin_categories = {xml:
"rviz_common"}), the original package.xml, runtime resources and shared library.
Compose @rviz_common//:ament_index, and @rviz_default_plugins//:ament_index when
its SDK is used: the latter brings the indexed image/point-cloud transport
runtime as well. These runtime indexes avoid the unrelated build-tool exports
of SDK ament_package targets. The generic //third_party/rviz2:runtime adapter
publishes the RViz executable; product plugin selection belongs to
//bazel/runtime:planning_simulator_rviz. Components use logical SDK labels.

Add a package/XML pair to bazel/tests/rviz_plugins/BUILD.bazel when registering a
plugin. Its <package>_factories_test parses that original XML and constructs,
destroys and unloads every exported Display/Panel/Tool class. Run it with
--config=runtime --config=no-agnocast --local_test_jobs=1 --test_output=errors.
DISPLAY and XAUTHORITY must identify an accessible X11 session: some original
constructors query X11 directly even with Qt's offscreen platform. Factory
success does not replace the product's real rendering and interaction check.

## Universe planning simulator

The original simulator product uses sample_vehicle/sample_sensor_kit,
CPU/Cyclone DDS and Agnocast off. Run these commands from the repository root;
no host ROS environment needs to be sourced. Build first:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=runtime --config=no-agnocast \
  //bazel/runtime:planning_simulator
```

Then launch the original RViz window, keeping waiting messages in a log:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  DISPLAY="$DISPLAY" XAUTHORITY="${XAUTHORITY:-}" \
  XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}" ROS_DOMAIN_ID=108 \
  bazel run --config=runtime --config=no-agnocast \
  //bazel/runtime:planning_simulator -- rviz:=true \
  > "${TMPDIR:-/tmp}/autoware-planning-simulator.log" 2>&1
```

Keep that terminal running. If no window appears, inspect the last 80 lines of
that log. The desktop must expose an authenticated X11/XWayland display and a
compatible OpenGL driver. The camera panel displays No Image in this simulated
rule-based profile. For manual lane driving:

1. Wait for the point-cloud/vector map and AutowareStatePanel to load.
2. Select **2D Pose Estimate**. Click a lane and drag along its direction;
   release and wait for Localization to become Initialized. For the first
   example, use the slanted straight road southwest of the central junction
   and drag northeast along it. The original
   adaptor fits the pose height to the indexed map.
3. Select **2D Goal Pose**. Click a reachable lane ahead, drag along the lane
   direction and release. Keep the first goal about 17 m ahead and before the
   junction. Wait for a route/trajectory and available **Auto**.
4. Click **Auto** in AutowareStatePanel. Observe the vehicle move and stop at
   its goal. Press **Ctrl+C in the launching terminal** to stop all nodes;
   the original launch can respawn RViz if only its window is closed.

The repeatable short scenario is recorded in
bazel/tests/planning_simulator_resources/scenario.yaml: start
(3717.2395, 73720.8438), goal (3732.0, 73728.6), heading about 28 degrees
counterclockwise from map +X, on lanelets 9102/9540. The typed equivalent is
//bazel/tests/universe_launch:lane_driving_test. It initializes through the
same /initialpose adaptor, sets a route through the public API, requests Auto,
and requires actual displacement, autonomous control, bounded arrival and stop.
The original GUI pose/goal/Auto workflow and this typed gate are validated.
Use **Clear Route** before selecting another route. The expected arrival state
is shown below; camera placement and mouse precision can differ.

![Original RViz after lane-driving arrival](images/planning-simulator-arrived.png)

For a headless session, use the same target with rviz:=false. The target supplies
original launch/config resources, real default rule-based planners/safety nodes,
native SDK runtime files and caller-selected sample model indexes. The map
resolves through autoware_map_asset; no map_path/data_path or absolute checkout
path is needed. Root planning_simulator_rviz composes the selected original
plugin indexes. The wrapper and YAML/XML frontends belong to the actual runnable
target's Python dependencies; test-only imports must not hide missing inputs.

The shared root configuration in bazel/runtime/cyclonedds.bzl sets
MaxAutoParticipantIndex=119. RMW's default of 32 is insufficient for this
single-host process graph. This controls discovery capacity, not kernel/network
provisioning or communication isolation. A different deployment topology must
select and validate its own domain/discovery configuration.

Run //bazel/tests/universe_launch:all with --config=expensive, the same build
configs and --local_test_jobs=1 --test_output=errors when changing this product
composition.
It exercises the original seven subsystem launches, full headless XML and the
closed-loop driving case. Component/standalone readiness, retained map/vehicle/
diagnostic resources, API version and clean shutdown remain part of the gate.
Routine component work keeps its own scoped suite. Existing core distribution
products retain their own scope; they do not package this simulator automatically.

## Native simulator distribution

Build //bazel/deployments:planning_simulator_bundle with --config=expensive
--config=runtime --config=no-agnocast. Extract its tar.gz at the desired installation prefix and
execute run_simulator from any working directory. It selects its own bundled
interpreter, libraries and indexed resources; no source checkout, Bazel command,
ROS setup script, Python installation or HOME model directory is a runtime input.
The supported native OS/ABI baseline still applies. The launcher defaults to the
headless original simulator; its subsequent arguments are original launch args.

The test-only //bazel/tests:planning_simulator_bundle_tests archive stays
separate. Use //bazel/tests:planning_simulator_bundle_structure_test and
//bazel/tests:planning_simulator_native_runtime_test for distribution changes.
They check closed archive links, extraction at another prefix, complete startup,
real lane driving and clean shutdown. The shared archive checker also retains
//bazel/tests:core_bundle_structure_test. These products package their declared
runfiles, not an inventory of files discovered on the builder's filesystem.

Independent-process test launchers must supply the product's explicit environment
at the exec boundary. In particular, do not forward a generated test PYTHONPATH
and rely on a child shell to unset it: a long relocated search path can prevent
that shell from starting. The product launcher then owns its own discovery paths.
The common Cyclone configuration is expanded into executable application/verifier
scripts from bazel/runtime/cyclonedds.bzl; update that source when the topology
requires another tested discovery profile.

## Python providers in runtime compositions

Use `runtime_python` from `//third_party/runtime:python.bzl` when a Python
consumer needs Python libraries reached through a non-Python product's runtime
graph. Set roots to the existing product target and add the resulting target
to the consumer's deps. The aspect collects PyInfo through deps, data, srcs,
libraries, executables, components and dynamic_deps; it merges the product's
runfiles and Python import/source providers. Ordinary data edges alone do not
propagate PyInfo to a py_binary. Keep runtime dependencies declared by their
owners rather than introducing a parallel package inventory.

Python plugin discovery also requires distribution metadata. The
`//third_party/ros2cli:entry_points` provider generates missing SDK CLI metadata
from the dependencies' own setup.py files using pinned setuptools. Add it to
the CLI's deps; keep entrypoint declarations and versions with those sources.
Its complete gate is `//third_party/ros2cli:metadata_test`. Generation runs in
an empty temporary directory with declared outputs, independently of checkout
package discovery or host Python installations.

The manifest output group records the selected roots and their declared runfile
symlink paths. It supports checking that a composed artifact preserves its
selected product's indexes/resources; it does not discover undeclared sources
or validate directory completeness. Run the generic helper's complete suite:

```sh
bazel test --config=runtime --config=no-agnocast \
  //third_party/runtime:runtime_python_test
```

## Sourceable native debugging environment

Build a debugging distribution from the planning_simulator entrypoint's
declared runtime closure, the existing all_interfaces selection and the selected
RCR ROS CLI extensions:

```sh
bazel build --config=expensive --config=runtime --config=no-agnocast \
  //bazel/deployments:planning_simulator_debug_bundle
mkdir -p /tmp/autoware-debug
tar -xzf bazel-bin/bazel/deployments/planning_simulator_debug_bundle.tar.gz \
  -C /tmp/autoware-debug
source /tmp/autoware-debug/install.bash
ros2 pkg prefix autoware_launch
ros2 pkg executables rclcpp_components
ros2 interface show autoware_planning_msgs/msg/Trajectory
python3 -c 'from autoware_planning_msgs.msg import Trajectory; print(Trajectory())'
```

setup.bash and its install.bash alias activate this distribution's bin directory,
ament prefix, native libraries, ROS Lyrical and CPU/Cyclone DDS profile with
Agnocast disabled. Activation replaces inherited ROS/Python discovery paths;
use a separate shell for another ROS installation. ROS_DOMAIN_ID, display
settings and an explicitly selected CYCLONEDDS_URI remain caller-owned. Match
ROS_DOMAIN_ID and discovery/network settings to the processes being inspected.
Use ros2 node/topic/service/param commands normally. The python3 wrapper selects
the same bundled interpreter and Python dependency environment as the CLI.
Runfiles variables remain private to each wrapper's subprocess so sourcing the
environment does not impose this tree on unrelated Bazel executables.

The product's indexes supply registered binaries, components, shared libraries,
interfaces, configuration, map/vehicle assets and RViz resources. Python
providers are collected from that product. The same top-level interface
selection used by compiled consumers supplies its Python/index projection;
compiled message libraries alone do not expose Python bindings or schema indexes.
Include the CLI group among the runtime_python roots too: its data edges can
carry Python providers that ordinary py_library deps do not forward. CLI
extensions add their own runtime dependencies. Undeclared
source packages and compiler/header/CMake SDK exports are outside this runtime
distribution. Add missing runtime exports to their owning package/index and
update the product's dependencies when its supported functionality changes.

The pinned RCR also provides ament_package and ament_underlay from
`@rosdistro//ament:defs.bzl`. They describe conventional installed package trees
and generate setup scripts/environment hooks. Use them when exporting that
package-install contract. The underlay aspect collects AmentPackageInfo through
deps; it does not consume an arbitrary entrypoint's data/runfile closure.
Our components currently export ament_index runtime providers, and some registry
ament_package declarations include build/lint/test dependencies. This debug
distribution therefore reuses the existing runtime indexes and RCR CLI Python
libraries, with a small shared activation template rather than substituting a
different package-install closure. The core run and run_simulator launchers source generated
instances of the same template before executing their fixed launch profiles.

Run the focused gates when changing this environment or its packaged closure:

```sh
bazel test --config=runtime --config=no-agnocast \
  //third_party/runtime:runtime_python_test \
  //bazel/tests/runtime_debug:launchers_test
bazel test --config=expensive --config=runtime --config=no-agnocast \
  --test_output=errors //bazel/tests/runtime_debug:bundle_test
```

The bundle gate extracts to a path containing spaces, checks preservation of
the entrypoint's declared index/resource paths, activates from a clean shell,
imports every interface in the declared all_interfaces selection and exercises
the CLI against a small live
peer. It does not start the full simulator or export an OCI image; select those
costly gates only when their behavior is affected.

## OCI simulator distribution

The simulator image puts the native application bundle at /opt/autoware over
the root-selected Ubuntu 24.04 base. Its headless entrypoint is run_simulator,
running as UID/GID 1000 with SIGINT for orderly shutdown. Build the OCI layout
or load the same manifest into the local Docker daemon:

```sh
bazel build --config=expensive --config=runtime --config=no-agnocast \
  //bazel/deployments:planning_simulator_oci //bazel/deployments:planning_simulator_docker_archive
bazel run --config=expensive --config=runtime --config=no-agnocast //bazel/deployments:planning_simulator_load
docker run --rm --name autoware-planning-simulator --pull=never \
  --network=none --read-only --cap-drop=ALL --security-opt=no-new-privileges \
  --tmpfs /tmp:rw,exec,nosuid,nodev \
  docker.io/library/autoware-planning-simulator:bazel
```

Stop it from another terminal with
docker stop --time 30 autoware-planning-simulator. The indexed map, model
descriptions, parameters, interpreter and libraries are application inputs;
no workspace/cache mounts, installed ROS/Python or dependency-install step is
needed. The local tag is a convenience, not an immutable release identifier.
Use the generated image manifest digest for release identity. The supported
runtime remains Linux x86_64/Ubuntu 24.04, CPU/Cyclone DDS with Agnocast off.
GUI display/device forwarding is not part of this image's acceptance gate;
use the native RViz instructions above for graphical lane driving.

For image changes, run the uncached local Docker gate:

```sh
bazel test --config=expensive --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors //bazel/tests:planning_simulator_oci_runtime_test
```

It uses the separate verification companion, checks all three original
startup/driving/shutdown cases in an isolated read-only image, and then checks
startup and SIGINT shutdown through the normal image entrypoint. Keep the
verifier outside the application image. Run the native archive/closure gates
above as well when changing the shared application payload.

## Original test source layout

Keep original tests in their source packages and expose their inputs through
package-local rules. When a suite assumes sibling source directories, a root
adapter may stage declared runfiles into a temporary directory with the original
package names. Do not resolve that assumption through undeclared checkout paths
or accept dependency-related skips. Supply real generator executables through
runtime data/arguments. The interface-lint suite uses
`//bazel/tests:interface_spec_lint_tests`; it includes all original cases and
its committed-spec/manifest checks. Its original config determines active gates.

When CMake supplies a test main, keep that driver and link gtest rather than
adding another gtest_main. Give the driver or fixture ownership of ROS context
initialization/shutdown; test bodies reuse an active context. If the original
suite itself initializes an active context twice, reproduce the failure and
correct the redundant call without dropping assertions or test files.

For a C++ fixture-path macro, derive its value from a declared $(rootpath file)
input and the expected parent directories. Keep quoted C++ strings intact
through copts tokenization; do not embed checkout paths or canonical repository
names. Declare runtime class-loader resource indexes as well as fixture bytes.

When an original C++ test textually includes a .cpp implementation, expose that
file through textual_hdrs on a test-only cc_library and add that library to the
test's deps. This supplies the sandbox input while preserving the original
relative include. Keep the production compilation in its owning library; adding
the included file to the test's srcs would compile another object unnecessarily.

The example node uses native RCLCPP component registration, with its original
ROS executable name registered under lib/autoware_test_node. Its index owns
that executable registration; avoid an executable-to-same-index data edge.
Use `//bazel/tests:autoware_test_node_launch_test` for standalone/component
checks and `//bazel/tests:autoware_testing_original_smoke_test` for the original
generic smoke helper, both with `--config=no-agnocast`.

## Tutorial vehicle and map resources

The tutorial's sample_vehicle_description and sample_sensor_kit_description are
separate local providers from the core autoware_sample_* descriptions. Declare
`@sample_vehicle_description//:ament_index` and
`@sample_sensor_kit_description//:ament_index` in the caller's data/deps and pass
vehicle_model=sample_vehicle and sensor_model=sample_sensor_kit. The sensor
index includes its velodyne_description dependency; Xacro, YAML, mesh and image
resources stay with their original source packages.

Select the git-tracked module at assets/map through the root local override.
Its MODULE.bazel owns the pinned archive and BUILD overlay; consume its public
`@autoware_map_asset//:ament_index` target. Resolve map resources from the
`autoware_map_asset` share directory
using ament_index/FindPackageShare or `$(find-pkg-share ...)`. Its original map
and projector files are installed together; do not inject workspace or HOME
paths. Fetching is a build operation, and runtime lookup uses indexed resources.
The map input and per-file provenance are maintained in
[the provider README](../../assets/map/README.md). Map/description resource tests
do not establish full planning-simulator or autonomous-driving support.

## Headless core composition

The core entrypoint resolves its declared `autoware_map_asset` resource package
through ament_index. Run it without a `map_path` argument; the root selects the
asset module/revision. Components that use an asset declare both module and
runtime package/index dependencies, so their exported runtime closure includes it.

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
bazel build --config=expensive --config=runtime --config=no-agnocast \
  //bazel/deployments:core_bundle //bazel/tests:core_bundle_tests
bazel test --config=expensive --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors //bazel/tests:core_system_headless_launch_test \
  //bazel/tests:core_bundle_structure_test
```

The application archive contains the headless original core launch, sample
vehicle/sensor providers and the selected autoware_map_asset. Its
executable-adjacent runfiles preserve the declared
ament indexes, libraries, Python and resource closure. Add component runtime
edges to the owning component's index/data; select concrete product resources
in bazel/runtime/BUILD.bazel. Do not add a second package-by-package file list.
Conflicting archive destinations fail. Build distributions with --config=runtime;
ordinary Python bootstraps otherwise require python3 before reaching their
declared interpreter.

Application archive rules under bazel/deployments/ consume product targets from
bazel/runtime/ with strip_prefix="/bazel/runtime". Verification archives under
bazel/tests/ use strip_prefix="/bazel/tests". Keep installed entrypoints and
adjacent runfiles independent of the archive rule's package location; moving a
BUILD target must not silently move files inside the deployment artifact.

The core run and simulator run_simulator launchers source generated instances
of bazel/runtime/setup.bash.in. Ship core_setup.bash or simulator_setup.bash
beside its launcher, respectively. The template activates that entrypoint's
ament/library prefix and clears inherited Python/runfiles discovery state;
the launcher sets its private RUNFILES_DIR and fixed product arguments before
exec. Keep these profiles separate from a developer's sourced shell. Check
both argument lists, profile settings and exit forwarding with
`//bazel/tests/runtime_debug:launchers_test`, and the affected native archives
with their bundle_structure_test targets. Reuse unchanged application behavior
evidence; this does not automatically require simulator driving or OCI export.

Register Python executable targets through register_ament_files, preserving the
launcher rather than flattening all DefaultInfo files to an executable name.
Registration must forward the executable's default runfiles as well as its
installed symlink. Validate the indexed entrypoint with a real subprocess;
build-tree execution can hide a broken launcher or missing imports/data.
For an original suffixless Python script, rules_python still requires a .py main.
Use a declared copy_file output alias (allow_symlink=True) as py_binary's source/
main, then register the executable under its original installed name. Keep the
original source filename and implementation unchanged.


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
directory without map arguments. The launch resolves the bundled map through
its ament index. The launcher establishes its own runtime paths
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

## OCI core distribution

The image reuses the native application archive verbatim beneath /opt/autoware.
Keep its prefix, OS selection, user and entrypoint in bazel/deployments/,
outside component BUILD files. The root selects rules_img and the pinned Ubuntu
base through bazel/deployments/deployments.MODULE.bazel. Assembly downloads
declared blobs with Bazel and requires no Docker daemon; image execution requires
a compatible Linux host kernel and container engine. The supported product remains Ubuntu 24.04 x86_64,
CPU/Cyclone DDS, with Agnocast disabled.

```sh
bazel build --config=expensive --config=runtime --config=no-agnocast \
  //bazel/deployments:core_oci //bazel/deployments:core_docker_archive
bazel run --config=expensive --config=runtime --config=no-agnocast //bazel/deployments:core_load
bazel test --config=expensive --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors //bazel/tests:core_image_structure_test \
  //bazel/tests:core_image_payload_test //bazel/tests:core_image_runtime_test
```

The OCI layout archive is bazel-bin/bazel/deployments/core_oci_layout.tar;
the Docker-load archive is bazel-bin/bazel/deployments/core_load_docker.tar.
The core_load target loads the same image as docker.io/library/autoware-core:bazel
into the selected local daemon. That tag is a local convenience; use the
generated bazel/deployments/core_digest manifest digest as the immutable release
identity, with a registry reference when publishing. No registry upload occurs
in these commands.

The structure and payload tests need no daemon. The runtime test loads its own
archive and is an uncached local test: Docker access is a prerequisite, not a
dependency skip. It runs both original headless cases using the external
verification companion, then checks the normal entrypoint and docker stop.
No application bind mount, source/cache mount, installed ROS or OS Python is
needed. Re-run all three tests when image packaging changes.

For manual execution, run the image directly; its declared map asset is bundled:

```sh
docker run --rm --name autoware-core --pull=never --network=none --read-only \
  --cap-drop=ALL --security-opt=no-new-privileges \
  --tmpfs /tmp:rw,exec,nosuid,nodev \
  docker.io/library/autoware-core:bazel
```

The image defaults to user 1000:1000, workdir / and HOME=/tmp. Its SIGINT stop
signal lets docker stop --time 30 autoware-core terminate ROS launch and its
children cleanly. Writable /tmp is ephemeral; persistent logs/state and
device-specific configuration need explicit deployment mounts. This image does
not establish GUI, GPU, enabled Agnocast, ARM, QEMU or OTA support.

### s6 core image

The core_s6 image adds a supervised demo prerequisite and core launch to the
same native payload. s6-rc starts the core only after the prerequisite writes
its readiness notification. Unexpected prerequisite failure stops the product
with nonzero status; recover by explicitly restarting the product. Ordinary
shutdown stops the core and its nodes before the prerequisite.

```sh
bazel run --config=expensive --config=runtime --config=no-agnocast //bazel/deployments:core_s6_load
bazel test --config=expensive --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors //bazel/tests:core_s6_structure_test \
  //bazel/tests:core_s6_runtime_test
docker run --rm --pull=never --network=none --read-only --cap-drop=ALL \
  --security-opt=no-new-privileges --tmpfs /tmp:rw,exec,nosuid,nodev \
  --tmpfs /run:rw,exec,uid=1000,gid=1000,mode=0755 \
  docker.io/library/autoware-core:s6
```

This variant runs its whole supervision tree as 1000:1000. The writable /run
mount belongs to that UID; S6_READ_ONLY_ROOT=1 keeps service definitions in the
image and mutable supervisor state in /run. Core output is recorded at
/run/autoware/core.log; lifecycle events are in /run/autoware/events. They are
ephemeral unless the operator explicitly supplies persistent storage. Map
selection is the root's declared asset module, shared with the native bundle.
AUTOWARE_READY_GATE is a demonstration/testing gate: when supplied,
the prerequisite waits for that file before announcing readiness.

Keep product service edges and failure policies under bazel/deployments/.
Preserve each added executable's runfiles, including toolchain-injected native
runtimes; copying only its ELF can omit required DSOs. The root verification adapter
checks the actual supervised core PID, reuses both original core cases and
asks the supervisor to stop the group. It does not start another core graph.
See [the s6 adoption gate](JOURNAL.md#2026-11-21-phase-722--s6-core-service-supervision)
for reproducible experiments and supported scope.

### systemd core image

core_systemd uses native systemd units for the same prerequisite/core product.
The prerequisite's Type=notify gates core startup; BindsTo/Requires stops core
on prerequisite loss. Unexpected service exit is fatal, including exit zero.
The units allow one start per manager instance: Restart=always plus an infinite
start-limit interval and burst one rejects the replacement start, then
OnFailure terminates the product. Recovery means restarting the container,
which supplies a fresh manager/start budget. Explicit group shutdown does not
request restart. The target uses Wants rather than Requires so target teardown
does not cancel the service's failure policy. PartOf propagates requested
product stops to both services.

```sh
bazel run --config=expensive --config=runtime --config=no-agnocast //bazel/deployments:core_systemd_load
bazel test --config=expensive --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors //bazel/tests:core_systemd_structure_test \
  //bazel/tests:core_systemd_runtime_test
docker run --rm --pull=never --network=none --read-only --cgroupns=private -t \
  --cap-add=SYS_ADMIN --security-opt=apparmor=unconfined \
  --security-opt=no-new-privileges --tmpfs /tmp:rw,exec,nosuid,nodev \
  --tmpfs /run:rw,exec \
  docker.io/library/autoware-core:systemd
```

The supported engine uses cgroup v2 with the systemd cgroup driver. PID 1 runs
as root; both application services use UID/GID 1000. Before starting systemd,
the entrypoint requires the private namespace root (0::/) and bind-remounts its
cgroup view writable. SYS_ADMIN alone is insufficient with docker-default
AppArmor; this container's profile is unconfined. These permissions are broader
than the s6 variant and must be accepted by the runtime operator. There is no
host-cgroup bind mount or use of the host's systemd package. The image is a
container supervision demonstration, not a bootable host OS.

Core output and readiness gating use the same paths/environment as the s6
variant; both resolve the bundled indexed map. Docker's SIGRTMIN+3 stop signal
requests orderly manager shutdown. When stopping services without exiting PID 1,
wait for all jobs:
`systemctl stop autoware.target autoware-core.service autoware-prerequisite.service`.
Stopping the target alone can return before its members finish. See the
[complete gate](JOURNAL.md#2026-11-21-phase-723--systemd-core-service-supervision).

### OSTree application distribution

//bazel/deployments/ostree:core_repository produces core_ostree.tar.gz (an archive-mode
OSTree repository plus releases.json) and core_ostree.json (the same manifest).
It reuses the complete native core bundle. The two demo releases have identical
application binaries and distinct synthetic release/domain metadata; they test
selection/update mechanics. The manifest records their immutable commit IDs,
the input bundle SHA-256 and SDK SHA-256. Commit metadata repeats the bundle
identity. The fixed timestamp and normalized archive headers avoid wall-clock
inputs. This is application delivery on the supported OS; systemd, its units
and the prerequisite stay in that OS/harness and are outside the update tree.

```sh
bazel build --config=expensive --config=runtime --config=no-agnocast \
  //bazel/deployments/ostree:core_repository
bazel test --config=expensive --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests:core_ostree_runtime_test
```

The gate uses the validated core_systemd image as a disposable fixed-OS harness,
mounting each independently selected checkout read-only over /opt/autoware. Both
original core cases and the complete managed failure/recovery/shutdown gate run
for A, B and rollback A. The SDK's own libc/ELF loader runs OSTree; a loader audit
rejects DSOs outside that SDK. No host OSTree installation is needed.

The local client also uses archive mode: it retains logical symlink metadata
without requiring filesystem xattrs. Force-copy checkout materializes a separate
application tree; regular-file permissions are canonicalized at commit time.
Native bundle bytes, link targets and executable status are checked directly.

For manual inspection, create a deployment directory, extract the distribution
there and use the declared CLI (absolute repository paths are needed because
Bazel runs tools from its execution environment):

```sh
deploy_root=$(mktemp -d)
tar -xf bazel-bin/bazel/deployments/ostree/core_ostree.tar.gz -C "$deploy_root"
ostree=(bazel run --config=runtime //third_party/ostree:ostree --)
"${ostree[@]}" --repo="$deploy_root/repo" fsck
"${ostree[@]}" --repo="$deploy_root/local" init --mode=archive
"${ostree[@]}" --repo="$deploy_root/local" pull-local --untrusted \
  "$deploy_root/repo" core/a core/b
a=$("${ostree[@]}" --repo="$deploy_root/local" rev-parse core/a)
mkdir "$deploy_root/deployments"
"${ostree[@]}" --repo="$deploy_root/local" checkout --user-mode --force-copy \
  "$a" "$deploy_root/deployments/$a"
ln -s "deployments/$a" "$deploy_root/active"
```

Stage a new checkout completely and verify its identity/integrity before
activation. Stop the whole product, atomically replace active with a symlink to
the prepared checkout, then start a fresh product instance. Resolve the selected
checkout for the image's /opt/autoware mount before startup. Do not change active
under a running product that might load further resources through that path.
Rollback selects a retained compatible checkout and follows the same stop/start
sequence. The gate kills an actual force-copy preparation after observing files;
the previous selection remains unchanged and passes its complete gate afterward.
This does not establish crash-safe whole-OS deployment or bootloader rollback.

Keep writable vehicle identity/calibration outside deployments and never overlay
it with revision content. The fixture preserves synthetic records across A→B→A;
release-owned domain metadata changes with the selected tree. Compatibility here
means unchanged binary/state schemas and unchanged service definitions. Real
schema migration, credentials, remote authentication/signing, fleet rollout and
full domain-resource switching are deferred. No automatic OS or host service
installation is performed. See the
[adoption gate](JOURNAL.md#2026-11-21-phase-724--ostree-application-tree-distribution).

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
dependency/dynamic-dependency, data, local-definition and extra-tag inputs; its native
binary uses the SDK-owned runtime launcher. Declare model fixtures with data and
rootpath definitions. The launcher resolves runtime anchors through runfiles,
adds the SDK/GNU directories for bare-name builder dlopen, preloads the selected
GNU libstdc++ so vendor RTTI and internal exceptions bind the GNU C++ ABI, and
preserves native GTest XML/exit status. LD_LIBRARY_PATH alone does not establish
that symbol-binding order when the application also uses libc++abi. Never link the builder-resource libraries directly; their
SONAMEs intentionally prohibit that use. The helper's runtime data includes
pinned GNU runtimes and the builder resources, not incidental host SDK installs. A
caller-owned fixture executable can be supplied through run_under; it receives
the native command after SDK runtime setup. Keep application fixture preparation
under bazel/tests/, and set timeout="long" for genuine multi-minute model builds.
The wrapper must preserve native exit/XML results and keep engine-cache outputs
in writable temporary storage.

Use tensorrt_cc_binary from the same defs.bzl for a standalone native preparation
tool needing that SDK/ABI runtime environment. It forwards command-line arguments
and exit status without assuming GTest XML is present. Declare model/provider
resources in the product's data; dependency helpers must not select Autoware models.

Run `bazel test --config=no-agnocast --nocache_test_results --test_output=errors @autoware_tensorrt_common//...` on a provisioned GPU host.
Check XML for all 13 cases with no skips. The separate CUDA-utils regression
remains all 17 cases. Root SDK selection, GPU code generation and driver
prerequisites follow the preceding CUDA convention. Ordinary applications need
the equivalent declared runtime/resource loading contract; a successful link
alone does not prove engine-building runtime closure.

### Common-library engine cache

Pass an explicit engine_path to TrtCommonConfig when ONNX resources are indexed
or read-only. Provision a separate writable parent directory first. The common
library writes the engine and layer JSON there; it does not create that parent.
Keep model inputs and derived cache files in separate locations.

Select different cache paths for different ONNX/weights revisions, TensorRT/GPU
profiles, precision, optimization shapes, plugins and calibration settings/inputs.
The current loader checks its SDK header and declared IO/shape profiles; it does
not compare model contents or every builder option. Reusing a path for a changed,
same-shaped model can silently reuse the old engine. Product configuration owns
that identity; automatic content-addressed cache selection is not implemented.

By default TrtCommon retains build-on-miss behavior: a short version header
triggers rebuilding, while a matching header with an invalid serialized body is
rejected. To prohibit generation, set the final TrtCommonConfig argument
allow_engine_build=false and supply an engine_path. This mode restores the plan
without initializing builder/network/parser or reading ONNX; getBuilderConfig()
and getNetwork() return nullptr. setup() checks declared IO/profiles against the
restored engine and never rebuilds on mismatch. Missing, truncated or incompatible
engines fail explicitly. Model/GPU/SDK/profile identity is still caller-owned.
Use one writer per cache key: these checks do not make publication atomic or
coordinate parallel builders. Validate changes with the full original common suite plus the added
runtime-cache gate on a real GPU:

```sh
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_tensorrt_common//... //bazel/tests:tensorrt_runtime_cache_test
```

### Engine compilation policy

Keep Universe components parameterized: they own preparation/inference APIs,
model-specific preprocessing/calibration mechanics and supported-option checks.
Product composition under bazel/ or autoware_launch and above owns concrete
model/precision/calibration/optimization choices, compatibility requirements and
engine/cache identity. Component CLI/YAML defaults are development conveniences,
not a qualified product profile. Pass the selected compilation settings explicitly
in product-owned preparation invocations; local device index and writable cache
location remain invocation/runtime inputs. SDK/toolchain mechanics stay in
third_party/, with version selection at the root.

The existing yolox_prepare target is a model-indexed runnable tool, not a pinned
product profile or an engine-producing build action. Do not infer product
qualification from its component defaults. Engine preparation may run on the
target device before activation; it need not be an action in the Bazel build.
Current generation/prepared-only APIs retain their documented behavior below.

### YOLOX model inputs and engine caches

Set the YOLOX engine_path node parameter (or the same argument in its three
single-node launch files) to a provisioned, profile-qualified cache filename.
An empty value preserves legacy model-adjacent output and is unsuitable for
immutable model resources. Resolve model/label/color-map inputs through their
own model package's ament index. For example, a composed node can use:

```xml
<param name="model_path" value="$(find-pkg-share autoware_tensorrt_yolox_models)/yolox_s_car_ped_tl_detector_960_960_batch_1.onnx"/>
<param name="engine_path" value="$(var yolox_engine_path)" type="str"/>
```

Apply the common-library cache identity rules above. Engine and layer JSON use
that cache stem. Generated calibration tables use its algorithm-specific
EntropyV2-/MinMax-/Legacy-calibration.table suffix; generated Legacy histograms
use histogram.table. Readers prefer the generated cache, then the supplied table
beside the ONNX. Writers only use the output path. Provided tables stay immutable
in their model repository; a matching filename does not guarantee SDK compatibility.

Prepare the engine on the target GPU before starting inference. The source-owned
prepare_engine CLI shares the detector's model/profile/calibration setup without
initializing a ROS node or executor. The root yolox_prepare product adds the indexed
model provider. Create a writable output directory and use a qualified cache key:

```sh
mkdir -p /tmp/yolox-engine-cache
bazel run --config=runtime --config=no-agnocast //bazel/runtime:yolox_prepare -- \
  --model-package autoware_tensorrt_yolox_models \
  --model yolox_s_car_ped_tl_detector_960_960_batch_1.onnx \
  --engine /tmp/yolox-engine-cache/traffic-light-trt10.16-sm120-fp16.engine \
  --precision fp16 --builder-optimization-level 3
```

Use --help for precision, calibration and builder-level flags. --model accepts
an explicit path without --model-package; with a package it is relative to that
package's share directory. The CLI reuses an existing compatible engine, so select
a new output key when inputs/settings change. It requires a supported GPU/driver,
not merely a CUDA compiler; it is a bazel run tool, not an engine-producing action.

TrtYoloXNode and its three single-node launchers default to
allow_engine_build=false. Supply the same engine_path, matching labels/remaps and
normal inference parameters. ONNX and calibration inputs are not read in this
mode; model_path can be empty in a composed node with an explicit engine_path.
A missing/incompatible plan fails startup without attempting compilation. Prepared
engines can be read-only; raw output class count must match the configured labels.
For development, explicitly set allow_engine_build=true to retain build-on-miss.
Legacy build_only=true still permits generation and shuts down the ROS context;
it remains compatible but the standalone CLI is the normal preparation interface.
Direct TrtYoloXDetector C++ callers retain allow_engine_build=true by default for
compatibility and must set false for prepared-only use.

The shared libraries still link the parser/build code and the declared SDK data
still includes builder resources. Functional ONNX-free loading does not establish
an inference-only binary/package closure; retain these SDK resources until a
separate packaging task demonstrates that they can be removed.

The validated profiles are traffic-light FP16, published segmentation INT8 with
clip_value=6, and fresh segmentation MinMax calibration as a functional smoke
check. Single-image calibration is not production accuracy qualification. Fresh
traffic-light INT8 calibration fails at the default builder optimization level 3
on the pinned 10.16/Blackwell profile. Its explicit error regression must be
reviewed when SDK/model/GPU inputs change. An opt-in workaround is validated:
set builder_optimization_level=0 with precision=int8, calibration_algorithm=MinMax
and clip_value=0. Supply a calibration image list for initial generation, or a
compatible supplied/generated table. The detector and TrtYoloXNode accept levels
0..5, default 3. With the three provided single-node launchers, pass the
builder_optimization_level launch argument explicitly; it overrides the YAML value.
The optional setting is also declared in the package's parameter schemas.

Choose a different engine cache key for each optimization level. Level 0 disables
dynamic kernel generation and may worsen inference performance; it is a workaround
for the qualified profile, not a new default or production performance claim.
To revisit the failure/setting independently of Autoware implementation code,
explicitly run //bazel/tests:yolox_sdk_compiler_test. This manual diagnostic saves
engines, calibration tables and detailed level-0 tensor formats under its test
outputs. It is not a routine wildcard test or an inference-performance benchmark.
Run the full original module/consumer suites and added cache gate from the
[YOLOX journal](JOURNAL.md#2026-11-23-phase-652--yolox-engine-paths-and-calibration-inputoutput-separation).

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

## MPC solver generation

Depend on `@acados` for native solver CcInfo. Keep the original model generator
inside the consuming source repository and declare it as a py_binary depending
on `@acados//:template`. Use `acados_codegen` from `@acados//:codegen.bzl` with
that executable and its explicit relative C/header outputs, then compile those
outputs into a package-owned library. Put the generated include root before any
source-tree IDE stubs. Code generation belongs to the build graph; application
startup must not invoke Python compilation or fetch solver dependencies.

The [dependency provider](../../third_party/acados/README.md) documents native
configuration, builder constraints, controlled GNU runtime and its full gate.
Keep code-generation wheels/tools out of the native consumer's runtime closure.

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
