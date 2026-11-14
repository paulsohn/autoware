# Developing the Bazel integration

## Read first

Read `AGENTS.md`, `PLAN.md`, and the latest `JOURNAL.md` entry before changing
the integration. The plan defines the current experiment; the journal records
what the repository has actually demonstrated.

## Local configuration

Use Ubuntu 24.04 x86_64 for the first phase. Install Bazelisk as `bazel`; the
repository's `.bazelversion` selects the tested Bazel release.

Put machine-specific limits in `user.bazelrc`, which is ignored. For example:

```text
build --jobs=6
build --local_resources=cpu=6
```

Choose limits appropriate to the machine. Run only one heavy Bazel build at a
time.

## Clean environment

Run Bazel without a sourced ROS environment when validating integration work:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" bazel <command>
```

Do not run `vcs import` or `vcs pull` into `src/`.

## Bootstrap smoke test

Cyclone DDS is the default. Build the publisher and subscriber:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build //bazel/smoke:talker //bazel/smoke:listener
```

Run the listener in one terminal and the talker in another:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel run //bazel/smoke:listener
```

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel run //bazel/smoke:talker
```

The listener exits successfully after receiving five messages.

No Cyclone DDS XML or host network tuning is required for this loopback smoke
test. Add a network configuration only when the topology under test requires
one.

Run the Tier 2 Fast DDS compatibility gate by adding `--config=fastdds` to
every command:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=fastdds \
  //bazel/smoke:talker //bazel/smoke:listener
```

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel run --config=fastdds //bazel/smoke:listener
```

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel run --config=fastdds //bazel/smoke:talker
```

## Development image

Build the pinned Ubuntu 24.04 development image:

```bash
docker build -f images/dev.Dockerfile -t autoware-bazel-dev:bootstrap .
```

Use a named volume for Bazel's cache and mount the source read-only:

```bash
docker volume create autoware-bazel-bootstrap-cache
docker run --rm \
  -v autoware-bazel-bootstrap-cache:/home/ubuntu/.cache \
  -v "$PWD:/workspace:ro" \
  autoware-bazel-dev:bootstrap \
  bazel build --experimental_convenience_symlinks=ignore \
  //bazel/smoke:talker //bazel/smoke:listener
```

Add `--config=fastdds` to the container's Bazel command for the compatibility
build.

## Local Autoware interface modules

The first package-local module is `autoware_common_msgs`. Build every generated
target exposed by it with:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_common_msgs//...
```

Run its C++ consumer through the primary and compatibility configurations:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test //bazel/smoke:autoware_common_msgs_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds //bazel/smoke:autoware_common_msgs_test
```

The package owns its `MODULE.bazel` and BUILD files. The root module makes it
visible with a direct `bazel_dep` at the published RCR version and selects the
source tree with `local_path_override`. Removing that override selects the RCR
source without changing consumer BUILD labels; use `--lockfile_mode=off` for a
temporary source-selection check. Package-local dependencies use version
`0.0.0` so the root release selection supplies their concrete versions.

An interface package with dependencies has three matching declarations:

1. direct package dependencies in its package-local `MODULE.bazel`;
2. referenced interface labels in each `ros_message` or `ros_service` rule;
3. package repositories in `default_generators.dependencies`.

Build all generated localization targets and run both C++ consumers:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_localization_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test \
  //bazel/smoke:autoware_common_msgs_test \
  //bazel/smoke:autoware_localization_msgs_test
```

The generated-message behavior pair is
`//bazel/smoke:localization_talker` and
`//bazel/smoke:localization_listener`. Run the listener first, then the talker;
the listener exits after validating five `KinematicState` payloads. Add
`--config=fastdds` to both commands for the compatibility gate.

Build the first Phase 2 interface layer and run its consumer:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_control_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_v2x_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test //bazel/smoke:autoware_interface_layer_test
```

Add `--config=fastdds` to the build and test commands at the bounded-group
compatibility gate. The consumer verifies same-package nested message targets
and a bounded sequence generated from `builtin_interfaces/Time`.

Build the expanded base layer sequentially:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_system_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_sensing_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_map_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_planning_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_perception_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test //bazel/smoke:autoware_interface_base_test
```

`geographic_msgs` is outside the `ros_core` release selection, so the root
module pins its concrete published version for `autoware_map_msgs`.

Build the dependent interface layer and run its consumer:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_vehicle_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_simulation_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results \
  //bazel/smoke:autoware_interface_dependent_test
```

`autoware_simulation_msgs` has no published Lyrical RCR module. The root uses
version `0.0.0` with a local override, so removing that override is not a
supported source-selection check. The `autoware_msgs` ament metapackage has no
generated interface surface and is not a Bazel module.

For the complete interface gate, build all 11 module repositories together,
then force execution of the five interface consumers. Repeat both commands
with `--config=fastdds` for the compatibility gate.

## Task workflow

1. State the target and its observable success condition in JOURNAL.md.
2. Make the smallest change that tests the current question.
3. Run the relevant build and test commands.
4. For executable targets, verify behavior rather than relying only on a build.
5. Record exact commands and results. For a failure, separate symptom, confirmed
   cause, and fix.
6. Update PLAN.md if the result establishes or reverses a durable decision.
7. Add upstream notes only when the evidence is reproducible and useful beyond
   this branch.
8. Run `git diff --check`, review the staged diff, and commit with sign-off.

## Commit boundaries

- Bazel rules, BUILD files, workspace configuration, and integration docs use
  scopes such as `feat(bazel)`, `fix(bazel)`, or `docs(bazel)`.
- Compatibility changes under an imported repository use
  `fix(src/<repository>)` and remain separate from BUILD-file commits.
- Do not combine unrelated package ports to reduce commit count.

## Validation language

Report only commands run against the current commit. Distinguish build success,
test success, runtime behavior, and artifact comparison; one does not imply the
others. Documentation-only changes should say that no build was run.
