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

Maintain the top-level target selection directly in
`bazel/interfaces/BUILD.bazel`, using `ros_interface_set(roots = [...])`.
Roots may be individual interface targets, package `:cc` targets, or application
targets; collection follows their declared `deps`, not actual C++ type usage.
One `//bazel/interfaces:all_interfaces` target exposes this closure.
`//bazel/agnocast:plugins` consumes that set and declares the matching `:cc`
compile and `:cc_shared` dynamic dependencies explicitly in its BUILD file.
Other consumers can read the same interface provider. The plugin rule iterates the interface
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

Use ROS packages from the ROS Central Registry until a package must build from
the repository source. A local Autoware package owns a package-local Bazel
module. The root module selects it with a concrete published version and a
`local_path_override` when an RCR counterpart exists. Package-local dependency
versions remain `0.0.0` so the root release selection supplies concrete ROS
versions.

Removing a local override is a deliberate source-selection check, not part of
the normal developer loop. It is unsupported for a package that has no
published RCR counterpart.

For a C++ consumer using generated ROS libraries, pair compile and runtime
dependencies:

- depend on the generated package's `:cc` target;
- add its `:cc_shared` target to `collect_transitive_dynamic_deps`.

This preserves the RCR shared-library layout required at runtime.

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
add an appropriate target to `roots` in `bazel/interfaces/BUILD.bazel` and the
corresponding compile/shared dependencies in `bazel/agnocast/BUILD.bazel`.
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

Keep dependency patches under `bazel/patches/` and apply them through a pinned
module override. Before the diff in each patch file, document the affected
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
