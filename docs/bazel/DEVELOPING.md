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

Fast DDS is the Tier 2 compatibility configuration. Add `--config=fastdds` to
the same build, test, or run command at the compatibility gate:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds //path/to/package:target_test
```

Do not let a Fast DDS-only workaround determine the primary Cyclone DDS path.

## Source and package layout

Source snapshots under `src/` are ordinary tracked files imported as squashed
git subtrees. Edit them in place. Do not run `vcs import` or `vcs pull` into
`src/`.

Keep:

- workspace-wide Bazel support under `bazel/`;
- package-owned `MODULE.bazel` and BUILD files beside package source;
- development and runtime container definitions under `images/`;
- integration strategy and evidence under `docs/bazel/`.

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

Then run a consumer that constructs or transports the changed type. Repeat the
bounded build and consumer test with `--config=fastdds`.

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

## Validation and commits

Validate in proportion to the change:

- BUILD or module change: build every affected target surface;
- library change: build and run focused tests;
- executable or communication change: run a behavior check;
- container change: build the image and run a representative target with the
  source mounted read-only;
- documentation-only change: run targeted documentation checks and state that
  no runtime validation was performed.

Run only targeted format and static checks for files changed by the task. Do
not run pre-commit for this experiment.

Use signed-off Conventional Commits. Keep source compatibility changes in a
separate `fix(src/<repository>): ...` commit from Bazel build definitions. Do
not commit `user.bazelrc`, Bazel output trees, or convenience symlinks.
