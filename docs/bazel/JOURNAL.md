# Autoware + Bazel integration journal

## How to reproduce from this journal

This is the adoption guide for bringing Bazel to a non-Bazel Autoware tree, not
a day-to-day developer manual or only a change log. Dated entries are the
chronological to-dos and milestones, and assume the successful state of
preceding entries unless they name a different starting point. A reader
applying the same method to another Autoware snapshot should preserve the
dependency and validation patterns, while re-deriving message inventories and
compatibility fixes from that snapshot rather than assuming identical source
contents.

Every entry records or links its purpose, prerequisites and inspected inputs,
minimum integration pattern, decision or transferable lesson, exact
verification method and observed result, and remaining limitation or next
boundary. Repository paths and Bazel labels are part of the method; complete
source listings are intentionally omitted when the required rule shape is
sufficient to recreate them.

## 2026-11-13: Define scope and validation gates

Goal: define the experiment's initial scope on Ubuntu 24.04 x86_64, with a
headless planning simulator in an OCI image as the first end-to-end milestone.

Changes: added PLAN.md with goals and validation gates. Development uses local
branch `bazel`, with direct pushes to `paulsohn/bazel` and signed-off
Conventional Commits.

Validation: `git diff --check` passed. This change contains documentation only;
no build or runtime experiments have been performed.

Result: the initial scope and evidence gates were recorded. No technical build
hypothesis was validated by this entry.

Reproduction notes: this entry establishes the experiment contract rather than
a build state. Start from an Autoware source snapshot on a dedicated branch,
keep the existing build system untouched, define an x86 Ubuntu 24.04 milestone,
and require host, minimal-container, build, and runtime behavior evidence before
calling a phase complete.

Next: scope cleanup, pinned Bazel workspace and talker/listener smoke checks.

## 2026-11-13: Plan the bootstrap experiment

Goal: define repository working rules and a Phase 0 experiment that discovers
the required Bazel and RCR configuration through a minimal ROS publisher and
subscriber.

Changes:

- added root `AGENTS.md` with source-layout, commit, validation, and journaling
  rules;
- narrowed PLAN.md to decisions supported before bootstrap and moved later work
  into phase boundaries and open questions;
- added DEVELOPING.md for local configuration, clean-environment commands,
  commit boundaries, and validation language;
- added empty, evidence-gated UPSTREAM_RCR.md and UPSTREAM_AWF.md notebooks;
- ignored Bazel convenience symlinks and developer-specific `user.bazelrc`.

Validation: documentation and ignore-rule change only. `git diff --check`, the
repository's `markdownlint` and `prettier` hooks, and ignore checks for
`bazel-bin` and `user.bazelrc` passed. No build or runtime experiment was
performed.

Result: repository boundaries, experiment records, and validation language
were established before implementation. This entry validates the workflow, not
the Bazel or ROS configuration.

Reproduction notes: create the operating documents before BUILD files so later
experiments have stable boundaries. Keep local resource limits in an ignored
`user.bazelrc`; ignore Bazel convenience symlinks; reserve `bazel/`, `images/`,
and `docs/bazel/` for integration artifacts; and separate source compatibility
commits from Bazel definitions. The upstream notebooks remain empty until a
finding is reproducible and useful outside the experiment.

Next: inspect current RCR bootstrap guidance and release metadata, then add only
the workspace configuration needed by the talker/listener experiment.

## 2026-11-13: Bootstrap Lyrical publisher and subscriber

Goal: build and run a minimal Lyrical `rclcpp` publisher/subscriber pair without
using host ROS packages, then repeat the build in a minimal development image.

Inputs inspected:

- ROS Central Registry commit
  `81a1ca6b4ee9e76d1e271bc04f7f04744f3e7632` (2026-11-09), including its
  README, examples, and module metadata;
- Bazel 9.3.2 and Bazelisk v1.29.0 release metadata;
- the Ubuntu 24.04 multi-platform image index at
  `sha256:2198e4c65076e9cb0b3d695219df667c0e3b0072da64ca3c2648aca17dcecaf5`.

Changes:

- pinned Bazel 9.3.2, the RCR and BCR registries, the generated module lockfile,
  LLVM 21.1.8, the Lyrical ROS release modules, and a glibc 2.34 platform;
- isolated action and runtime environments and selected `rmw_fastrtps_cpp`;
- added `rclcpp`/`std_msgs` talker and listener targets using the RCR shared
  library collection;
- added a pinned Ubuntu 24.04 development image with checksum-verified Bazelisk
  v1.29.0 and no ROS installation.

Experiments:

1. A root configuration flag addressed to `@protobuf` failed because that
   transitive repository was not visible to the root module. The flag was not
   required by the smoke target, so it was removed instead of adding an unused
   direct dependency.
2. The root platform label could not resolve `@platforms` until `platforms`
   became an explicit root dependency. Adding that dependency is required
   because the root BUILD file references it directly.
3. With the RMW selection left dynamic, analysis reached 1,188 loaded packages
   and 76,363 configured targets without completing in the observation window.
   Selecting `rmw_fastrtps_cpp` reduced loaded packages to 371 and allowed
   analysis and execution to complete. The completed graph still configured
   76,915 targets, reflecting the shared-library dependency closure.

Host validation:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build //bazel/tests:talker //bazel/tests:listener
```

The build passed with 7,352 actions in the graph. The completed invocation used
6,106 action-cache hits, 76 remote-cache hits, 155 internal actions, and 1,015
Linux-sandbox actions. C++23 builds emitted upstream deprecation warnings from
`rosidl_runtime_cpp`; no bootstrap source change was required.

Behavior validation ran the generated listener script before the talker script
from the same clean environment. The listener received messages 0 through 4,
printed `Received 5 messages; smoke test passed`, and exited 0. The continuously
running talker was then stopped by its 10-second timeout, which exited 124 as
expected.

After adding Apache 2.0 source headers attributed to Taeseung Sohn and removing
a namespace-wide chrono import found by `cpplint`, the host and container
builds were repeated. Each rebuilt four local actions, with all other actions
cached. The behavior check was also repeated with the same result.

Development-image validation:

```bash
docker build -f images/dev.Dockerfile -t autoware-bazel-dev:bootstrap .
docker volume create autoware-bazel-bootstrap-cache
docker run --rm \
  -v autoware-bazel-bootstrap-cache:/home/ubuntu/.cache \
  -v "$PWD:/workspace:ro" \
  autoware-bazel-dev:bootstrap \
  bazel build --experimental_convenience_symlinks=ignore \
  //bazel/tests:talker //bazel/tests:listener
```

The image build passed. The source was read-only and the cache volume began
empty. The Bazel build passed in 923.097 seconds with 3,859 remote-cache hits,
2,138 internal actions, and 1,015 process-wrapper sandbox actions. Host and
container executable SHA-256 digests matched:

- talker: `49c7036f77c174b7fb3d0cbc23e5078d401931e5c0cc8bc980be45c3c602ce7e`;
- listener: `1200b55d1786597d3c6b402e0453ce54d0ba9535621ce08f642834e066869b5e`.

Result: Phase 0 exit criteria passed. The comparison covers the two executable
files, not their separately loaded shared libraries. No generally useful
upstream defect was isolated, so the upstream notebooks remain unchanged.

Static validation: `git diff --check`, JSON parsing of `MODULE.bazel.lock`,
`markdownlint`, Prettier, `clang-format`, `cpplint`, and `hadolint` passed. Bazel
parsed the root module and BUILD files during both host and container builds.

Reproduction notes: the minimum bootstrap consists of `.bazelversion`,
`.bazelrc`, root `MODULE.bazel`, a Linux platform target, one BUILD package for
the two nodes, their C++ sources, and the development Dockerfile. Select
`ros_core@lyrical.2026-10-05.rcr.0`,
`rosdistro@lyrical.2026-10-05.rcr.3`, direct `rclcpp@0.0.0` and
`std_msgs@0.0.0`, `llvm@0.8.25.rcr.0`, `rules_cc@0.2.26.rcr.0`, and
`platforms@1.1.0`; register LLVM 21.1.8 and pin the glibc 2.34 platform. Put the
RCR registry before BCR, disable direct-dependency checking for release-selected
`0.0.0` packages, use strict action environments and the public read-only RCR
cache, download shared outputs, and set `LD_LIBRARY_PATH=lib` for runs. Pair
`@rclcpp//:rclcpp` with `@rclcpp//:rclcpp_library` and message `:cc_shared`
dynamic dependencies with `:cc` compile dependencies. Those details are what
turn the version pins and validation commands above into a reproducible graph.

Next: define the first Autoware interface-package slice and its local-package
layout before adding package BUILD files.

## 2026-11-13: Set middleware support tiers

Goal: align middleware priorities with Autoware before selecting the first
interface-package slice.

Decision: Cyclone DDS is the primary middleware target for Autoware integration.
Fast DDS remains a Tier 2 compatibility target and will be checked after the
primary Cyclone DDS path meets each phase's exit criteria. Phase 0 remains a
valid Fast DDS bootstrap result; it does not establish the default for later
phases.

Plan: use the existing talker/listener as a middleware gate before Phase 1.
First select Cyclone DDS without adding a custom XML file or host tuning, then
run the identical build and behavior checks through an explicit Fast DDS
compatibility configuration. Repeat both builds in the development image and
compare artifacts before starting an Autoware interface package.

Validation: documentation-only change. `git diff --check`, `markdownlint`, and
Prettier passed. No build or runtime behavior changed.

Result: the middleware support policy and its required follow-up gate were
defined. Middleware selection remained unvalidated until the next entry.

Reproduction notes: this is a policy checkpoint, not a configuration result.
Record middleware support tiers before package integration so later experiments
have one primary path and a bounded compatibility gate. Do not add DDS tuning
until the same loopback behavior test demonstrates a need.

Next: execute the Cyclone DDS primary and Fast DDS compatibility smoke gates.
Do not add middleware configuration until a current behavior check demonstrates
that it is required.

## 2026-11-13: Validate middleware support tiers

Goal: make Cyclone DDS the default for the existing smoke pair and retain Fast
DDS as an explicit compatibility path before adding Autoware packages.

Changes:

- selected `rmw_cyclonedds_cpp` for normal builds and run commands;
- added `--config=fastdds` to select `rmw_fastrtps_cpp` at build and run time;
- added `@std_msgs//:cc_shared` to the smoke target's dynamic dependency map,
  matching the current RCR convention for message libraries.

The `std_msgs` shared-library trial did not reduce the configured-target count,
but it changed the link structure from a statically linked message library to
the RCR's paired `:cc`/`:cc_shared` layout. It was retained for consistency with
future Autoware message packages.

Host builds used a caller environment without ROS variables:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build //bazel/tests:talker //bazel/tests:listener
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=fastdds \
  //bazel/tests:talker //bazel/tests:listener
```

For each configuration, `bazel run --script_path` generated listener and talker
scripts. The listener started first under a 20-second timeout and the talker ran
under a 10-second timeout. Both listeners received messages 0 through 4,
printed `Received 5 messages; smoke test passed`, and exited 0. Each talker
continued publishing until its expected timeout exit 124.

The development-image builds mounted the source read-only and reused the named
Bazel cache volume:

```bash
docker run --rm \
  -v autoware-bazel-bootstrap-cache:/home/ubuntu/.cache \
  -v "$PWD:/workspace:ro" \
  autoware-bazel-dev:bootstrap \
  bazel build --experimental_convenience_symlinks=ignore \
  //bazel/tests:talker //bazel/tests:listener
docker run --rm \
  -v autoware-bazel-bootstrap-cache:/home/ubuntu/.cache \
  -v "$PWD:/workspace:ro" \
  autoware-bazel-dev:bootstrap \
  bazel build --config=fastdds --experimental_convenience_symlinks=ignore \
  //bazel/tests:talker //bazel/tests:listener
```

Measured graphs:

- Cyclone DDS: 352 packages, 70,383 configured targets, 1,197 aspect
  applications, and 5,785 actions in the graph;
- Fast DDS: 371 packages, 76,915 configured targets, 1,224 aspect applications,
  and 7,352 actions in the graph.

Host and container executable SHA-256 digests matched within each
configuration:

- Cyclone DDS talker:
  `853ef5a9e7c5731d2557a790b74cd01543ed583b97a84d777eb114640f0dcafd`;
- Cyclone DDS listener:
  `f5c02dc5cde9a36007278fdf9051d43b8fe6c40672bccbe585981699ad7ac4b4`;
- Fast DDS talker:
  `3e84cd9c709377213efae6cbf23c55776e0fcdb6dfaae69c35171c62395b9cda`;
- Fast DDS listener:
  `921ec9f55a1c6ec2bfbdeb2ed24dd6ce64168a89aec86a5f4e10ccb42d917d23`.

Result: the middleware gate passed. Cyclone DDS needs no custom XML or host
network tuning for the loopback smoke behavior. Fast DDS remains independently
selectable and passes the same behavior check. No upstream defect was isolated.

Static validation: `git diff --check`, `markdownlint`, Prettier, end-of-file,
and trailing-whitespace hooks passed. Both Bazel configurations parsed the
updated BUILD and rc files during host and container builds.

Reproduction notes: select middleware at both build and run time. Use
`--@rosdistro//:rmw=rmw_cyclonedds_cpp` plus
`RMW_IMPLEMENTATION=rmw_cyclonedds_cpp` by default, and put the equivalent
`rmw_fastrtps_cpp` settings behind `--config=fastdds`. Consumers of generated
messages need the paired `:cc` compile target and `:cc_shared` entry passed to
`collect_transitive_dynamic_deps`; a successful link alone does not prove the
runtime shared-library closure. Validate each configuration by receiving five
messages, not merely by building it.

Next: prototype `autoware_common_msgs` manually as the first local Autoware
package module.

## 2026-11-14: Build the first local Autoware package module

Goal: determine the minimum package-local Bazel layout for an Autoware
interface package by generating and consuming `autoware_common_msgs/msg/ResponseStatus`.

Inputs inspected:

- the package's `package.xml`, `CMakeLists.txt`, and `ResponseStatus.msg`;
- the current RCR `default_generators` and `ros_message` definitions;
- the RCR's published `autoware_common_msgs` metadata and generated target
  convention.

Changes:

- added a package-local `MODULE.bazel` with a direct
  `rosidl_default_generators@0.0.0` dependency;
- added root and `msg/` BUILD files using `default_generators` and
  `ros_message`;
- registered the package in the root module with a direct `bazel_dep` and
  `local_path_override`;
- added a C++ test that constructs `ResponseStatus` and verifies its generated
  `SERVICE_TIMEOUT` constant and fields through the paired `:cc`/`:cc_shared`
  targets.

No source message, CMake, or package manifest change was required.

Host validation:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test //bazel/tests:autoware_common_msgs_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds //bazel/tests:autoware_common_msgs_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_common_msgs//...
```

The consumer passed with both middleware configurations. Building
`@autoware_common_msgs//...` built all 12 targets exposed by the module,
including its C, C++, Python, Rust, and protobuf generator outputs. Existing
C++23 deprecation warnings from `rosidl_runtime_cpp` remained non-fatal.

Development-image validation mounted the source read-only:

```bash
docker run --rm \
  -v autoware-bazel-bootstrap-cache:/home/ubuntu/.cache \
  -v "$PWD:/workspace:ro" \
  autoware-bazel-dev:bootstrap \
  bazel test --experimental_convenience_symlinks=ignore \
  //bazel/tests:autoware_common_msgs_test
```

The test passed. Its host and container executable SHA-256 digests matched:
`6b1eaf06cfb2e8a01ab9cbcd91106d315687f0074c46e0f6805dbefe4099506f`.

Result: one package-local module can expose the same generated C++ target shape
as an RCR interface package and can be consumed from the root without a sourced
ROS installation. This establishes the primitive-only package shape, not yet
the cross-package convention.

Static validation: `git diff --check`, lockfile JSON parsing, `clang-format`,
`cpplint`, `markdownlint`, Prettier, end-of-file, trailing-whitespace, and
private-key checks passed. Bazel parsed all new module and BUILD files during
the host and development-image builds.

Reproduction notes: a primitive-only interface package needs four integration
points. Add a package-local `MODULE.bazel` with
`rosidl_default_generators@0.0.0`; declare each `.msg` with `ros_message` in
`msg/BUILD.bazel`; aggregate those rules with `default_generators` in the
package root; and select the package from the workspace root with its concrete
published version plus `local_path_override`. A root C++ consumer should depend
on the package `:cc` target and collect its `:cc_shared` target dynamically.
Build `@package//...`, not only the consumer, to expose failures in non-C++
generators. The concrete version used here was
`autoware_common_msgs@1.11.0-3.rcr.2`.

Next: test local-versus-RCR source selection without changing the consumer,
then add a second interface package with an Autoware package dependency.

## 2026-11-14: Validate the interface package convention

Goal: complete the interface-layout experiment by switching the existing
consumer to a published source, adding a second module with an Autoware
interface dependency, and exercising a generated message over both supported
middleware paths.

The source-selection trial replaced the local `autoware_common_msgs` override
with the concrete published version `1.11.0-3.rcr.2` and left
`//bazel/tests:autoware_common_msgs_test` unchanged:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --lockfile_mode=off \
  //bazel/tests:autoware_common_msgs_test
```

The test passed. The published module analysis loaded 199 packages and
configured 59,214 targets with 258 aspect applications. Restoring the local
override while retaining the concrete dependency version made source selection
a root-module concern; the consumer label and generated C++ target shape are
identical.

`autoware_localization_msgs` was selected as the second module because it has
only `KinematicState.msg` and `InitializeLocalization.srv`, while the service
directly references `autoware_common_msgs/ResponseStatus`. The package-local
module declares `autoware_common_msgs`, `geometry_msgs`, `std_msgs`, and
`rosidl_default_generators`. Its individual interface rules name the exact
referenced message targets, and `default_generators.dependencies` registers the
three dependent interface packages.

The local C++ consumers passed:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test \
  //bazel/tests:autoware_common_msgs_test \
  //bazel/tests:autoware_localization_msgs_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds \
  //bazel/tests:autoware_common_msgs_test \
  //bazel/tests:autoware_localization_msgs_test
```

The localization consumer constructs both generated interfaces and verifies
the nested `ResponseStatus` fields and service constant. The primary test graph
configured 69,351 targets across 331 packages with 994 aspect applications.
The Fast DDS tests also passed.

The complete localization module surface built 13 targets, including C, C++,
Python, Rust, and protobuf outputs:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_localization_msgs//...
```

That graph configured 61,273 targets with 1,590 aspect applications and 7,851
declared actions. Existing C++23 deprecation warnings from
`rosidl_runtime_cpp` remained non-fatal.

For runtime behavior, `localization_talker` published `KinematicState` values
with a `map` parent, a numbered `base_link_*` child, and a matching x position.
The listener validated the frame fields and received five messages before
exiting successfully. The behavior passed first with Cyclone DDS and then with
the same targets under `--config=fastdds`. In both runs the publisher continued
until stopped after the listener completed.

The read-only development-image check passed both consumers:

```bash
docker run --rm \
  -v autoware-bazel-bootstrap-cache:/home/ubuntu/.cache \
  -v "$PWD:/workspace:ro" \
  autoware-bazel-dev:bootstrap \
  bazel test --experimental_convenience_symlinks=ignore \
  //bazel/tests:autoware_common_msgs_test \
  //bazel/tests:autoware_localization_msgs_test
```

The container configured 69,312 targets across 331 packages with 994 aspect
applications. Both tests passed; the uncached localization portion completed
with the source mount read-only.

Targeted static validation passed for the changed files: `git diff --check`,
lockfile JSON parsing, clang-format, cpplint, markdownlint, and Prettier. The
final payload-consistency check was followed by fresh Cyclone DDS and Fast DDS
consumer tests, builds, and five-message behavior runs.

Result: the phase exit criteria passed. Two local interface modules build,
their cross-package dependency follows the generated RCR target convention,
the root consumer can select local or registry source without a BUILD change,
and a generated Autoware message passes Cyclone DDS and Fast DDS behavior.

Reproduction notes: every referenced external interface creates three matching
edges: a direct package dependency in the package-local module, the exact
message label on the `ros_message` or `ros_service` rule, and the package
repository in `default_generators.dependencies`. Keep manifest-only
dependencies in `MODULE.bazel` even when no current interface references them;
do not add them to generator inputs. To test source interchangeability, remove
only the local override, use `--lockfile_mode=off` for the temporary trial, and
run the unchanged consumer against the published version. A cross-package
service plus a five-message publisher/listener run proves more than a header
compilation test alone.

Next: begin Phase 2 with the smallest dependency layer among the remaining
Autoware interface packages. Port a bounded group manually before deciding
whether repeated declarations justify a generator.

## 2026-11-14: Start the interface package set

Goal: begin Phase 2 with a bounded dependency layer and validate two additional
message shapes without introducing another unported Autoware dependency.

The remaining package inventory identified `autoware_control_msgs` and
`autoware_v2x_msgs` as the smallest independent pair. Each contains four
messages. Control depends only on `builtin_interfaces`; v2x declares
`builtin_interfaces` and `geometry_msgs`, although its current message files
reference only `builtin_interfaces`. The slice will retain manifest-level
module dependencies while limiting individual message rules and
`default_generators.dependencies` to interfaces actually referenced by the
generated types.

Success conditions:

- both complete generated module surfaces build with Cyclone DDS selected;
- one root C++ consumer verifies nested control messages and bounded v2x time
  sequences;
- the consumer and complete module surfaces pass the Fast DDS compatibility
  gate;
- the read-only development image builds and runs the consumer.

Changes:

- registered both packages in the root module at their published
  `1.11.0-3.rcr.2` versions and selected their source snapshots with local
  overrides;
- added package-local modules, package generator targets, and eight individual
  `ros_message` targets;
- added one root C++ consumer for nested `ControlHorizon` values and a
  `VirtualGateCommand` whose area contains a bounded arrival-time sequence.

The v2x package retains its manifest-level `geometry_msgs` dependency in
`MODULE.bazel`. Its message rules and `default_generators.dependencies` omit
`geometry_msgs` because no current v2x message references one of its types.

Primary validation used the clean caller environment:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test //bazel/tests:autoware_interface_layer_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_control_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_v2x_msgs//...
```

The consumer passed. Its graph loaded 325 packages and configured 69,225
targets with 346 aspect applications. Each module exposed and built 15 targets,
including the C, C++, Python, Rust, and protobuf surfaces. The control module's
build graph configured 2,476 targets with 311 aspect applications and 3,472
declared actions. Existing C++23 deprecation warnings remained non-fatal.

The Fast DDS bounded-group gate passed:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=fastdds \
  @autoware_control_msgs//... @autoware_v2x_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --nocache_test_results \
  //bazel/tests:autoware_interface_layer_test
```

All 30 module targets built, and forcing test execution confirmed that the
consumer passed rather than only reusing its earlier test result.

The read-only development-image command could not start:

```bash
docker run --rm \
  -v autoware-bazel-bootstrap-cache:/home/ubuntu/.cache \
  -v "$PWD:/workspace:ro" \
  autoware-bazel-dev:bootstrap \
  bazel test --experimental_convenience_symlinks=ignore \
  --nocache_test_results //bazel/tests:autoware_interface_layer_test
```

Symptom: Docker failed before starting a container because the selected
`desktop-linux` socket did not exist. Cause: neither the Docker Desktop socket
nor `/var/run/docker.sock` was available. No package or Bazel action ran, so
container validation remains pending until a Docker daemon is available.

Result: the first Phase 2 host slice passed its Cyclone DDS and Fast DDS gates
without a source compatibility fix. The repeated package structure is still
small enough to keep manual; no generator was added.

Targeted static validation passed for the changed files: `git diff --check`,
lockfile JSON parsing, clang-format, cpplint, markdownlint, and Prettier. Bazel
parsed every new module and BUILD file in the host builds. Pre-commit was not
run.

Reproduction notes: derive each bounded layer from `package.xml` and the actual
type references in `.msg` and `.srv` files. Keep the package manifest dependency
set in its module, but let rule-level and generator dependency sets follow
actual references. Register each local package at its published RCR version and
override it to the source snapshot. Use a combined consumer to exercise new
shapes, then build each entire module surface and force the compatibility test
with `--nocache_test_results`. A missing Docker daemon blocks only the container
gate; it does not invalidate the recorded host build and test results.

Next: add `autoware_system_msgs`, the smallest remaining interface package and
the next package whose only Autoware dependency is already integrated. Repeat
the read-only development-image test when the Docker daemon is available.

## 2026-11-14: Build the expanded interface base layer

Goal: integrate the five remaining packages whose Autoware dependencies were
already available, validate every generated target, and exercise representative
types through one root consumer.

Added package-local modules and interface targets for:

- `autoware_system_msgs`: 5 interfaces and 16 package targets;
- `autoware_sensing_msgs`: 9 interfaces and 20 package targets;
- `autoware_map_msgs`: 13 interfaces and 24 package targets;
- `autoware_planning_msgs`: 13 interfaces and 24 package targets;
- `autoware_perception_msgs`: 19 interfaces and 30 package targets.

Each module retains dependencies from its package manifest. Individual
interface rules and `default_generators.dependencies` contain only referenced
types. The published RCR target relationships matched the current source
filenames and served as the initial dependency hypothesis.

The primary builds ran sequentially:

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
```

System, sensing, planning, and perception built without adjustment. The map
build initially failed during analysis because `@geographic_msgs//` resolved to
the `0.0.0` stub, which has no root BUILD package. The cause was that
`geographic_msgs` belongs to the full ROS release but not `ros_core`, so no
dependency selected its concrete version. Pinning
`geographic_msgs@1.0.6-3.rcr.2` in the root module selected the generated
package, after which all 24 map targets built.

The completed map graph configured 71,908 targets across 380 packages with
2,521 aspect applications and 11,985 declared actions. The other successful
module builds exposed their expected target counts; existing C++23 deprecation
warnings remained non-fatal.

The combined consumer constructs a system service request, nested sensing
radar data, a geographic map origin, a planning trajectory point, and a
predicted perception object:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test //bazel/tests:autoware_interface_base_test
```

The test compiled and executed successfully.

The bounded-group compatibility gate built all 114 package targets and forced
the consumer to execute:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=fastdds \
  @autoware_system_msgs//... @autoware_sensing_msgs//... \
  @autoware_map_msgs//... @autoware_planning_msgs//... \
  @autoware_perception_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --nocache_test_results \
  //bazel/tests:autoware_interface_base_test
```

Both commands passed. The combined Fast DDS analysis configured 59,689 targets
with 3,416 aspect applications; all 15,780 actions were available from the
action cache.

Targeted static validation passed: `git diff --check`, lockfile JSON parsing,
clang-format, cpplint, markdownlint, and Prettier. Bazel parsed every new module
and BUILD file. Pre-commit was not run.

Result: all five base-layer packages build from the current source snapshots,
and representative generated types pass C++ consumer execution under both
middleware configurations. No source compatibility fix was required.

Reproduction notes: add packages in dependency order so every Autoware label is
already selectable when its consumer is introduced. Published RCR-generated
rules are useful dependency hypotheses, but confirm every rule name and edge
against the current source inventory. A package outside the selected release
set needs a concrete root selection: `geographic_msgs` was not selected by
`ros_core`, so `geographic_msgs@1.0.6-3.rcr.2` was required even though the map
package's local module used a `0.0.0` dependency. Build modules sequentially to
respect local resource limits, then use one consumer covering at least one
representative type from every package in the layer.

Next: add the dependent vehicle and simulation packages, then run the combined
message-set compatibility gate. The Docker validation remains pending until a
daemon is available.

## 2026-11-14: Complete the Autoware interface package set

Goal: add the two interface packages that depend on the completed base layer,
exercise their generated C++ types, and validate every integrated message
target as one graph.

Added package-local modules and complete interface rules for:

- `autoware_vehicle_msgs`: 14 messages, one service, and 26 package targets;
- `autoware_simulation_msgs`: two messages and 13 package targets.

The root selects the published vehicle module version and redirects it to the
current source snapshot. No Lyrical RCR module exists for simulation, so its
root dependency uses `0.0.0` with a mandatory local override. Both package
modules retain their manifest-level dependencies; generator inputs contain
only interfaces referenced by their current definitions.

The primary package builds ran sequentially:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_vehicle_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_simulation_msgs//...
```

Vehicle built all 26 targets. The first simulation build failed while
generating the `SimulatedObject` type description: `Shape.json` was missing.
The `SimulatedObject.msg` source referenced both `ObjectClassification` and
`Shape`, but the Bazel rule declared only the former. Temporarily removing the
perception package from the root generator dependencies reproduced the same
failure, ruling out an over-broad package edge. Adding the explicit
`@autoware_perception_msgs//msg:Shape` rule dependency and restoring the package
generator input fixed the cause; all 13 simulation targets then built.

The dependent-layer consumer constructs a stamped actuation command and a
simulated object containing perception classification and shape fields:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results \
  //bazel/tests:autoware_interface_dependent_test
```

Its first execution failed on the expected numeric value for
`Shape::BOUNDING_BOX`. The message source defines that constant as zero, not
one. Correcting the assertion to the source-defined value made the test pass.

The dependent-layer Fast DDS gate passed all 39 package targets and forced the
consumer to execute:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=fastdds \
  @autoware_vehicle_msgs//... @autoware_simulation_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --nocache_test_results \
  //bazel/tests:autoware_interface_dependent_test
```

The complete interface build then passed for all 208 targets from
`autoware_common_msgs`, `autoware_control_msgs`,
`autoware_localization_msgs`, `autoware_map_msgs`,
`autoware_perception_msgs`, `autoware_planning_msgs`,
`autoware_sensing_msgs`, `autoware_simulation_msgs`,
`autoware_system_msgs`, `autoware_v2x_msgs`, and
`autoware_vehicle_msgs`:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build \
  @autoware_common_msgs//... @autoware_control_msgs//... \
  @autoware_localization_msgs//... @autoware_map_msgs//... \
  @autoware_perception_msgs//... @autoware_planning_msgs//... \
  @autoware_sensing_msgs//... @autoware_simulation_msgs//... \
  @autoware_system_msgs//... @autoware_v2x_msgs//... \
  @autoware_vehicle_msgs//...
```

The command passed once with the default Cyclone DDS configuration and once
with `--config=fastdds`. Each analysis found 208 targets, with the final Fast
DDS gate configuring 59,776 targets and applying 3,876 aspects; all 17,789
actions were available from the action cache.

Finally, one command forced all five interface consumers to execute together:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results \
  //bazel/tests:autoware_common_msgs_test \
  //bazel/tests:autoware_localization_msgs_test \
  //bazel/tests:autoware_interface_layer_test \
  //bazel/tests:autoware_interface_base_test \
  //bazel/tests:autoware_interface_dependent_test
```

All five passed under Cyclone DDS. Repeating the command with
`--config=fastdds` also passed all five. Existing C++23 deprecation and
rules_python implicit-`__init__.py` warnings remained non-fatal.

The `autoware_msgs` package itself is an ament metapackage: it contains no
interface definition or generated output and lists these same 11 packages as
runtime dependencies. The combined module gate therefore covers its interface
set directly without adding an output-free Bazel module.

Targeted static validation passed: `git diff --check`, lockfile JSON parsing,
clang-format, cpplint, markdownlint, and Prettier. Bazel parsed every new module
and BUILD file in the package and combined builds. Pre-commit was not run.

Result: the complete current Autoware interface package set builds and its
representative generated C++ types execute under both middleware
configurations. No source compatibility change was required. Docker validation
remains pending until a daemon is available.

Reproduction notes: use a concrete published version plus local override when
an RCR counterpart exists. When it does not, as for
`autoware_simulation_msgs`, select `0.0.0` at the root and treat the local
override as mandatory. Type-description failures naming a missing JSON file
usually identify an omitted rule-level interface edge; compare every source
field with the `ros_message.deps` list before changing package-wide generator
inputs. Verify enum values from the source definition rather than assuming ROS
conventions. An output-free ament metapackage needs no Bazel module when a
combined gate directly covers all of its member modules.

Next: select a small runnable Autoware core target, trace its utility-package
dependencies, and establish only the shared Bazel conventions that target
requires.

## 2026-11-14: Validate the complete interface set in the development image

Goal: close the pending development-image check by building and testing the
complete Phase 2 interface graph with the repository mounted read-only.

Docker Desktop 29.8.2 was available. The existing pinned
`autoware-bazel-dev:bootstrap` image and
`autoware-bazel-bootstrap-cache` volume were reused. The primary container
build ran:

```bash
docker run --rm \
  -v autoware-bazel-bootstrap-cache:/home/ubuntu/.cache \
  -v "$PWD:/workspace:ro" \
  autoware-bazel-dev:bootstrap \
  bazel build --experimental_convenience_symlinks=ignore \
  @autoware_common_msgs//... @autoware_control_msgs//... \
  @autoware_localization_msgs//... @autoware_map_msgs//... \
  @autoware_perception_msgs//... @autoware_planning_msgs//... \
  @autoware_sensing_msgs//... @autoware_simulation_msgs//... \
  @autoware_system_msgs//... @autoware_v2x_msgs//... \
  @autoware_vehicle_msgs//...
```

The Cyclone DDS build passed all 208 targets from the read-only mount. It
loaded 407 packages, configured 72,152 targets, applied 3,876 aspects, and
completed 11,917 actions in 420.762 seconds. The container cache was mostly
cold for the expanded package set: 4,921 action-cache hits and 1,858 remote
cache hits were reported.

All five C++ consumers were then forced to execute:

```bash
docker run --rm \
  -v autoware-bazel-bootstrap-cache:/home/ubuntu/.cache \
  -v "$PWD:/workspace:ro" \
  autoware-bazel-dev:bootstrap \
  bazel test --experimental_convenience_symlinks=ignore \
  --nocache_test_results \
  //bazel/tests:autoware_common_msgs_test \
  //bazel/tests:autoware_localization_msgs_test \
  //bazel/tests:autoware_interface_layer_test \
  //bazel/tests:autoware_interface_base_test \
  //bazel/tests:autoware_interface_dependent_test
```

All five tests passed. The command configured 69,631 targets and applied 2,625
aspects.

Repeating the 208-target build with `--config=fastdds` passed with 17,789
action-cache hits. Repeating the five-test command with `--config=fastdds`
forced all five consumers to execute, and all passed. Existing rules_python,
C++23, protobuf, and generic-service deprecation warnings remained non-fatal.

Result: the complete Autoware interface set builds and its generated C++
consumers execute in the minimal development image under both middleware
configurations. The read-only mount confirms that Bazel writes its working
state outside the source workspace when convenience symlinks are disabled.

Targeted documentation validation passed: `git diff --check`, markdownlint,
and Prettier. Pre-commit was not run.

Reproduction notes: build the pinned image first if it is not already present,
create the named cache volume, and mount the source at `/workspace:ro`. Pass
`--experimental_convenience_symlinks=ignore` so Bazel does not try to create
workspace output symlinks. Run the complete module build before the consumer
tests to validate every language generator, and use `--nocache_test_results` to
prove test execution. Repeat both commands with `--config=fastdds`; a warm
action cache is acceptable because analysis, target discovery, and forced test
execution still validate the source graph and runtime closure.

Next: select a small runnable Autoware core target, trace its utility-package
dependencies, and establish only the shared Bazel conventions that target
requires.

## 2026-11-14: Make the experiment journal independently reproducible

Goal: verify that every existing entry explains enough purpose, integration
method, transferable knowledge, decisions, and validation for a reader to apply
the experiment without inspecting this branch's diffs.

The audit found that build and behavior evidence was already detailed, but
several entries relied on the changed files to reveal the minimum configuration
or package rule pattern. Added a cumulative-use guide and a reproduction note
to every dated entry. The notes now identify the essential bootstrap pins and
runtime layout, middleware selection flags, package-local module structure,
three required cross-package interface edges, dependency-layer inventory
method, concrete versus local-only version selection, missing-interface
diagnosis, and read-only container method. Planning-only entries now state
explicitly that their result is a policy or workflow rather than runtime
evidence.

The requirement is now durable in `AGENTS.md` and this journal's reproduction
preface: future entries must be understandable without a commit diff and must
include purpose, prerequisites, the minimum integration pattern and rationale,
decisions and failure diagnoses, exact verification with observable results,
and remaining limitations.

Coverage was checked mechanically by splitting JOURNAL.md at every dated
heading and confirming that each section contains `Goal:`, `Reproduction
notes:`, a validation command or statement, `Result:`, and `Next:`. All 12
entries passed that structural audit. Content was then reviewed against the
five requested categories; the structural check is only a guard against
omission, not a substitute for that review.

Targeted documentation validation passed:

```bash
markdownlint -c .markdownlint.yaml \
  AGENTS.md docs/bazel/DEVELOPING.md docs/bazel/JOURNAL.md
prettier --check \
  AGENTS.md docs/bazel/DEVELOPING.md docs/bazel/JOURNAL.md
git diff --check
```

No build or runtime validation was run because this task changes only
experiment documentation. Pre-commit was not run.

Reproduction notes: use the journal standard as a content rubric, then use the
five-marker structural audit only to catch missing sections. Do not make every
entry self-contained by copying earlier setup; name the preceding entry as a
cumulative prerequisite and restate only the new pattern, decision, evidence,
or limitation introduced by the current task.

Result: the journal now provides a chronological implementation method rather
than requiring access to this branch's diffs. Future work has an explicit rule
and workflow checklist to preserve that property.

Next: select a small runnable Autoware core target, trace its utility-package
dependencies, and apply the reproducible-entry standard while establishing
only the shared Bazel conventions that target requires.

## 2026-11-14: Separate adoption guidance from steady-state development

Goal: give the adoption record and developer guide distinct audiences so a
project adopting Bazel does not confuse migration experiments with normal
post-adoption development.

Defined the documentation boundary in PLAN.md and AGENTS.md. PLAN.md owns the
adoption strategy and durable decisions. JOURNAL.md owns chronological adoption
to-dos, milestone gates, experiments, diagnosed failures, and verification for
a non-Bazel Autoware tree. DEVELOPING.md assumes the relevant package or target
already has Bazel support and documents only the steady-state developer
workflow.

Replaced the phase-by-phase integration narrative in DEVELOPING.md with a daily
guide covering prerequisites, narrow build/test/run loops, middleware
configurations, source layout, dependency conventions, interface maintenance,
read-only container checks, and commit validation. Bootstrap trials, package
port order, adoption-specific smoke consumers, journal-writing procedure, and
milestone history remain in this journal instead.

Targeted documentation validation:

```bash
markdownlint -c .markdownlint.yaml \
  AGENTS.md docs/bazel/PLAN.md docs/bazel/JOURNAL.md \
  docs/bazel/DEVELOPING.md
prettier --check \
  AGENTS.md docs/bazel/PLAN.md docs/bazel/JOURNAL.md \
  docs/bazel/DEVELOPING.md
git diff --check
```

No build or runtime validation is required because the change only reorganizes
and clarifies documentation. Pre-commit is not run.

Reproduction notes: when adapting this documentation model elsewhere, put
questions such as "what must be ported next, what failed, and what evidence
closes the milestone?" in the adoption journal. Put questions such as "how do
I build, test, run, add a dependency, or validate my change now that Bazel is
supported?" in the developer guide. Promote a command into the developer guide
only after the adoption journal has validated it as a stable workflow.

Result: the distinction is now explicit in all three entry points, and
DEVELOPING.md can be read as the post-adoption developer contract without
following the experiment history.

Next: select a small runnable Autoware core target and record its adoption work
here; add only the resulting stable developer workflow to DEVELOPING.md.

## 2026-11-14: Start utility integration with autoware_utils_math

Goal: establish the minimum package-local Bazel pattern for an Autoware C++
library and its existing unit tests without introducing ROS runtime or core
node dependencies.

Inventorying the 12 packages under `src/core/autoware_utils` identified
`autoware_utils_math` as a leaf: its manifest has no runtime dependencies, its
CMake target compiles `src/sin_table.cpp` and `src/trigonometry.cpp`, and its
test target collects `test/*.cpp`. The root now selects the local package at
the published `1.11.0-3.rcr.2` version. Its package module directly declares
`rules_cc` and GoogleTest, and its BUILD file exposes the `include/` tree with
that prefix stripped so existing `#include "autoware_utils_math/..."` lines
remain unchanged.

The first analysis attempt loaded `cc_test` from `cc_binary.bzl`. Bazel reported
that the file does not export that symbol. The workspace's existing smoke BUILD
demonstrated the supported public entry point, so both `cc_library` and
`cc_test` now load from `@rules_cc//cc:defs.bzl`.

The next attempt declared GoogleTest with `dev_dependency = True`. Analysis
reported that `@googletest` was not visible from `@autoware_utils_math`.
Bzlmod ignores development dependencies of a non-root module; a local override
does not make that package the root. Removing the development-only marker made
the package-owned test dependency visible without adding it to the root
module.

Verification ran the full package surface and forced the test to execute:

```bash
bazel mod tidy
bazel test --nocache_test_results @autoware_utils_math//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results @autoware_utils_math//...
```

Bazel analyzed the library and test targets, completed 28 actions with a warm
cache, and executed `test_autoware_utils_math`; the single Bazel test target
passed. The final clean-caller run analyzed both targets and forced the test to
execute again; it passed with 15 action-cache hits. GoogleTest emitted two
non-fatal warnings during compilation for the source package's use of deprecated
`INSTANTIATE_TEST_CASE_P`. No middleware compatibility run was needed because
this package has no ROS or RMW dependency. `git diff --check` passed;
markdownlint and Prettier were unavailable in the caller environment.
Pre-commit was not run.

Reproduction notes: create a package-local module with normal direct
dependencies on `rules_cc` and GoogleTest, add a root dependency and
`local_path_override` at the published package version, and expose the public
headers with `strip_include_prefix = "include"`. Do not mark GoogleTest as a
development dependency when tests are invoked from the consuming root. Load
the C++ rules from `@rules_cc//cc:defs.bzl`, then build and execute the whole
package with the command above.

Result: the first utility library builds from local source and its complete
upstream unit-test source set executes under Bazel. The slice establishes only
the C++ library and test conventions; ROS dependencies, shared-library needs,
and runtime data remain intentionally untested.

Next: add the next leaf utility packages in dependency order, using each
package's current sources and tests to determine which ROS and linking
conventions are actually required.

## 2026-11-14: Add system and rclcpp utility leaves

Goal: extend the utility layer to the smallest ROS-dependent packages and
validate the publisher/subscriber test behavior needed before the later
Agnocast phase.

`autoware_utils_system` is a compiled library: `backtrace.cpp` uses the RCLCPP
logger, while its other public utilities are header-only. Its Bazel target
therefore compiles the source against `@rclcpp//:rclcpp_library` and exposes all
three headers. Its four-file GoogleTest source set covers backtraces, the LRU
cache, and the stopwatch.

`autoware_utils_rclcpp` has no compiled source. Its library exposes the
parameter and polling-subscriber headers against the same RCLCPP compile
target. The three-file test source set additionally depends on the paired
`@std_msgs//:cc` and `:cc_shared` targets. The existing polling-subscriber tests
create publishers, subscribers, and an executor, then verify message and
timestamp behavior for the Latest, Newest, and All policies.

The first BUILD attempt put `dynamic_deps` on both `cc_library` rules. Analysis
failed because `cc_library` has no such attribute. The compile-only dependency
remains on each library; `collect_transitive_dynamic_deps` now supplies RCLCPP
and generated-message shared libraries to the final test executables. This
keeps runtime closure at the executable boundary while allowing downstream
libraries to propagate headers and compile providers.

Verification forced both suites to execute in a clean caller environment with
Cyclone DDS, then repeated the same gate with Fast DDS:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results \
  @autoware_utils_system//... @autoware_utils_rclcpp//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --nocache_test_results \
  @autoware_utils_system//... @autoware_utils_rclcpp//...
```

The clean Cyclone run analyzed six targets and both tests passed in 1.098
seconds with 19 action-cache hits. The Fast DDS compatibility run analyzed the
same six targets, completed 163 actions with 7,227 action-cache hits, and both
tests passed. Existing rules_python and ROS C++ deprecation warnings remained
non-fatal. `git diff --check` passed. Pre-commit was not run.

Reproduction notes: represent a header-only package with a `cc_library` whose
`hdrs` use `strip_include_prefix = "include"`; omit `srcs` rather than creating
a placeholder. For ROS libraries, place the compile provider in the library's
`deps`, then collect the matching shared-library target on every final binary
or test. Generated messages require their paired compile and shared targets as
well. Force the package tests under the primary RMW and repeat the exact target
set with the compatibility configuration whenever the tests communicate.

Result: three utility packages are now available locally. The new RCLCPP
utility test proves publisher/subscriber behavior under both supported
middleware configurations, so the communication-test mechanism needed for a
minimal core slice is established without adding any core or Agnocast target.

Next: continue the utility dependency graph with the remaining independent
leaves, then add geometry and TF only after their direct prerequisites are
available.

## 2026-11-14: Add UUID and visualization utility leaves

Goal: integrate the next independent utility sublayer and validate two new
dependency shapes: a narrow rosdistro Boost header target paired with a
generated ROS message, and a compiled library using RCLCPP plus generated
visualization messages.

The inspected inputs were both source packages' manifests, CMake files,
headers, sources, and tests; the RCR metadata and module manifests for
`unique_identifier_msgs@2.8.1-3.rcr.2` and
`visualization_msgs@5.9.3-1.rcr.0`; and the Boost targets exported by the pinned
rosdistro. That rosdistro exports the required header surface as
`@rosdistro//cc:boost.uuid`.

`autoware_utils_uuid` is header-only. Its library exposes `uuid_helper.hpp` and
depends on the rosdistro's Boost.UUID target plus
`@unique_identifier_msgs//:cc`; its test supplies the paired `:cc_shared`
runtime closure. `autoware_utils_visualization` compiles
`marker_helper.cpp`, exports `marker_helper.hpp`, and uses the compile targets
from RCLCPP and `visualization_msgs`. Its final test collects both corresponding
shared-library targets.

The root selects both external ROS package versions explicitly because the
local BUILD files expose their labels and they are not existing direct root
dependencies. The local package modules retain `0.0.0` ROS dependencies so the
root remains responsible for the concrete release. Both local Autoware modules
are selected at their published `1.11.0-3.rcr.2` versions.

An initial dependency hypothesis used the BCR
`boost.uuid@1.90.0.bcr.1` module. The code built and tested, but that module
also selected BCR's umbrella `boost` module. Querying the pinned rosdistro's C++
targets demonstrated that the ROS release already provides `boost.uuid`.
Replacing the BCR dependency with that target removed the Boost umbrella from
the resolved module graph while retaining a package-specific dependency.

Running `bazel mod tidy` during that trial also evaluated module extensions
already reachable elsewhere in the workspace and recorded 992 unrelated lines
of extension state. This was command-induced lockfile noise rather than a
requirement of either utility. Restoring the lockfile and resolving through the
actual test targets produced only the new `visualization_msgs@0.0.0` lookup and
an updated rosdistro extension usage digest. Those two minimal changes are the
accepted lockfile result.

Verification first resolved the modules and exercised the complete local
package surfaces, then repeated the forced tests from a clean caller under both
middleware configurations:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results \
  @autoware_utils_uuid//... @autoware_utils_visualization//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --nocache_test_results \
  @autoware_utils_uuid//... @autoware_utils_visualization//...
```

The accepted clean Cyclone run analyzed six targets, completed 149 actions with
9,210 action-cache hits, and both tests passed. The Fast DDS run completed 149
actions with 10,781 action-cache hits and both tests passed. The tests exercise
UUID conversion/generation and marker construction; they do not perform
middleware communication. Existing rules_python and ROS C++ deprecation
warnings remained non-fatal. `git diff --check` passed. Pre-commit was not run.

Reproduction notes: inspect the exact Boost headers used, then query the pinned
rosdistro's C++ targets before adding a BCR module. Prefer its package-specific
target when available. Inspect lockfile changes after module-management
commands and retain only state reproducibly required by target resolution. For
a header-only utility, put generated message compile targets on the library and
their shared counterparts on each final test or binary. For a
compiled utility, do the same for every RCR library referenced by its public
headers or source. Select concrete external ROS releases at the root while
using `0.0.0` in package-local modules.

Result: five utility packages now build from local source and their complete
existing test suites pass. UUID and visualization introduce no new source
compatibility changes and preserve the executable-boundary dynamic-link
convention established by the preceding slice.

Next: integrate the logging and diagnostics leaves, whose tests add generated
service and diagnostic-updater behavior, before entering the geometry and TF
dependency chain.

## 2026-11-14: Add logging and diagnostics utility leaves

Goal: integrate the remaining independent ROS utility leaves before entering
the geometry and TF chain, and validate generated-service, direct C-library,
diagnostic-updater, and diagnostics publisher dependency shapes.

Prerequisites and inspected inputs were both packages' manifests, CMake files,
public headers, compiled sources, and complete test source sets; the RCR
metadata and module manifests; and the targets exported by the selected RCR
repositories. The selected releases are `logging_demo@0.37.9-1.rcr.0`,
`diagnostic_updater@4.4.7-1.rcr.2`, and
`diagnostic_msgs@5.9.3-1.rcr.0`. Target queries confirmed the compile/shared
pairs `@logging_demo//:cc` and `:cc_shared`, `@rcutils//:rcutils_library` and
`:rcutils`, and `@diagnostic_updater//:diagnostic_updater_library` and
`:diagnostic_updater`.

`autoware_utils_logging` is header-only. Its public template creates a
`logging_demo::srv::ConfigLogger` service through RCLCPP and directly calls the
RCUtils logging API, so its library declares all three compile providers. The
test executable collects their shared-library counterparts. RCUtils is exposed
as a direct root `0.0.0` dependency selected by the pinned `ros_core` release;
the two packages outside `ros_core` use concrete root versions. The package
module uses `0.0.0` for ROS dependencies so the root retains version control.

`autoware_utils_diagnostics` compiles `timeout_diagnostics.cpp` and exports both
diagnostics headers. Its library depends directly on RCLCPP, generated
`diagnostic_msgs`, and the compiled `diagnostic_updater` provider. The test
executable collects the corresponding shared libraries. This is the minimum
pattern required by the public headers and compiled source; no geometry or TF
dependency is introduced by this slice.

Verification forced both complete package suites from a clean caller under the
primary and compatibility middleware configurations:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results \
  @autoware_utils_logging//... @autoware_utils_diagnostics//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --nocache_test_results \
  @autoware_utils_logging//... @autoware_utils_diagnostics//...
```

The Cyclone DDS run analyzed six targets, completed 37 actions with 6,099
action-cache hits, and both tests passed. The Fast DDS run analyzed the same
surface, completed 158 actions with 7,549 action-cache hits, and both tests
passed. Existing rules_python and ROS C++ deprecation warnings remained
non-fatal. `git diff --check` passed. Pre-commit was not run.

The diagnostics suite publishes and receives `DiagnosticArray` messages across
multiple clear cycles, and verifies level, message, key replacement, and value
behavior. This supplies middleware behavior evidence under both RMWs. The
logging test currently verifies only that a node can construct the generated
service, and the timeout test only constructs its diagnostic task; neither
invokes the service callback nor exercises timeout-level transitions. Those
are test-coverage limitations, not Bazel build failures.

Reproduction notes: inspect every public header as well as compiled sources;
manifest dependencies alone do not distinguish compile-only, generated, and
shared runtime labels. Query each RCR package's exported targets, declare the
compile provider directly on the library, and collect the paired shared target
at every final executable. Generated services use the same `:cc`/`:cc_shared`
pair as messages. A public call into a C package such as RCUtils warrants a
direct dependency even when RCLCPP also reaches it transitively. Pin packages
outside `ros_core` concretely at the root and keep package-local ROS versions
at `0.0.0`.

Result: seven utility packages now build from local source and their complete
existing test suites pass. The integration adds no source compatibility patch,
and the lockfile records only the selected RCR releases, their `0.0.0` lookup
modules, and the updated rosdistro extension digest.

Next: inspect the geometry and TF utility dependency chain from its first
unavailable leaf, adding only prerequisites exposed by the first current
target.

## 2026-11-14: Add the geometry and TF utility chain

Goal: integrate geometry and TF through explicit prerequisite gates so each
new dependency shape is validated before the next layer is introduced. The
sequence was the local planning interfaces, compiled geometry, then the
header-only TF helpers.

The inspected inputs were the current interface definitions, package
manifests, CMake source lists, public and private includes, and complete test
sources for all three packages; the RCR metadata for the corresponding
Autoware and Geometry2 releases; and the actual targets exported by resolved
repositories. The interface release is
`autoware_internal_planning_msgs@1.16.0-1.rcr.0`. The TF2 packages use the
matching `0.45.10-1.rcr.0` release.

The first gate added all 21 messages and five services from
`autoware_internal_planning_msgs`, not only `PathWithLaneId` needed by
geometry. Each `ros_message` and `ros_service` lists its direct referenced
types, while `default_generators` lists every external package used anywhere
in the generated graph. The root selects the published interface version and
overrides it with the local source snapshot. Building `//...` therefore checks
the complete generator surface instead of one C++ consumer.

The second gate added `autoware_utils_geometry` as one compiled library over
all ten current sources and public headers. Direct dependencies come from the
actual includes: the local internal-planning, perception, and planning message
targets; math; Geometry messages; TF2 and TF2 Geometry Messages; and the
rosdistro's narrow Eigen, Boost.Config, and Boost.Geometry targets. The test
adds the system utility and collects every paired ROS shared target at its
executable boundary. Although the manifest lists `tf2_eigen`, no current
header or source includes it, so it was not introduced.

The third gate added header-only `autoware_utils_tf` over geometry, Geometry
messages, RCLCPP, and TF2 ROS. Its test executable collects both its direct
shared dependencies and the shared ROS closure of the compiled geometry
library. This preserves the current executable-boundary convention without
prematurely creating installable package shared libraries.

The first geometry analysis failed with:

```text
no such package '@@tf2_geometry_msgs+//': BUILD file not found
```

The root initially selected `tf2` and `tf2_geometry_msgs` at `0.0.0`. In this
graph no other root dependency upgraded those compatibility modules, so Bazel
resolved raw source archives without RCR BUILD overlays. Pinning `tf2`,
`tf2_geometry_msgs`, and later `tf2_ros` at `0.45.10-1.rcr.0` supplied their
real compile/shared targets. The interrupted Boost download reported alongside
the aborted analysis was incidental and disappeared on the corrected build.

No package shared library is required by the current targets. Consequently,
compiled Boost shared pairs were not added preemptively: the geometry library
and its tests link successfully as currently structured. If a later
`cc_shared_library` consumer reports duplicate static Boost ownership, that
consumer will provide the evidence and scope for the shared-pair change.

Verification commands were:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_internal_planning_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=fastdds @autoware_internal_planning_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results \
  @autoware_utils_geometry//... @autoware_utils_tf//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --nocache_test_results \
  @autoware_utils_geometry//... @autoware_utils_tf//...
```

The initial Cyclone interface build analyzed 37 targets and completed 469
actions with 12,471 action-cache hits. The Fast DDS interface build analyzed
the same surface successfully. The final combined Cyclone utility run
completed 198 actions with 11,967 action-cache hits; both tests passed. The
combined Fast DDS run completed 198 actions with 13,538 action-cache hits; both
tests passed. Geometry tests exercise algorithms, message conversions, path
geometry, random polygons, and pose deviation. TF tests currently exercise
listener construction and lookup behavior without publishing live transforms.

Existing ROS C++ and Boost deprecation warnings remained non-fatal. The build
also exposed a Clang warning from a GCC-only diagnostic pragma in
`boost_geometry.hpp`; that source finding and the package's undeclared public
message dependencies are recorded in `UPSTREAM_AWF.md`. `git diff --check`
passed. Pre-commit was not run.

Reproduction notes: integrate the complete prerequisite interface package
before its C++ consumer; derive direct dependencies from public headers and
sources rather than relying on the manifest alone; query the resolved RCR
repository before choosing labels; and verify that each `0.0.0` dependency is
actually upgraded by a concrete root selection. Keep generated-message compile
targets on libraries and their shared counterparts on final executables. Add a
package shared library or compiled Boost shared pairs only after a current
consumer demonstrates that boundary.

Result: the local planning interface surface, geometry library, and TF helper
library build in dependency order and pass their complete existing tests under
both middleware configurations. The lockfile retains only the concrete TF2
family and transitive message-filter releases required by target resolution.

Next: run the complete currently integrated utility set inside the read-only
development image, then use its result to decide whether Phase 3 can close and
the minimal Agnocast-free core slice can begin.

## 2026-11-14: Establish the minimal Agnocast-free core baseline

Goal: start core integration with the smallest runnable package that establishes
node construction, executor, and publish/subscribe behavior without introducing
an Agnocast dependency. Use that behavior gate to define when the independent
Agnocast lane can begin.

The inspected inputs were `autoware_node`'s manifest, CMake file, public
headers, single compiled source, and existing initialization/executor test; the
root module's selected ROS releases; and the Lyrical RCR module metadata. No
`autoware_node` module is published by the selected registry, so the local
package uses version `0.0.0` and is selected explicitly by a root
`local_path_override`. This is different from a ROS dependency declared as
`0.0.0`: the package is intentionally local-only rather than waiting for
another dependency to upgrade it to a registry release.

The minimum library has `src/node.cpp` and the package's public headers. Its
direct compile providers are RCLCPP and RCUtils: the implementation derives
from `rclcpp::Node`, while the public visibility header directly includes
RCUtils macros. The existing test verifies the node's namespace and name while
an executor spins. A Bazel-owned test adds two `autoware::node::Node` instances,
a reliable `std_msgs/String` publisher and subscriber, a single-threaded
executor, and a five-second delivery deadline. Keeping this test under the
package's `bazel/` directory avoids changing the CMake test glob or the
upstream package dependency surface solely for this experiment.

The final test executable depends on the RCLCPP and RCUtils shared providers
plus generated `std_msgs`; its compile target uses the matching generated
message provider. This follows the established executable-boundary convention
and makes the communication behavior sensitive to the selected RMW. Both the
existing and new sources are compiled into one `cc_test` because they share
one GoogleTest main and each test manages its own RCLCPP lifecycle.

Verification forced the complete package surface from a clean caller under
both middleware configurations:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results @autoware_node//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --nocache_test_results \
  @autoware_node//...
```

The final Cyclone DDS run analyzed three targets, completed 148 actions with
5,657 action-cache hits, and passed the test in 0.1 seconds. The Fast DDS
compatibility run analyzed the same three targets, completed 148 actions with
7,228 action-cache hits, and passed the forced test in 0.2 seconds. The
behavior assertion observed the exact `autoware-node` payload;
success therefore covers discovery, publisher/subscriber matching, executor
progress, and message delivery rather than construction alone. Existing ROS
C++ deprecation warnings remained non-fatal. `git diff --check` passed.
Pre-commit was not run.

The integration also demonstrated that `autoware_node`'s public visibility
header uses RCUtils while its manifest declares only RCLCPP. The Bazel target
declares RCUtils directly instead of relying on RCLCPP's transitive closure.
The reproducible source-metadata finding is recorded in `UPSTREAM_AWF.md`; the
source manifest remains unchanged until the normal Autoware build and
dependency lints can validate an upstream-facing fix.

Reproduction notes: choose a core baseline by observable behavior rather than
package count; inspect public headers for direct C-library dependencies; keep
local-only modules explicitly versioned and overridden at the root; and add
generated compile plus shared providers only at the test boundary when the
message exists solely to exercise behavior. Run the same test under each RMW
instead of treating a successful compile as middleware evidence.

Result: `autoware_node` is the complete Agnocast-free core baseline. Do not add
another package merely to enlarge that lane. The appropriate next step is to
start `autoware_agnocast_wrapper` in ROS fallback mode, whose current tests
cover generic pub/sub, message filters, parameter clients, polling subscribers,
and service introspection. After applicable fallback tests pass, introduce a
separately selectable native backend and only the Agnocast library, component,
allocator, kernel, or runtime prerequisites that its current targets expose.
Subsequent dependency-driven core work may proceed alongside that lane without
using fallback-only expansion to postpone the native backend.

## 2026-11-14: Define the Agnocast runtime policy and interface universe

Goal: replace two environment-dependent Agnocast workflows before integrating
the wrapper: compile one artifact that selects its backend at runtime, and make
the bridgeable ROS interface universe available during Bazel analysis instead
of discovering installed interfaces and generating a package after the build.

The inspected inputs were the wrapper's CMake build switch, runtime backend
selection and heap-hook checks; the bridge plugin generator and its CMake
template; every currently integrated interface module; and the pinned RCR's
`rosidl_adapter` and `rosidl_default_generators` Starlark APIs. The RCR declares
each `.msg`, `.srv`, or `.action` with `RosInterfaceInfo`, whose fields are the
source file and ROS package name. Package-level generated C++ targets reach
those rules through Bazel dependency edges.

The runtime policy is one Agnocast-enabled build. Future wrapper targets define
`USE_AGNOCAST_ENABLED` and link Agnocast unconditionally. `.bazelrc` now passes
`ENABLE_AGNOCAST=1` to run and test actions by default and
`--config=no-agnocast` passes `0` to the same artifacts. The Fast DDS selection
is an independent configuration, so combining `--config=fastdds` and
`--config=no-agnocast` remains valid. The environment wiring is committed before
the wrapper target so its first behavior checks exercise the intended policy;
heap-hook and kernel-module behavior remain unvalidated until that enabled
target exists.

`bazel/agnocast/interfaces.bzl` adds `ros_interface_set`. Its aspect walks
rule-level `deps`, records each `RosInterfaceInfo` as
`package/kind/Interface`, carries the source files for the later generator, and
deduplicates both with depsets. The set rule sorts the descriptors, fails during
analysis when a required descriptor or expected count is absent, and writes a
deterministic `.interfaces` manifest. Both the small proof and the product set
are ordinary targets, so a future bridge rule can consume their provider in the
same analysis rather than reading a prior action's directory.

The first analysis failed before loading the rule:

```text
unknown repo 'rosidl_default_generators' requested from the main repository
```

Generated interface modules already use that module transitively, but Bzlmod
repository visibility does not allow the root to load its Starlark API through
another module. Adding a direct root `rosidl_default_generators@0.0.0`
dependency exposed the release selected by `ros_core` without changing the ROS
runtime graph.

The next analysis expected the 39 targets returned by a broad `bazel query`
closure but collected 32. The seven extra query targets came from entire
service/action dependency packages supplied to `default_generators`; they were
not individual type dependencies of either root. Treating those package-wide
edges as bridge inputs would silently enlarge the product whenever generator
implementation dependencies change. The accepted rule follows only interface
and compiled-target `deps`: the proof therefore contains the interfaces owned
by `std_msgs` and `autoware_common_msgs`, plus individually referenced types
such as `builtin_interfaces/msg/Time`. Its frozen count is 32.

The current product set explicitly roots the 11 Phase 2 interface modules plus
`autoware_internal_planning_msgs`. It contains 136 unique descriptors, including
referenced standard ROS types, and analysis checks representative Autoware
message, service, internal-planning, perception, and Geometry types. A package
must be added as a root when all of its interfaces should become bridgeable;
textual BUILD references, package-name patterns, and registry-wide enumeration
are not sources of truth.

Verification commands were:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build //bazel/agnocast:interface_provider_proof
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=no-agnocast \
  //bazel/agnocast:interface_provider_proof
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=fastdds --config=no-agnocast \
  //bazel/agnocast:interface_provider_proof
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build //bazel/agnocast:autoware_interface_universe
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results \
  @autoware_node//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --config=no-agnocast \
  --nocache_test_results @autoware_node//...
```

All commands passed. The proof manifest contained 32 sorted entries and had
SHA-256 `dcd9c64612dddac0a557033ede10d0f8ac80716f91ec5962f42b2c5ef6c3c9e1`
under all three configuration combinations. The product target applied 2,911
aspects, emitted 136 entries, and had SHA-256
`913cea99406eddd9465a1cc4b73853b8521926d6e65b76229427002d131eeeb2`.
The existing `autoware_node` initialization, executor, and reliable loopback
test also passed with `no-agnocast` under Cyclone DDS and Fast DDS. Existing
rules_python implicit-package and ROS C++ deprecation warnings remained
non-fatal. No Agnocast or bridge runtime behavior was exercised. Pre-commit was
not run.

Reproduction notes: analysis cannot enumerate every target in every external
module, so define “all interfaces” as explicit product roots plus their
rule-level type dependencies. Load the ROS interface provider from a direct
root module dependency. Carry both descriptors and source `File` objects in the
provider so later rules can declare hermetic generation actions. Freeze counts
and representative types at the product boundary; do not discover packages by
regex-scanning BUILD text or by reading `AMENT_PREFIX_PATH`.

Next: make the interface set consumable by an `agnocast_bridge_plugins` rule.
Start with one standard message, one Autoware message, and one service; render
their bridge translation units as declared outputs, compile and link them in
the same Bazel invocation, then run an end-to-end bridge before expanding the
generated plugin library to all 136 descriptors.

## 2026-11-14: Add the first Agnocast library prerequisites

Goal: establish the smallest independently testable local dependency layer
required to compile `agnocastlib`, without introducing its kernel module, heap
hook, discovery agent, component executables, or configurator daemon before a
current target exercises them.

The inspected inputs were the manifests, CMake targets, public headers,
compiled sources, and tests for `agnocast_cie_config_msgs` and
`agnocast_cie_thread_configurator`; the direct includes from `agnocastlib`; and
the generated targets selected by the pinned Lyrical release. `agnocastlib`'s
`cie_client_utils.cpp` directly includes the thread configurator client API.
That exported library consists of `util.cpp`, `non_ros_thread_ipc.cpp`, and
`announcement_sources.cpp`. Its headers and sources use RCLCPP and
`CallbackGroupInfo`, but not YAML or the daemon scheduling implementation.

`agnocast_cie_config_msgs` is a local-only `0.0.0` module. It declares
`CallbackGroupInfo` and `ReapplyConfig` with the standard ROS interface rules
and builds the complete 13-target generator surface. Both interfaces contain
only primitive fields, so their rules have no explicit type dependencies. As
with every service, `ros_service` adds the selected release's implicit service
event information.

`agnocast_cie_thread_configurator` is also a local-only `0.0.0` module. Its
current Bazel library contains only the three exported client/announcement
sources and their three public headers. The compile edge uses RCLCPP and the
generated CIE interface target; the final test collects the matching shared
providers. The existing `test_non_ros_thread_ipc` exercises abstract Unix
domain socket addressing, listener/client exchange, malformed and oversized
messages, shutdown, and error behavior without `/dev/agnocast` or
`LD_PRELOAD`.

The package's daemon scheduling core, YAML parsing, `thread_configurator_node`,
`prerun_node`, and their four additional unit suites are not part of this
target. They are not link dependencies of the client library used by
`agnocastlib`. Keeping them behind a later runtime gate avoids selecting
`yaml-cpp` and privileged scheduling behavior before the discovery/component
runtime demonstrates that it needs the daemon.

Verification commands were:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @agnocast_cie_config_msgs//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results \
  @agnocast_cie_thread_configurator//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --nocache_test_results \
  @agnocast_cie_thread_configurator//...
```

The interface build analyzed all 13 targets and completed 37 actions with
3,293 action-cache hits. The Cyclone DDS client-library run analyzed three
targets, completed 155 processes with 2,453 action-cache hits, and passed the
socket behavior test in 0.3 seconds. The Fast DDS run completed 149 processes
with 5,885 action-cache hits and passed in 0.4 seconds. Existing rules_python
implicit-package and ROS C++ deprecation warnings remained non-fatal. Neither
test uses the Agnocast kernel module or heap hook. Pre-commit was not run.

Reproduction notes: inspect the actual source list of an exported library
rather than treating every target in a CMake package as one dependency gate.
Split only at an existing source/target boundary: here CMake already separates
the exported client library from daemon-internal scheduling logic. Generate a
local interface package completely, pair its compile/shared targets at the
executable boundary, and run a behavior test that demonstrates the selected
subset. Record every deferred package target explicitly.

Result: the generated CIE configuration API and the kernel-independent client
transport are available to the next `agnocastlib` target. Next, derive the
library's direct RCR compile/shared pairs, generate its version header as a
declared output, and first compile the complete shared-library source surface.
Add unit-test groups only after the library exposes which Lyrical compatibility
or link boundaries require source changes.

## 2026-11-14: Build the complete Agnocast shared library

Goal: compile the complete exported `agnocastlib` shared-library surface before
selecting tests or runtime executables, so the build itself reveals the direct
ROS dependency boundary and the exact source compatibility needed by Lyrical.
The target deliberately excludes the kernel module, heap hook, discovery
agent, component containers, and thread-configurator daemon because none is a
source or link input of the library being integrated.

The inspected inputs were `agnocastlib/package.xml`, its exported CMake target
and complete source list, public and private headers, the version-header
template, its direct includes, the selected RCR BUILD targets, and the Lyrical
rclcpp, message_filters, TF2, and tracetools APIs exposed by failed compiler
diagnostics. The previously integrated CIE message and client-transport modules
are direct local dependencies. All other dependencies are selected from the
pinned rosdistro graph; the package module declares each one directly rather
than relying on visibility through another module.

The minimum Bazel pattern is a package-local `agnocastlib@0.0.0` module selected
by a root `local_path_override`. A declared genrule reads the tracked
`package.xml` version and expands `agnocast_version.hpp.in` into the generated
header. `agnocast_library` contains the same 51 compiled sources as the
exported CMake library, its public and private headers, the rosdistro local
defines, and C++17 warning settings. Its direct compile dependencies use the
RCR library or generated-interface targets. The final `cc_shared_library`
collects the matching ROS shared providers and produces `libagnocast.so`.

Tracing is a configuration-dependent link boundary, not an unconditional
dependency. Both the compile target and shared-library closure select the RCR
LTTng providers only through `@rosdistro//:tracing_enabled_linux`. This keeps
the default artifact independent of LTTng while making the tracing-enabled
configuration a first-class build gate. The module lockfile update records the
newly reached module and extension inputs so a fresh checkout resolves the same
graph.

The first compilation failed because the source calls `TRACEPOINT`, while
Lyrical's tracetools header exposes `TRACETOOLS_TRACEPOINT`. A compatibility
alias in the existing tracepoint wrapper preserves the source call sites. The
next failures were rclcpp 32 overrides whose shared-pointer parameters changed
from values to const references. A small version-gated parameter macro is used
only for those executor, logging, generic-client, and generic-service override
signatures, preserving the older API form for earlier rclcpp versions.

Further compilation exposed distribution API changes in sequence. Lyrical
message_filters and TF2 install selected headers as `.hpp` rather than the
older `.h` names, so the includes now name the installed headers. The removed
`message_filters::mp_count` helper is replaced locally by a fold-expression
that counts non-null tuple types. rclcpp 32 changed node guard-condition
ownership and added node graph and parameter virtual methods. The node base now
owns one shared guard condition and returns or triggers that same instance; the
new parameter method enables the existing guarded flag. Service endpoint
introspection follows the package's existing unsupported graph-query behavior
and throws explicitly. No anticipated source patch was applied beyond a
compiler error reached by this target.

Verification used a caller environment without a sourced ROS installation:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @agnocastlib//:agnocast
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=fastdds @agnocastlib//:agnocast
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --@rosdistro//:enable_tracing @agnocastlib//:agnocast
git diff --check
```

All three builds produced
`bazel-bin/external/agnocastlib+/libagnocast.so`. The final default Cyclone DDS
confirmation configured 59,005 targets, applied 1,755 aspects, and completed
245 processes with 7,768 action-cache hits and 16 remote-cache hits in 42.876
seconds. The Fast DDS compatibility build loaded 21 packages, configured
65,919 targets, applied 1,777 aspects, and completed 219 processes with 9,287
action-cache hits in 44.595 seconds. The tracing-enabled Cyclone DDS build
completed 351 actions with 7,767 action-cache hits and 99 remote-cache hits in
56.276 seconds. Existing ROS codecvt
deprecations, rules_python implicit-package warnings, Agnocast's missing
`override`, C-linkage return-type warnings, a deprecated ament-index overload,
and existing unused fields remained non-fatal. Targeted clang-format was run on
the changed C++ files, `git diff --check` passed, and pre-commit was not run.

This milestone validates compilation and linking, not Agnocast behavior: a
shared library has no standalone behavior entry point. The next gate is to add
focused, kernel-independent `agnocastlib` unit-test groups and execute the
applicable tests before a wrapper consumes the library. Kernel access,
LD_PRELOAD allocation, discovery, components, and configurator-daemon behavior
remain explicitly deferred until a current executable or test requires them.
After the library test gate, make the product interface set consumable by the
bridge-plugin rule and compile a representative message and service plugin in
the same Bazel invocation.

Reproduction notes: begin from the exported library's actual CMake source list,
not every target in the package; generate configured headers as declared
outputs; declare direct module dependencies even when the release graph already
contains them transitively; pair compile and shared providers only at the final
link boundary; and enable optional tracing during validation so its otherwise
hidden native link requirements are exercised. Treat compiler diagnostics as
the boundary for compatibility work and record every deferred runtime surface.

## 2026-11-14: Clarify scope and dependency ownership

Goal: make three experiment boundaries explicit before additional core and GPU
work makes them implicit policy. The selected Autoware runtime subset does not
require the system-design workflow, Bazel targets must not inherit dependencies
from host provisioning, and interface declarations need a locally runnable
consistency gate even though this experiment has no external CI workflow.

`autoware_system_designer` and its related packages remain in the source
snapshot but are outside the Bazel experiment. Ignoring them is sufficient;
removing or modifying those packages would be unrelated repository churn and
would not answer a build or runtime question for the selected subset.

Apt, pip, rosdep, and the existing Ansible roles remain reference inputs only.
When a current target reaches a dependency that those mechanisms previously
installed, the target must select a pinned Bazel module or repository instead.
This applies to CUDA user-space libraries and tools in the later GPU phase.
Host kernel drivers and the minimal bootstrap tools needed to start Bazel are
platform prerequisites because they cannot be built as ordinary target
dependencies.

For interface packages, a successful generated target does not prove that the
source inventory and BUILD declarations are complete: an unreferenced source
file can be absent from the graph, and a stale dependency label can survive.
Each Bazel-supported interface package must therefore expose a test tagged
`validation` that compares `.msg`, `.srv`, and `.action` sources and their
directly referenced types with the BUILD declarations. The test is invoked
locally through `bazel test --test_tag_filters=validation`; it is not a script
whose execution depends on an external CI service.

This was a documentation-only policy clarification. `git diff --check` was the
applicable validation; no build or runtime behavior changed. The validation
test rule itself will be introduced when interface-package work next resumes,
before accepting another interface definition or declaration change.

## 2026-11-15: Add the kernel-independent Agnocast unit gate

Goal: exercise the complete unit-test surface that can run without the
Agnocast kernel module or heap hook before allowing an Autoware wrapper to
consume `libagnocast.so`. This gate checks behavior hidden by the preceding
compile-only milestone and keeps privileged runtime integration separate.

Prerequisites were the Bazel-built `agnocast_library` compile target and
`libagnocast.so`, the two local CIE modules, and the pinned Lyrical dependency
graph. The inspected test topology was the package's CMake test section and
all sources in its five unit groups: the main mocked library suite, TF2
broadcasters, TF2 buffer, TF2 transform listener, and message filters. Kernel,
heap-hook, discovery-agent, component, and message-filter integration tests
were not selected because their behavior requires runtime facilities outside
this milestone.

The minimum integration pattern keeps those five groups as five Bazel tests.
Each test compiles against `agnocast_library` for headers and C++ providers but
links through the local `cc_shared_library`. A collected dynamic-dependency
closure supplies the direct ROS shared libraries. This boundary is important:
the tests intentionally define mocks for production functions, so embedding
the production archive in the executable produces duplicate definitions,
whereas shared-library linking preserves normal symbol interposition. GTest
and test-only RCL, interface, typesupport, and standard-interface dependencies
are declared directly in the package module and BUILD file.

Observed failures and fixes were:

- The main group did not compile because rclcpp 32 removed
  `get_notify_guard_condition()` from `NodeBaseInterface`. The tests now select
  `get_shared_notify_guard_condition()` for rclcpp 32 and retain the older
  assertion for earlier versions.
- Static linkage reported duplicate publisher, subscription, and bridge
  symbols supplied by the existing mocks. Keeping the compile provider while
  putting the local Agnocast shared library in the test dynamic closure
  restored the same interposition boundary as the CMake shared link.
- The TF2 groups included legacy `.h` spellings that Lyrical installs as
  `.hpp`; only the compiler-reported test includes were updated.
- The message-filter process exited in its first approximate-time case with
  “publish() called exceeds ... borrow_loaned_message()”. Its helper used entry
  ID `-1`, which is now the publisher-side `ENTRY_ID_NOT_ASSIGNED` sentinel,
  despite constructing received messages. Entry ID `0` selects the intended
  subscriber-side release path, which the existing test `ioctl` mock handles.

Verification used an empty caller environment so no sourced ROS installation
could satisfy an undeclared dependency:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results @agnocastlib//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --nocache_test_results @agnocastlib//...
git diff --check
```

Both Bazel invocations executed all five tests successfully. The final
Cyclone DDS run completed six actions in 12.376 seconds; the main group ran in
11.9 seconds. The Fast DDS compatibility run completed 282 actions with 9,552
action-cache hits in 62.517 seconds; the main group ran in 5.0 seconds. The
message-filter group contains 57 cases and passed in 0.1 seconds in both
configurations. Existing rules_python implicit-package, ROS codecvt,
missing-`override`, and C-linkage return-type warnings remained non-fatal.
Targeted clang-format was applied to changed tests, `git diff --check` passed,
and pre-commit was not run.

Reproduction notes: copy the upstream test grouping rather than coalescing
tests with different dependency surfaces; distinguish compile providers from
the shared-library link boundary when mocks interpose production symbols; and
use source-version guards only where the selected distribution changes the
tested public API. A mocked kernel call does not make every test
kernel-independent: admit only groups whose behavior and fixtures can run
without device, allocator-preload, or daemon state.

Remaining limitation: this milestone validates library behavior under mocks,
not an enabled Agnocast transport. Next, integrate
`autoware_agnocast_wrapper` with `USE_AGNOCAST_ENABLED` unconditionally and
first run its applicable communication behavior under `--config=no-agnocast`.
Introduce the heap hook and kernel/runtime prerequisites only when the default
Agnocast behavior gate reaches them; bridge-plugin generation remains the
parallel analysis-time lane.

## 2026-11-15: Validate the Agnocast wrapper fallback backend

Goal: make the first Autoware consumer of `libagnocast.so` compile with
Agnocast support unconditionally, then prove that the same artifact selects
and runs its RCLCPP fallback when `ENABLE_AGNOCAST=0`. This isolates wrapper
API and link integration before adding the privileged enabled-transport
runtime.

Prerequisites were the Bazel-built Agnocast shared library and its passing
kernel-independent unit gate, the local `autoware_utils_rclcpp` module, the
pinned Lyrical ROS graph, and `.bazelrc` runtime selection through
`--config=no-agnocast`. The inspected inputs were the wrapper manifest, CMake
library and test definitions, its two compiled sources, all public headers,
and the five existing C++ test-case files. The Python launch-action test was
not selected: it validates installed launch packaging rather than the C++
wrapper and introduces a separate Python/launch dependency surface.

The minimum integration is a package-local `autoware_agnocast_wrapper@1.10.0`
module selected by a root `local_path_override`. The compile library contains
`src/node.cpp`, `src/runtime.cpp`, and all public headers; it publishes
`USE_AGNOCAST_ENABLED` unconditionally and depends on the Agnocast compile
provider. A `cc_shared_library` links it with `libagnocast.so` and the direct
ROS shared providers. The test compiles the existing CMake C++ suite as one
binary and uses the same wrapper shared-library boundary. `std_msgs` remains a
test-only dependency.

This arrangement separates two independent choices. Bazel always compiles the
two backend implementations into the wrapper, so downstream source sees one
stable API. The test environment set by `--config=no-agnocast` supplies
`ENABLE_AGNOCAST=0`, causing `use_agnocast()` and each wrapper object to select
the RCLCPP path at runtime. No alternate library or source configuration is
built for fallback testing.

Observed failures and fixes were:

- The first test compile could not find `heaphook_probe.hpp`. CMake inherited
  the test source directory, while Bazel requires the header to be a declared
  test input; adding it to the test sources fixed the hermetic input boundary.
- Lyrical installs the four wrapper-used message-filter headers with `.hpp`
  names rather than `.h`, so the compiler-reported public includes were
  updated.
- Lyrical's `message_filters::Subscriber` takes only the message type; node
  types are accepted by constructor and `subscribe()` overloads. Removing the
  obsolete `rclcpp::Node` template argument retained the wrapper's runtime
  dispatch while matching the selected public API.

Verification used an empty caller environment:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results \
  @autoware_agnocast_wrapper//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --config=no-agnocast \
  --nocache_test_results @autoware_agnocast_wrapper//...
git diff --check
```

Both invocations ran 36 tests from eight suites and passed every test. The
Cyclone DDS run completed 235 processes with 7,839 action-cache hits in 47.075
seconds; the test ran in 0.3 seconds. The Fast DDS compatibility run completed
234 processes with 9,411 action-cache hits in 51.400 seconds; the test ran in
0.4 seconds. Existing rules_python implicit-package, ROS codecvt,
missing-`override`, C-linkage return-type, unused capture, and test-only unused
variable warnings remained non-fatal. Targeted clang-format was applied to the
changed C++ header, `git diff --check` passed, and pre-commit was not run.

Reproduction notes: do not model runtime backend selection with two Bazel
libraries. Publish the Agnocast compile definition and dependency from one
wrapper target, then vary only the test/run environment. Keep compile
providers and shared providers paired at the final executable boundary, and
declare test helper headers even when a legacy build system found them through
an ambient include directory. Exercise both middleware configurations before
adding privileged runtime dependencies.

Remaining limitations: this result proves the DDS fallback of an
Agnocast-capable artifact, not zero-copy transport. The heap hook,
`/dev/agnocast`, kernel module, discovery agent, component containers, and
Python launch packaging remain absent. Next, run the same wrapper behavior
gate with the default `ENABLE_AGNOCAST=1`; integrate the heap hook and kernel
runtime in the smallest order exposed by that attempt. The bridge-plugin rule
remains an independent analysis-time lane.

## 2026-11-15: Validate the heap hook and enabled wrapper C++ suite

Purpose: prepare the allocator runtime required by the enabled wrapper suite
and run that suite with the actual shared library preloaded. Prerequisites
were the integrated wrapper, its passing DDS fallback suites, the Bazel-built
`libagnocast.so`, and the RCR Rust toolchain. Inspected inputs were the heap
hook's Cargo manifest, build script, Rust sources and stub, RCR's Rust exports
and rules_rs toolchain declaration, and the wrapper's heap-hook fixture probe.

The initial enabled invocation selected 36 cases but passed only the backend
selection case and skipped 35 cases because the heap hook was absent. That
established the allocator as the next input; a successful Bazel exit with
those skips does not validate communication.

The heap hook is a Rust 2021 `cdylib` at version 2.4.1. Add a package BUILD
with `rust_shared_library`, its two Rust source files, and direct `libc` and
`rlsf` dependencies. Resolve the tracked Cargo manifest through rules_rs
`crate.from_cargo`, commit its Cargo.lock with checksums, and reuse the RCR
Rust 1.93.0 toolchain. The accepted lock selects libc 0.2.191 and rlsf 0.2.3.
The remaining crate closure is cfg-if, const-default, and rustversion.

The Cargo build script compiles an Agnocast stub with ambient rustc and probes
host glibc with ldd. Bazel instead supplies a `cc_import` of the real
`@agnocastlib//:agnocast` shared library and sets `CARGO_PKG_VERSION` explicitly.
`--cfg=glibc_pre_2_35` selects the source's existing allocator-bootstrap path
for the workspace's glibc 2.34 floor. This preserves deterministic target
configuration without running the Cargo build script or linking its stub.

Add a `sh_binary` launcher with the heap-hook target in `data`. Resolve the
library from runfiles and set `LD_PRELOAD` immediately before `exec` of the
test. Applying preload to the launcher shell itself would initialize Agnocast
before the test's library closure is available. Select the launcher through
`--run_under` so ordinary fallback runs retain their existing runtime setup.

Commands from an unsourced caller environment:

```bash
cargo generate-lockfile --manifest-path \
  src/core/agnocast/agnocast_heaphook/Cargo.toml
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build //src/core/agnocast/agnocast_heaphook:agnocast_heaphook
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_heaphook \
  --test_env=ENABLE_AGNOCAST=1 --nocache_test_results \
  @autoware_agnocast_wrapper//:unit_tests
```

Cargo is used only to produce the initial dependency lock; Bazel performs the
library build using declared crate inputs and its registered toolchain.
Subsequent builds consume the committed lock without resolving new versions.
The library build succeeded in 26.671 seconds with 22 processes and 8,915
action-cache hits. `readelf -d` confirms `libagnocast.so` as a NEEDED library.

The enabled suite succeeds after correcting the existing test setup. Without
these fixes, four generic pub/sub cases fail:

- `GenericPubSubMethod1Test.MacroRoundTrip` and `MacroOnNodeRoundTrip` time out
  after ten seconds waiting for their callbacks. Both tests instantiate an
  `rclcpp::executors::SingleThreadedExecutor` although their wrapper endpoints
  select Agnocast. Use `SingleThreadedAgnocastExecutor::spin()` for those
  mixed ROS/Agnocast nodes. Inherited RCLCPP `spin_some()` does not dispatch
  Agnocast callbacks.
- `GenericPubSubMethod2Test.NodeMemberRoundTrip` and
  `NodeMemberDepthAndOptionsOverload` throw because they call
  `get_rclcpp_node()` while the wrapper owns an Agnocast node. The test executor
  setup now passes the common node-base interface to
  `AgnocastOnlySingleThreadedExecutor` for Agnocast-owned nodes and to the
  RCLCPP executor in fallback mode.

The test environment initializes both ROS and Agnocast contexts when enabled:
Method 1 uses ROS nodes, while the Agnocast-only executor used by Method 2
requires the Agnocast signal handler. Without the latter initialization,
executor construction exits with “signal handler is not installed”. A bounded
test helper runs each selected executor's `spin()` on a worker, waits for the
message, cancels, and joins before nodes are destroyed. Repeated cancellation
handles RCLCPP's cancel-before-spin race. Payload assertions are unchanged.

Kernel-backed tests require `--strategy=TestRunner=local`. PID-isolated Bazel
tests reuse small process IDs while sharing `/dev/shm`; Agnocast names shared
memory by PID. A subsequent run can fail with `shm_open failed: File exists`
before the test harness. Local TestRunner execution uses unique host PIDs
while compile actions retain sandboxing. The execution host's already loaded
kernel module and accessible `/dev/agnocast` are prerequisites; this experiment
did not install or load a module. A sandbox device listing alone does not prove
what the actual test process can access. Do not remove arbitrary shared-memory
objects to make a test pass.

The discovery agent was absent and startup emitted an observability warning.
The suite continued through parameter, polling, and introspection tests; the
warning is not evidence that discovery-agent installation fixes these four
failures. The fixes affect test context and executor setup only.

The heap hook's existing allocator tests run separately with:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results \
  //src/core/agnocast/agnocast_heaphook:unit_tests
```

All 26 tests passed with no ignored cases. The first unit attempt selected the
production `glibc_pre_2_35` flag and panicked before the harness: its startup
hook unwraps a resolver that the source excludes under `cfg(test)`. That
invocation was interrupted. The test target therefore uses its existing
default bootstrap on Ubuntu 24.04; the production shared library retains the
legacy path for the ABI floor. Allocator tests cover alignment, zero and
excessive sizes, realloc, zero-initialized calloc, and TLSF offset changes.
They do not validate the legacy startup path; the preloaded C++ suite exercises
that production path. Execution on glibc 2.34 itself remains untested.

Verification also runs the whole C++ suite under fallback and Tier 2 middleware
configurations, with the same empty caller environment:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results \
  @autoware_agnocast_wrapper//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --config=no-agnocast \
  --nocache_test_results @autoware_agnocast_wrapper//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_heaphook \
  --test_env=ENABLE_AGNOCAST=1 --nocache_test_results \
  @autoware_agnocast_wrapper//...
```

All four configurations passed all 36 cases without skips. Cyclone DDS passed
in 2.2 seconds with Agnocast enabled and 0.3 seconds with fallback. Fast DDS
passed in 0.5 seconds with Agnocast enabled and 0.4 seconds with fallback.
Targeted clang-format,
`bash -n bazel/agnocast/with_heaphook.sh`, and `git diff --check` are the static
checks; pre-commit was not run. AGENTS.md now requires completion against the
whole declared module test gate and one consolidated journal entry per task.

This completes the C++ runtime test gate.
Python launch packaging remains the separate scope described in the plan;
this gate does not claim complete wrapper package adoption. The next runtime
work is a Bazel-generated representative bridge plugin and its message/service
behavior, driven by the existing analysis-time interface provider.

## 2026-11-15: Record AWF feedback and Lyrical patch alignment

Purpose: make the verified wrapper test fix reviewable upstream and keep
independent Lyrical compatibility work aligned with AWF's transition. Inspected
inputs were AWF autoware issue #7404 and its comments, core issue #1538, linked
toolchain/context issues, and repository-scoped Lyrical and wrapper PR searches.
UPSTREAM_AWF.md records dated associations, their exact overlap, and remaining
upstream validation. The wrapper source patch is `1d0cd537`; its existing
four-configuration, 36-case C++ results remain the runtime evidence.
Each finding now includes a Markdown association checkbox. A checked item
requires a linked issue or PR for that specific finding; related links alone
are insufficient. Unchecked entries distinguish a completed search with no
match from a pending upstream review.

The review found related feature/context PRs and a draft CMake transition PR,
but no matching executor-test fix. Open Agnocast PR #1517 changes context
ownership and must be reassessed before combining it with explicit test
initialization. AGENTS.md and PLAN.md now require independent local validation,
comparison with relevant AWF work, and recorded patch/PR associations without
pre-applying unrelated fixes. Follow the links and dated search scope in
UPSTREAM_AWF.md to repeat this review; upstream status must be checked again
before submission. No upstream comment or PR was created.

This task changes documentation only. `git diff --check` is the applicable
validation; no runtime checks were rerun and pre-commit was not run.

## 2026-11-15: Build root-owned typed Agnocast plugins from the interface graph

Purpose and scope: expose the interfaces selected by the top-level build during
analysis, instantiate the upstream bridge implementations directly in that
graph, and run the complete eight-case bridge suite with the production heap
hook and Cyclone DDS. The module boundary also includes the heap hook's Cargo
setup. Recheck all five existing Agnocast unit groups, allocator tests,
wrapper C++ tests, and interface consumers. Discovery-agent installation,
component containers, and the 15 additional upstream Agnocast integration
groups are separate adoption gates, not covered by this milestone.

Prerequisites and inspected inputs: the pinned RCR Lyrical/rclcpp 32/LLVM
configuration, package-local generated interface targets, RosInterfaceInfo,
the upstream ros2agnocast verb/templates/helpers, bridge factory API, and
existing heap-hook and wrapper test fixtures. Runtime checks require a
compatible loaded Agnocast kernel module and access to /dev/agnocast. Use the
ignored user.bazelrc resource limits and run heavy builds sequentially. No
host ROS installation is sourced and no apt/pip/rosdep target dependencies are
used. Fast DDS checks require approval at major milestones; none were run
for this final gate.

Final integration pattern:

1. Put interfaces.bzl in bazel/interfaces and define ros_interface_set with a
   `roots` label-list attribute. Apply an aspect following `deps`, collect
   each encountered RosInterfaceInfo's package/kind/name and source file,
   combine them in depsets, and write a sorted manifest. Return the public
   RosInterfaceSetInfo metadata for bridges, loggers, or other consumers.
   Roots are build targets, not module names: individual interfaces, aggregate
   :cc targets, or applications work through their dependency paths. Aggregate
   libraries can include more types than the source code actually uses.
2. Declare only //bazel/interfaces:all_interfaces. List 15 concrete :cc target
   labels directly in its BUILD file: agnocast_cie_config_msgs, the 12 local
   Autoware interface packages, std_msgs, and std_srvs. These selected product
   roots and their declared dependencies are the universe, not every package
   available in the module registry. Developers register needed interfaces
   with ros_message/ros_service and default_generators(deps=[...]); undeclared
   files remain outside the graph. There is no source-directory completeness
   validator, inventory repository, packages.bzl list, or separate proof set.
3. Keep product plugin assembly in bazel/agnocast/plugins.bzl and instantiate
   //bazel/agnocast:plugins in the root module. ros2agnocast is a minimal local
   module exporting its existing C++ templates and generic helper files. It
   does not depend back on root labels. A separate autoware_bazel source
   repository is deferred; the existing root module name is only the
   metarepository's workspace identity.
4. Iterate interface descriptors during analysis, declare one C++ translation
   unit per descriptor, and expand the upstream templates with native
   ctx.actions.expand_template. Supply the message/service descriptor, C++
   namespace type, ROS snake-case header path, and flattened symbol suffix.
   There is no ROS verb invocation, ament-index discovery, Python renderer,
   generated CMake package, or second build. Keep generic_functions.cpp/.hpp
   beside the generated sources as declared inputs.
5. Match the verb's modern-ROS service handling: synthesize one
   package/srv/Service_Event pub/sub type per service and deduplicate. Its
   header is the service's own header, not a separate event header. Test
   String, ResponseStatus, GNSSFix, UInt16 and SetBool_Event naming during
   analysis. Action interfaces and templates with EmPy control flow remain
   unsupported; the current C++ templates use substitutions only.
6. Compile generated sources with explicit interface :cc and Agnocast/RCLCPP
   library dependencies; collect matching :cc_shared and runtime providers
   before cc_shared_library links libagnocast_bridge_plugins.so. The plugin
   BUILD file declares the compile/shared targets directly. Outputs belong
   to the root plugin target; generated C++ is not checked into source.
7. Give bridge_plugins_test that library as declared data and a rootpath
   argument. dlopen the exact file, resolve typed factories with dlsym, and
   check standard/local messages, SetBool requests/responses, and service-event
   payloads in both directions. Message publishers run as separate processes
   because bridge subscriptions ignore local publications. Explicitly add
   returned callback groups to the mixed executor; run spin() in a thread,
   then cancel/join before destroying endpoints.
8. Make agnocast_heaphook its own component module. Put crate.from_cargo beside
   Cargo.toml/Cargo.lock; name the extension proxy `crate` and retain the
   generated repository name "heaphook_crates". Keep the real Agnocast shared
   library import and glibc 2.34 bootstrap selection. The root preload launcher
   uses the Bash runfiles library and repository mapping, not a source checkout
   path. Pass the empty canonical source-repository name for the main module;
   `_main` is its runfiles directory name, not its mapping key.

Diagnosed setup issues: production preload must not be applied to unit suites
that interpose kernel/allocator calls with mocks. Run those groups without
the heap hook. Kernel-backed behavior uses --strategy=TestRunner=local because
reused sandbox PIDs collide with host PID-named shared memory. Source-tree
relocation probes under /tmp need --sandbox_add_mount_pair for that directory,
otherwise Python runfiles can point into a masked host /tmp. These are test
environment constraints, not reasons to skip required tests. Ordinary component
module boundaries resolve through labels and source overrides.

Final verification commands, in a shell with no sourced ROS environment:

```bash
clean_bazel() {
  env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" bazel "$@"
}
clean_bazel test --config=no-agnocast --nocache_test_results --test_output=errors \
  @agnocastlib//... @agnocast_heaphook//... @autoware_agnocast_wrapper//... \
  //bazel/tests/... //bazel/agnocast:template_tests \
  //bazel/interfaces:all_interfaces //bazel/agnocast:plugins
clean_bazel test --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_heaphook \
  --nocache_test_results --test_output=errors \
  @autoware_agnocast_wrapper//... //bazel/agnocast:bridge_plugins_test
packages=(agnocast_cie_config_msgs autoware_common_msgs autoware_control_msgs
  autoware_internal_planning_msgs autoware_localization_msgs autoware_map_msgs
  autoware_perception_msgs autoware_planning_msgs autoware_sensing_msgs
  autoware_simulation_msgs autoware_system_msgs autoware_v2x_msgs
  autoware_vehicle_msgs std_msgs std_srvs)
targets=()
for package in "${packages[@]}"; do targets+=("@${package}//..."); done
clean_bazel build "${targets[@]}"
clean_bazel aquery \
  'mnemonic("TemplateExpand", //bazel/agnocast:plugins_sources)' --output=text
git diff --check
```

Coverage checks: the sorted manifest contains 169 interfaces, including 21
services; expansion produces 190 typed translation units. All were compared
byte-for-byte with the upstream template substitutions using the verb's
CamelCase-to-snake-case algorithm, including the service-event header rule.
`nm -D --defined-only bazel-bin/bazel/agnocast/libagnocast_bridge_plugins.so`
contains both typed factories per instantiated type: 380 expected symbols.
The generic helper sources match the upstream files exactly. The native action
query shows root-owned TemplateExpand actions with upstream templates as inputs.

The 13-target unit/consumer gate passes: five Agnocast groups (439 cases),
allocator tests (26), wrapper fallback (37), five interface consumer tests,
and template naming checks (five assertions). Kernel-backed checks pass the
complete wrapper suite (37) and all eight bridge cases without skips. All 317
generated-interface targets across the 15 selected packages build successfully.
The wrapper's additional case comes from the source fix recorded below.

The broad generation gate first hit a timeout fetching the pinned Ogre archive
through GitHub's archive URL. Fetching the same version through codeload and
passing it through --distdir resolved the download; Bazel verified the archive
against its existing checksum. No dependency version or source patch changed:

```bash
mkdir -p /tmp/autoware-bazel-distdir
curl -fL https://codeload.github.com/OGRECave/ogre/tar.gz/refs/tags/v1.12.10 \
  -o /tmp/autoware-bazel-distdir/v1.12.10.tar.gz
clean_bazel build --distdir=/tmp/autoware-bazel-distdir "${targets[@]}"
```

DEVELOPING.md now describes explicit interface registration, mixed packages,
collector reachability, consumer/link dependencies, and supported day-to-day
commands. PLAN.md records the final ownership and scope; it does not require
the deleted source module or validator. Earlier committed milestones remain
history; this entry consolidates the design iterations into the
final reproducible pattern. No pre-commit run or Fast DDS check was performed.
The next runtime gate is normal plugin auto-selection/discovery, not another
interface collector proof.

## 2026-11-15: Repair the wrapper's Lyrical message-filter subscription path

Source fix and regression commit: `b5d19a6c`, separate from Bazel build support.

Purpose: fix a real public API compile failure missed by a default-construction
test. On the pinned Lyrical message_filters API, instantiating
Subscriber<PoseStamped>::subscribe(node, topic, rmw_qos_profile_sensor_data)
fails because the ROS branch forwards an rmw profile to an overload requiring
rclcpp::QoS. A syntax-only probe using the wrapper test's actual Bazel compile
arguments failed; the same header text with explicit conversion passed.

Use the existing profile-preserving detail::to_qos helper already in node.hpp,
guarded by rclcpp major 32. Preserve the pre-32 call form and the Agnocast
raw-profile path. Do not duplicate conversion logic or normalize the profile
through QoSInitialization::from_rmw. The new regression creates a subscriber
through its constructor, checks topic/history/depth/reliability/durability,
resubscribes with a different topic/depth, and verifies unsubscribe releases
the endpoint. It runs against the actual selected backend, with the heap hook
required for the enabled gate.

Verification: the complete wrapper C++ suite passes all 37 cases without skips
with both runtime selections, using the two clean_bazel test commands above.
The regression test would instantiate the failing method before the fix; it
now checks real endpoint creation rather than only class construction. Targeted
clang-format and git diff --check pass. Fast DDS, older distributions, and
ordinary CMake/colcon builds were not run. The rclcpp guard preserves the older
source path but is not a claim of tested Humble/Jazzy compatibility.

Reviewed upstream main and the actual patch of core PR #1257 on 2026-11-15.
Main still forwards the raw profile and retains the old Subscriber template
arity. PR #1257 changes the non-Agnocast API shape, not this enabled subscription
path. No matching Lyrical QoS repair was found in the documented searches.
UPSTREAM_AWF.md records exact inspected blob/head hashes, related trackers,
the independent reproducer, regression, and remaining upstream validation.
This is wrapper Lyrical compatibility, not a transport behavior modification.

Coverage limitation: the 439 Agnocast unit cases do not replace the 15 upstream
integration groups not yet declared in our BUILD file. Discovery-agent warn()
calls also need review against rclpy's warning() API when that module is adopted.
Those are future module gates; no speculative source patches were applied.

## 2026-11-15: Phase 4.1 — adopt the complete Agnocast library test inventory

Purpose: expose the library's existing integration suite in Bazel before
extending automatic bridge activation. Inspect `agnocastlib/CMakeLists.txt`
and `test/integration/` to inventory all 15 integration groups alongside the
five already-supported unit groups. This task changes test/build integration,
not executor behavior. Phase 0–4 retain their existing numbers; subsequent
Agnocast tasks are 4.2–4.4, and later work continues at Phase 5.

Prerequisites: the pinned Lyrical/Cyclone workspace, the local heap-hook module,
and a compatible loaded Agnocast kernel module exposing `/dev/agnocast`.
No installed ROS environment or externally installed test dependency is used.
Keep the test source groups aligned with CMake rather than replacing them with
smaller smoke tests. Declare each group's private integration headers and its
ROS compile dependencies, and reuse the existing shared dependency boundary
so mocks interpose the production library's symbols.

The heap-hook component exports `tests.bzl:heaphook_test`. It builds a test-only
C++ binary and wraps its final execution with the built allocator in
LD_PRELOAD, resolved through runfiles. Preloading only after the shell starts
avoids injecting the allocator into the launcher. The rule merges the binary
and preload libraries' runfiles, preserves arguments and exit status, uses
thread-safe death tests, and disables sandbox/remote execution so PID-named
shared memory matches the host kernel's process namespace. The library's
module declares the hook dependency; neither component refers to root labels.

Preserve the suite's distinct runtime boundaries: executor integration uses
its existing ioctl mocks and runs without the heap hook; allocator integration
preloads its existing initialize/shutdown mock before the real hook and does
not need the kernel; the other 13 integration groups use the real hook and
kernel. Build the mock as its own shared library to preserve ELF interposition
order. Do not globally preload the allocator into Bazel or its build tools.

Verification, from the repository root:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @agnocastlib//...
git diff --check
```

Result: all 20 test targets pass in 107.704 seconds, including all 15 newly
registered integration groups. The current targets' XML contains 672 cases:
667 pass and five report the source suite's explicit existing skips. These
five cases concern adding timers after a callback-isolated executor starts;
`should_skip_entity_added_after_spawn()` documents unsupported behavior and
references upstream issue #1263. They are not missing-dependency skips and no
new filters or skips were added. A green Bazel target therefore does not imply
that this unsupported executor behavior works. Do not count stale XML for
previous target names when totaling cases.

No source patch was needed for this adoption gate. No pre-commit or Fast DDS
check ran. Phase 4 remains incomplete: normal bridge selection/discovery,
component and tool packaging, wrapper launch support, and end-to-end runtime
gates follow in 4.2–4.4. The upstream executor limitation remains visible in
the adopted suite rather than being silently treated as covered behavior.

Phase 8 goal clarification before committing: target JetPack 7.2.1 software
build compatibility, with physical-device validation deferred. NVIDIA's
[release page](https://developer.nvidia.com/embedded/jetpack/downloads) and
[archive](https://developer.nvidia.com/embedded/jetpack-archive), inspected on
2026-11-15, list Jetson Thor support for JetPack 7.2.1 / Jetson Linux 39.2.1.
The release's Ubuntu 24.04, CUDA 13.2.2, and TensorRT 10.16.2 versions define a
planning baseline only. Phase 8 must pin and validate its actual user-space
inputs; cross-compilation and dependency closure do not prove GPU execution
or device behavior. No SDK download, cross-build, GPU, or hardware check was
performed for this documentation change. Existing Phase 0–4 numbers remain.

## 2026-11-15: Phase 4.2 — production bridge activation

Purpose and acceptance: replace direct factory-only checks with the production
BridgeLoader, then verify normal endpoints automatically activate both bridge
directions and the auto-started discovery agent reports a typed endpoint.
Retain message, service-event, and service payload checks. Adopt the complete
five-file discovery-agent pytest suite and its ioctl/message dependencies.
This module gate passes; the complete runtime closure is recorded in Phase 4.4.

Inspected inputs: `agnocastlib`'s bridge loader, manager and process startup;
the discovery agent's package manifest, scripts, setup metadata and tests;
the ioctl wrapper's CMake source list; and RCR Python/ament helpers. The root
assembles plugins and runtime resources; each source package owns its module
and declares no metarepository label. Discovery messages follow the existing
message/module pattern, with explicit local edges Endpoint -> Topic -> State.
The ioctl wrapper is one C++ library/shared-library pair. Reuse RCR's pytest,
Python rules, pinned PyYAML wheel and ament package/index rules.

Diagnosed failures:

- Importing `rcl_interfaces.msg.Parameter` failed before test collection.
  Generated messages are namespace packages and RCR injects its import hook
  through `sitecustomize.py`. Disabling implicit `__init__.py` creation on the
  final test alone was insufficient: transitive executables' runfiles still
  supplied empty initializers. `--incompatible_default_to_explicit_init_py`
  removes those across the graph; keep the setting in `.bazelrc`.
- The ioctl wrapper failed on undeclared `errno`/`ENOENT`. Add `<cerrno>` to
  node_info.cpp, topic_info.cpp and topic_list.cpp, which use those names.
  Other source files already include it. This is header self-sufficiency,
  not a runtime logic change.
- After message imports worked, the complete suite collected 147 cases and
  reported 146 passes plus one AttributeError: RcutilsLogger has no `warn`.
  Use `warning()` consistently in the agent, bridge decider and registry;
  update existing mock expectations. The existing real-logger failure-path
  test exposes this API issue without inventing another logger abstraction.
  All 147 cases now pass, and all ioctl library targets build.
- Bazel Python mains must end in `.py`, while upstream's installed wrappers
  have extensionless names. Copy the existing scripts to declared `.py`
  outputs and use them as py_binary mains, retaining the public executable
  names and their existing alias behavior.

Commands used (clean caller environment, no sourced ROS):

```bash
clean_bazel() {
  env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" bazel "$@"
}
clean_bazel test @ros2agnocast_discovery_agent//:tests \
  @agnocast_ioctl_wrapper//... --test_output=errors
clean_bazel test --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime --nocache_test_results \
  --test_output=errors //bazel/agnocast:bridge_plugins_test
```

The second command passes all 11 cases (7.3 seconds in the final run): eight loader cases,
two automatic bridge directions, and one actual agent gossip case. No case
is skipped. Runtime assembly supplies ament
resource symlinks, the agent executables, the ioctl library, typed plugins and
the heap hook. Set plugin search, ament prefix and library search before
preloading the final process: the allocator can fork runtime daemons before
`main()`. Merely setting the plugin environment in the test body is too late
for the manager fork. Preserve the host PID namespace for kernel-backed tests.
No Fast DDS or pre-commit check has run.

Runtime registration diagnosis: ament_package installs executables under
lib/<package> in its install metadata, but the reused ament_index runfiles
layout places them in bin/. The agent's automatic startup could find its
package marker but not its required executable path. Register the existing
py_binary outputs under lib/ros2agnocast_discovery_agent with
register_ament_files and include that registration in ament_package data.
The full runtime gate then passes, including agent startup and gossip.
Keep the runtime payload in cc_test data, not the --run_under tool's data:
otherwise Bazel builds it again in the execution configuration, where the
fixed target RMW selection does not constrain its dependency closure.

## 2026-11-15–16: Phase 4.3 — component, configurator and Python runtime

Purpose and acceptance: build the current Agnocast component containers and
configurator executables, expose the ROS CLI extensions and wrapper launch
action, and adopt each package's complete existing tests. Follow with the
existing functional launches; compilation alone does not close Phase 4.

Inspected the component, sample and end-to-end packages' CMake registration
lists, upstream standalone-main templates, configurator C++ and Cargo inputs,
Python setup.py/setup.cfg files, and RCR's component/launch/Python helpers.
Keep source repositories independent: their modules build their own libraries,
components and executables; the root assembles runtime resources and selects
product-level launch scenarios. Standard RCR component registration creates
records and ROS executables. The Agnocast component package's small adapter
expands its existing main templates for the selected Agnocast executor while
reusing RCR's component records and shared-dependency collector.

Configurator pattern: retain the exported client library, add the four-source
scheduling/YAML core and prerun implementation library, and link both daemon
entry points. Match CMake's real shared boundary with a separate
libagnocast_thread_configurator.so target. A consumer that links libagnocast
and directly uses the configurator client otherwise encounters Bazel's
"linked statically ... but not exported" error. Give libagnocast the real
configurator shared dependency instead of exporting another component's
static implementation from it. Reuse pinned yaml-cpp 0.9.0. The Rust client
owns a Cargo extension for its tracked lockfile and builds its library, six
wire-format cases and real Unix-datagram transport case with the RCR toolchain.

Python pattern: reuse the source package's setup.py under a hermetic
setuptools build tool to generate CLI egg-info and entry points. Declare the
metadata as py_library data; expose a small ros2 entry point with the full
extension dependency closure. The initial ros2bag 0.33.3-1.rcr.1 overlay has
no Python library target. Inspecting the published 0.33.4-1.rcr.0 overlay
confirmed that it supplies the missing runtime; pin that version and validate
it instead of adding a local patch or mocking away the recording dependency.
The wrapper's Python target carries its setup.cfg-equivalent frontend entry
point and package metadata, and its ament package keeps setup.cfg for install
metadata. Keep these entry-point declarations consistent when editing them.

Verification commands:

```bash
clean_bazel test @agnocast_cie_thread_configurator//... \
  @agnocast_components//... --test_output=errors --local_test_jobs=1
clean_bazel test @ros2agnocast//:tests --test_output=errors
clean_bazel test @autoware_agnocast_wrapper//:launch_tests \
  @agnocast_sample_application//... @agnocast_e2e_test//... --test_output=errors
```

Results: five configurator C++ groups pass 137 cases; the Rust wire suite
passes six and the transport test passes one, without skips. All three
current component containers build. All 95 CLI cases (including the two version-path regressions below) and 66 wrapper launch
cases pass. The sample/end-to-end fixture build reports 104 non-test targets
plus the wrapper launch target. Bazel's fallback XML represents each pytest
invocation as one case; obtain the actual pytest totals from test.log.

Runtime resources must register executable paths under lib/<package>, not
only the default runfiles bin/ aliases. Root ament_index targets also need an
explicit nonempty export_name: Bazel's root canonical repository name is
empty, so omitting it creates a packages/ marker that shadows the whole
resource directory. After naming the product index, the existing CIE
component-container launch passes in 9.4 seconds.

A Python launcher can exec multiple bootstrap stages with the same PID.
Preloading the allocator before those execs registers the process twice and
fails AGNOCAST_ADD_PROCESS_CMD with EINVAL. The root runtime wrapper therefore
allows Python launch drivers to start without preload; a tiny launch-source
adapter sets LD_PRELOAD after interpreter startup, then loads the unmodified
scenario in the same module namespace. Its launched native nodes receive the
hook. Reuse RCR launch_test for environment setup, process cleanup and JUnit
output. The C++ bridge test still preloads its final executable directly.

Functional gate result (2026-11-16): all seven scenarios pass together, 38
cases with no skips. Add the CIE config package's :py provider explicitly for
reapply/wildcard's service imports; package resource metadata is not a Python
compile/import dependency. The multi-domain source failure is diagnosed as
follows: rclcpp 32 rejects a domain node whose context differs from its
executor. Introduce one shared spin_domain_nodes helper for all three entry
point paths. Give each domain node an executor with its own context, then
spin_some serially on the original thread with a 1 ms idle interval. Retain
ordinary blocking spin for one domain. This preserves the configurator's
existing serial callback-state assumptions instead of adding executor threads.
The full five-group C++ and two-target Rust suite still passes after the fix.
UPSTREAM_AWF.md records the upstream main blob, searches and comparison.

```bash
clean_bazel test @agnocast_cie_thread_configurator//... \
  --nocache_test_results --test_output=errors --local_test_jobs=1
clean_bazel test --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime --nocache_test_results \
  --test_output=errors --local_test_jobs=1 \
  //bazel/agnocast:launch_agnocast_component_container_cie \
  //bazel/agnocast:launch_agnocast_callback_isolated_executor \
  //bazel/agnocast:launch_multi_domain_cie_talker \
  //bazel/agnocast:launch_thread_configurator_restart \
  //bazel/agnocast:launch_thread_configurator_reapply \
  //bazel/agnocast:launch_thread_configurator_wildcard \
  //bazel/agnocast:launch_thread_configurator_precedence
```

## 2026-11-16: Phase 4.4 — communication matrices and runtime closure

Purpose and acceptance: run the existing Agnocast sample/end-to-end scenarios
with the packaged Bazel runtime, including the complete 128-configuration
one-to-one matrix, 24 two-to-two configurations and nine service arrangements.
Keep the seven functional launches, wrapper backend tests and full adopted
package suites as regression gates. Actual CLI discovery, echo and recording
behavior is also required before closure. Kernel compilation/KUnit and CUDA
transport samples remain separate platform/GPU work, not user-space test
substitutes. The initial runtime gate uses Cyclone DDS; the separately approved Tier 2 milestone is recorded below.

Inspected inputs: scripts/test/e2e_test_1to1.bash and e2e_test_2to2.bash,
plus test_1to1.py, test_2to2.py, test_service.py and their fixture YAMLs. Reuse
the Python scenarios and their assertions; do not run the shell wrappers,
which source /opt/ros, invoke colcon, edit checked-in YAML and write to kmsg.
The one-to-one matrix is the Cartesian product of Agnocast/ROS publisher,
publisher/subscriber startup order, publisher/subscriber depths 1/10,
publisher/subscriber durability, and callback/take subscriptions. Two-to-two
uses agno2agno, ros2agno and agno2ros across the eight original PPSS layouts.
The service source already parameterizes all nine arrangements itself.

The root e2e_matrix.py driver creates a fresh directory per configuration,
copies the original scenario beside an immutable JSON-as-YAML configuration,
and runs RCR's launch-testing entry point with the same pinned interpreter.
This preserves scripts that locate YAML relative to __file__ without editing
source or inventing a second scenario. Each case starts in a separate process,
with the root launch adapter setting preload after Python bootstrap. Keep the
import closure in PYTHONPATH, reuse RCR's process guard, and bound each launch
with a timeout/process-group cleanup. Aggregate the actual per-case JUnit
results and retain configurations, source copies, logs and XML under the
Bazel target's test.outputs directory. Assert the matrix cardinalities; a
small direct self-check also verified unique one-to-one configurations and
exactly two publishers/subscribers in every two-to-two layout.

Add the fixture's sample interfaces and discovery messages as explicit product
roots and matching plugin compile/shared dependencies. They are current
runtime inputs, not speculative package adoption. The generated library now
exports 394 typed factories, representing 197 message/service/event
instantiations. The interface manifest needs an explicit build to refresh;
consuming its provider does not require its manifest output action.
The root CLI explicitly includes Python providers for the selected interface
packages, because C++ plugin dependencies do not supply Python imports.

Final results:

- The seven functional launches pass 38 cases without skips (see Phase 4.3).
- The complete two-to-two matrix passes all 24 cases in 245.8 seconds.
- All nine service arrangements pass 45 assertions/cases in 18.1 seconds,
  covering native services and both automatic bridge directions.
- The complete one-to-one matrix passes all 128 configurations, 384 cases,
  in 1551 seconds, with no failures or skips. Its first build was cancelled
  before tests after prolonged compilation and almost all 8 GiB of swap being
  consumed. Reducing machine-local concurrency in ignored user.bazelrc allowed
  the full unchanged matrix to complete; host limits are not committed.
- The production bridge gate passes all 11 cases against the final runtime.
- The actual CLI roundtrip passes in 17.4 seconds: version lookup, topic
  discovery, daemon status, echo and graceful recording shutdown. The bag has
  positive message counts, Lyrical distro metadata, an embedded message schema,
  and a deserializable DynamicSizeArray with correct first/last vector values.

Commands (clean_bazel is the clean-environment function defined above):

```bash
clean_bazel test --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime --nocache_test_results \
  --test_output=errors --local_test_jobs=1 \
  //bazel/agnocast:matrix_2to2 //bazel/agnocast:launch_services
clean_bazel test --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime --nocache_test_results \
  --test_output=errors --local_test_jobs=1 //bazel/agnocast:matrix_1to1
```

All declared Cyclone DDS gates pass. No source test group or configuration
was removed to bypass a failure.

CLI version-path diagnosis: with LD_PRELOAD unset, the real built command
reported the heap hook unavailable despite its artifact being present in the
runtime prefix. Its fallback only checked /opt/ros/$ROS_DISTRO/lib. Reuse the
existing package-prefix library lookup for agnocastlib before that system
fallback, retaining preload priority and the existing not-preloaded warning.
The actual command now finds 2.4.1 for kernel, library, hook and CLI. Two new
regressions check the overlay case and preload priority; the entire CLI suite
passes 95 cases. UPSTREAM_AWF.md records the inspected main blob and related
version-command PR. This source fix is independent of Bazel path conventions.


Recording-resource diagnosis: recording initially delivered payloads but warned
that the message definition could not be found. A Python interface import does
not install ament IDL resources. Make each local rosidl_ament_package depend
on its own default_generators :ament_index, and put all 17 selected interface
indexes in the root CLI data alongside their :py providers. Existing RCR
helpers then expose IDL and resource-index markers without a custom generator.
The recording test checks the embedded schema, not only a nonempty bag. Use
`--topics /my_topic` with the pinned Lyrical recorder and set ROS_DISTRO for
its recorded distro metadata. Topic-list accepts --spin-time, not --no-daemon.

Final regression and CLI commands (same clean_bazel function):

```bash
clean_bazel test --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime --nocache_test_results \
  --test_output=errors //bazel/agnocast:cli_runtime_test \
  //bazel/agnocast:bridge_plugins_test
clean_bazel run //bazel/agnocast:ros2 -- agnocast version
clean_bazel test --config=no-agnocast --nocache_test_results \
  --test_output=errors --local_test_jobs=1 \
  @agnocastlib//... @agnocast_heaphook//:unit_tests \
  @agnocast_cie_thread_configurator//... @ros2agnocast_discovery_agent//... \
  @ros2agnocast//:tests @autoware_agnocast_wrapper//... \
  @autoware_node//... //bazel/tests/... //bazel/agnocast:template_tests
clean_bazel test --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_heaphook --nocache_test_results \
  --test_output=errors @autoware_agnocast_wrapper//:unit_tests
```

The combined regression passes all 39 targets: the 20-group library suite,
allocator (26), configurator (137 C++ plus seven Rust), discovery agent (147),
CLI (95), wrapper fallback (37 C++ and 66 Python), core-node, smoke and template
checks. The library reports 672 cases, with five existing unsupported
late-timer/callback-isolated-executor skips (upstream #1263); all other cases
pass. These are upstream unsupported behaviors, not missing dependencies or
omitted groups. The separate enabled-wrapper run passes all 37 cases without
skips in 2.2 seconds. The standalone CLI reports all four versions as 2.4.1
without a sourced ROS installation; its not-preloaded hook warning is expected.

The final metadata-only IDL/index additions were checked by the actual CLI
roundtrip, refreshed interface manifest (175 descriptors), and production bridge
runtime. The long communication matrices had already passed against the same
native fixtures and scenario code. No Fast DDS or pre-commit run was performed.
Kernel compilation/KUnit, CUDA samples, other distributions and hardware/GPU
behavior remain outside this user-space gate. Next: Phase 5.1 shared core
foundations, choosing the next package from runnable dependency graphs.

Targeted final static checks pass: buildifier checks all 29 changed Bazel
files, Python AST parsing and bash -n check the changed scripts, and
git diff --check reports no whitespace errors. No all-files hook was run.


Module-boundary validation on 2026-11-16 also passes the discovery packages
with `clean_bazel test @ros2agnocast_discovery_agent//...
@agnocast_ioctl_wrapper//... @ros2agnocast_discovery_msgs//...
--nocache_test_results --test_output=errors --local_test_jobs=1` (28 non-test
targets and 147 agent cases); configurator, containers and library together
with `clean_bazel test @agnocast_cie_thread_configurator//...
@agnocast_components//... @agnocastlib//... --nocache_test_results
--test_output=errors --local_test_jobs=1` (all 27 test targets); and the full
fixture surface with `clean_bazel build @agnocast_sample_interfaces//...
@agnocast_sample_application//... @agnocast_e2e_test//...` (119 targets).
The CLI/wrapper package boundary passes its 95/66/37 cases with
`clean_bazel test --config=no-agnocast @ros2agnocast//...
@autoware_agnocast_wrapper//... --nocache_test_results --test_output=errors
--local_test_jobs=1`, plus the enabled-wrapper command above (37 cases).
Keep the heap-hook launcher dependency fix with wrapper packaging; retaining
its old payload dependency caused redundant execution-configuration builds,
which were stopped before test execution. The corrected launcher passes.


### Phase 4 Tier 2 milestone — Fast DDS (complete)

Purpose and prerequisites: complete Phase 4's secondary middleware gate after
the Cyclone module/product checks pass. The user approved this milestone on
2026-11-16. Use the same pinned Lyrical workspace, loaded 2.4.1 kernel module,
built heap hook and complete suites with --config=fastdds. Keep the ignored
host limits and sequential tests; no pre-commit or test filters are used.

Two failures required corrections before this gate could pass:

- Native Python shutdown: the CLI passed all 95 assertions, then aborted in
  Fast DDS's DomainParticipantQos destructor. RCR rclpy embedded ROS/DDS code
  without dynamic_deps while rosbag loaded shared copies. A pinned
  rclpy 10.0.11-1.rcr.0 BUILD patch adds the existing DEPENDENCIES shared targets.
  ELF inspection then shows ROS/DDS DT_NEEDED entries and no embedded DDS
  destructor. The complete CLI suite exits normally. The documented patch
  preface and UPSTREAM_RCR.md include the published overlay identity and
  reproducer; local correction is a2dcb386.
- Bridge handoff: one of nine service arrangements failed after a replacement
  manager encountered EADDRINUSE. The kernel had cleared the old manager's
  role while its abstract socket remained held through DDS teardown. Align
  the four-file source correction with draft AWF PR #1519 at
  be1c2b1dae9a2d5105026871f874ddb84d2305aa: release the listener before teardown
  and bound bind retry to 50 × 100 ms. Preserve other errors and the zero-retry
  duplicate-bind test; its added held-address retry case passes in 200 ms.
  All nine original service arrangements and their assertions are unchanged.
  UPSTREAM_AWF.md associates issue #1501 and the draft PR, distinguishing it
  from the already-present eventfd wakeup in PR #1507. Local fix is 26f1d0ae.

The complete library and product behavior suites pass after these changes.
Cyclone passes all 39 package regression targets, all 37 enabled-wrapper cases,
and all ten bridge/CLI, functional and service targets. The full Cyclone
matrices passed earlier in Phase 4; the final Fast DDS sweep covers every
configuration after the corrections. An additional final-code Cyclone repeat
was stopped after 37/128 configurations (111 cases, no failures/skips) when
the user requested more proportionate validation. That partial repeat is not
counted as a completed sweep. No middleware result is inferred from upstream
PR testing claims.

Exact final Fast DDS commands (same clean_bazel function as above):

```bash
clean_bazel test --config=fastdds --config=no-agnocast \
  --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @agnocastlib//... @agnocast_heaphook//:unit_tests \
  @agnocast_cie_thread_configurator//... @ros2agnocast_discovery_agent//... \
  @ros2agnocast//:tests @autoware_agnocast_wrapper//... \
  @autoware_node//... //bazel/tests/... //bazel/agnocast:template_tests
clean_bazel test --config=fastdds --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_heaphook --nocache_test_results \
  --test_output=errors \
  @autoware_agnocast_wrapper//:unit_tests
clean_bazel test --config=fastdds --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime --nocache_test_results \
  --test_output=errors --local_test_jobs=1 \
  //bazel/agnocast:bridge_plugins_test //bazel/agnocast:cli_runtime_test \
  //bazel/agnocast:launch_agnocast_component_container_cie \
  //bazel/agnocast:launch_agnocast_callback_isolated_executor \
  //bazel/agnocast:launch_multi_domain_cie_talker \
  //bazel/agnocast:launch_thread_configurator_restart \
  //bazel/agnocast:launch_thread_configurator_reapply \
  //bazel/agnocast:launch_thread_configurator_wildcard \
  //bazel/agnocast:launch_thread_configurator_precedence \
  //bazel/agnocast:launch_services \
  //bazel/agnocast:matrix_1to1 //bazel/agnocast:matrix_2to2
```

Final result: all 39 Fast DDS regression targets, the enabled-wrapper target
(37 cases), and all 12 product-runtime targets pass. The library has 673 cases,
including the new regression; five existing unsupported-executor skips remain
and all 668 supported cases pass. No dependency-related skips or omitted groups
were introduced. Product results have no skips: 128 one-to-one configurations
(384 cases), 24 two-to-two configurations, nine service arrangements (45 cases),
seven functional launches (38 cases), production bridge loading/activation
(11 cases) and actual CLI discovery/echo/recording (one roundtrip checking
embedded schema, distro metadata and deserialized payload). Both wrapper
runtime selections and its complete 66-case Python suite pass.

The complete post-fix Cyclone regression uses the same three commands with
--config=fastdds omitted and the two long matrix labels omitted from the last
command; it passes 39 + 1 + 10 targets. The complete module suites remain
mandatory, while broad system-matrix repeats are selected by changed behavior
and approved milestone scope rather than automatically per phase. Reuse valid
results for unchanged code/configuration and document the actual boundary.
Targeted format/syntax checks, patch application and git diff --check pass.
This documentation-only closure performs no additional runtime validation;
it records the completed commands above. No pre-commit was run. Kernel compilation/KUnit,
CUDA samples, other distributions, device/GPU behavior and performance remain
outside this user-space milestone. Phase 4's primary and Tier 2 gates are
complete. Phase 3 still has its separate read-only utility-image gate open;
close that recorded gap before further core expansion in Phase 5.1. Future
Fast DDS gates still require fresh approval.

## 2026-11-16: Phase 3 — complete utility development-image gate

Purpose: validate the complete bounded utility set independently of host ROS
and development packages, closing the remaining Phase 3 exit criterion.
The scope is the nine already-integrated packages: math, system, rclcpp, UUID,
visualization, logging, diagnostics, geometry and TF. Each package's existing
BUILD test target includes its complete test source set. This gate adds no
packages or tests and requires no Agnocast kernel module or communication
matrix. Cyclone DDS is the only middleware selected for this container check.

Prerequisites and inspected inputs: images/dev.Dockerfile, the root's nine
local_path_override selections, each utility's BUILD test source glob, and
Phase 3's previous host results. Docker Desktop 29.8.2 is available. Build the
tracked image definition rather than assuming an existing tag is current:

```bash
docker build -f images/dev.Dockerfile -t autoware-bazel-dev:bootstrap .
docker volume create autoware-bazel-bootstrap-cache
```

The image build succeeds with image ID
`sha256:08faa9513d454694410dcbdc7a667ff7a5eabc68057da684042515ea300fbfec`.
Its Ubuntu 24.04 base and Bazelisk version/checksum procedure are tracked in
the Dockerfile. Reuse the existing named cache volume; no host cache or ROS
installation is mounted. Stop an idle host Bazel server if needed to release
memory. The resource values below are invocation-specific, not new committed
machine defaults; choose limits appropriate to the Docker VM.

Run all declared utility targets, forcing actual test execution:

```bash
docker run --name autoware-bazel-phase3-20261116-j2 --memory=6g --cpus=2 \
  -v autoware-bazel-bootstrap-cache:/home/ubuntu/.cache \
  -v "$PWD:/workspace:ro" \
  autoware-bazel-dev:bootstrap \
  bazel --host_jvm_args=-Xmx3g test \
  --experimental_convenience_symlinks=ignore \
  --jobs=2 --local_resources=cpu=2 --local_resources=memory=2048 \
  --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --build_event_json_file=/home/ubuntu/.cache/phase3-validation.bep.json \
  @autoware_utils_math//... @autoware_utils_system//... \
  @autoware_utils_rclcpp//... @autoware_utils_uuid//... \
  @autoware_utils_visualization//... @autoware_utils_logging//... \
  @autoware_utils_diagnostics//... @autoware_utils_geometry//... \
  @autoware_utils_tf//...
```

Docker inspection confirms /workspace has RW=false, the named cache has
RW=true, and the 6 GiB/two-CPU container limits apply. The ignored host
user.bazelrc is visible through the source mount, so the command explicitly
overrides its build resources for the smaller Docker VM. The JVM cap bounds
analysis memory. An initial single-worker build was stopped before tests after
measurement showed 1.8 GiB of 6 GiB in use and less than one CPU utilized;
the final invocation uses two workers with the same cache and unchanged scope. Convenience symlinks are disabled so Bazel never needs to
create output links in the read-only source. The image provides only bootstrap
tools; target headers, libraries, compilers and test dependencies come through
the Bazel graph. Keep the stopped container until its XML and build-event
reports have been inspected, then remove it while retaining the cache volume.

Result: the final invocation succeeds in 484.622 seconds, building all 26
selected targets (17 non-test targets and nine test targets). All nine test
targets pass; XML confirms 165 executed cases pass with no failures, errors
or runtime skips. Geometry also lists two pre-existing source-disabled cases:
DISABLED_intersectPolygon and DISABLED_intersectConcavePolygon. Their source
comments identify differing GJK results for shared-edge/shared-point cases.
They remain disabled by the existing suite; this gate adds no filter and does
not count them as passed. No dependency-related skip or omitted package is used.

| Utility package | Executed/pass | Source-disabled |
| --- | ---: | ---: |
| autoware_utils_diagnostics | 3 | 0 |
| autoware_utils_geometry | 80 | 2 |
| autoware_utils_logging | 1 | 0 |
| autoware_utils_math | 40 | 0 |
| autoware_utils_rclcpp | 10 | 0 |
| autoware_utils_system | 17 | 0 |
| autoware_utils_tf | 2 | 0 |
| autoware_utils_uuid | 5 | 0 |
| autoware_utils_visualization | 7 | 0 |

The container exits 0. Final Docker inspection confirms the read-only source
mount and writable cache, and build-event summaries show PASSED for all nine
suites. Export the build-event file before removing the stopped container:

```bash
docker cp autoware-bazel-phase3-20261116-j2:/home/ubuntu/.cache/phase3-validation.bep.json \
  /tmp/phase3-validation.bep.json
```

Each testResult event identifies its test.xml and test.log through
file:// URIs inside the container. Copy those exact paths with docker cp and
inspect testcase status/result as well as failures/errors/skipped: GTest lists
DISABLED_ cases as status=notrun, result=suppressed rather than skipped elements.
The reports were exported and inspected for this gate. Remove only this task's
stopped containers afterward, keeping the named cache volume for future work.

All Phase 3 exit criteria are now met: the bounded local-source utility set
builds and its complete existing default suites execute in the minimal image
with the source read-only. Package-local C++/ROS/shared-library conventions
are already documented in DEVELOPING.md. No source or build-definition fix was
required. Additional service-callback/timeout coverage and the two explicitly
disabled geometry cases remain known test limitations, not missing container
prerequisites. No Fast DDS middleware validation, pre-commit or unrelated
runtime sweep was run. This closure changes documentation only and records
the completed container run; it needs no further runtime rerun. Next: Phase
5.1 shared core foundations, with Phase 4's communication lane already complete.

## 2026-11-16: Phase 5.1 — autoware_kalman_filter

Purpose: Eigen-only state-estimation primitives for the EKF localizer.

Prerequisites and inspected inputs: the pinned Phase 3/4 workspace and
CMakeLists.txt, package.xml, both public headers, two implementation files and
both existing test files. No ROS include is used by this implementation.

Minimum integration pattern and rationale: Use one cc_library with the RCR Eigen
provider and one cc_test containing both existing files (23 static test
declarations). No ROS shared closure or separate Eigen download is required.

Select the package-local 0.0.0 module with a root local_path_override. Component
definitions use only package/external labels, with no root bazel/ dependency.
Source compatibility patches and dependency upgrades remain hypotheses until
this target exposes a need.

Verification command:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors \
  --local_test_jobs=1 @autoware_kalman_filter//...
```

Result: both library sources and both existing test files build. All 23 cases
pass without failures, errors, disabled cases or skips, confirmed in GTest XML.
The module-only root selection passes the full gate in 7.538 seconds with
cached compilation and actual test execution. Keep each committed root override
paired with its package MODULE/BUILD files; future draft modules are not needed
by this commit. No ROS runtime, source compatibility fix or additional Eigen
release was required. Targeted buildifier and whitespace checks pass. No Fast
DDS, pre-commit, broad communication matrix or container rerun was performed.

## 2026-11-16: Phase 5.1 — autoware_qos_utils

Purpose: Expose the default QoS compatibility macros for map/localization
consumers.

Prerequisites and inspected inputs: the pinned Phase 3/4 workspace and
CMakeLists.txt, package.xml, the sole public header and cached RCLCPP
publisher/service declarations. The package has no existing tests.

Minimum integration pattern and rationale: Export a header-only cc_library. A
Bazel-owned test under test/ consumes both macros through real RCLCPP
publisher/service construction, checking topic depth and service naming. This is
a Lyrical consumer contract check, not a cross-distribution or delivery-matrix
claim.

Select the package-local 0.0.0 module with a root local_path_override. Component
definitions use only package/external labels, with no root bazel/ dependency.
Source compatibility patches and dependency upgrades remain hypotheses until
this target exposes a need.

Verification command:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors \
  --local_test_jobs=1 @autoware_qos_utils//...
```

Result: the header library and consumer executable build; the single consumer
case passes with Cyclone DDS and no skips. The test constructs a publisher and
service through both macros, checking the resolved topic depth and service
name. It checks the Lyrical API contract, not service delivery or older ROS
distributions. The initial link failed on undefined main: unlike several
existing suites, this new test has no local main. Depend on gtest_main rather
than gtest for the runner. The complete corrected module gate passes in
5.837 seconds. Targeted formatting and whitespace checks pass; no source
compatibility patch, Fast DDS run or broad matrix is needed.

## 2026-11-16: Phase 5.1 — autoware_signal_processing

Purpose: Provide the low-pass and Butterworth filters used by
localization/planning.

Prerequisites and inspected inputs: the pinned Phase 3/4 workspace and
CMakeLists.txt, package.xml, all three implementation files, public includes,
three existing test files and the private Butterworth fixture header.

Minimum integration pattern and rationale: Use the narrow Boost.Optional and
generated geometry interfaces alongside RCLCPP logging. Keep the fixture include
directory on the test and collect geometry/RCLCPP shared providers at the final
executable. All 12 existing static test declarations are registered.

Select the package-local 0.0.0 module with a root local_path_override. Component
definitions use only package/external labels, with no root bazel/ dependency.
Source compatibility patches and dependency upgrades remain hypotheses until
this target exposes a need.

Verification command:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors \
  --local_test_jobs=1 @autoware_signal_processing//...
```

Result: all three implementations and three existing test files build. All 12
cases pass without failures, errors, disabled cases or skips, confirmed in GTest
XML. The complete module gate takes 9.117 seconds with cached dependencies.
These sources have no test main, so the target explicitly depends on gtest_main
as its runner. No source compatibility patch or dependency upgrade is required.
Targeted buildifier and whitespace checks pass. No Fast DDS check, pre-commit,
broad communication matrix or container rerun is performed.

## 2026-11-16: Phase 5.1 — autoware_interpolation

Purpose: Provide interpolation routines required by motion utilities and
velocity smoothing.

Prerequisites and inspected inputs: the pinned Phase 3/4 workspace and
CMakeLists.txt, package.xml, four sources, six headers and eight existing test
files (32 static test declarations). Inspect direct includes rather than adding
every manifest dependency to cc_library.

Minimum integration pattern and rationale: Reuse local autoware_utils_geometry,
Eigen and existing TF/message providers. The final test needs geometry's full
message/TF shared closure because the compiled geometry library is linked
statically beneath this consumer. No unrelated source fix or additional TF/Eigen
release is introduced.

Select the package-local 0.0.0 module with a root local_path_override. Component
definitions use only package/external labels, with no root bazel/ dependency.
Source compatibility patches and dependency upgrades remain hypotheses until
this target exposes a need.

Verification command:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors \
  --local_test_jobs=1 @autoware_interpolation//...
```

Result: all four implementations, public headers and eight existing test files
build. All 32 cases pass without failures, errors, disabled cases or skips,
confirmed in GTest XML. The complete module gate takes 22.789 seconds with
cached dependencies. The existing test source supplies main; use gtest rather
than adding another runner. The declared geometry/message/TF shared closure
links successfully. No source compatibility patch or dependency upgrade is
required. Targeted buildifier and whitespace checks pass. No Fast DDS check,
pre-commit, broad communication matrix or container rerun is performed.

## 2026-11-16: Phase 5.1 — autoware_vehicle_info_utils

Purpose: Expose vehicle parameters, derived geometry and package resources to
later runnable slices.

Prerequisites and inspected inputs: the pinned Phase 3/4 workspace and
CMakeLists.txt, package.xml, both implementations, scoped and compatibility
headers, both existing test files, config/launch resources and the installed
calculator script.

Minimum integration pattern and rationale: Register an ament_index containing
package.xml and the config/launch files. The original parameter test reads
config/vehicle_info.param.yaml via ament_index_cpp; carry the index in its data
rather than rewriting the test. Keep the two ROS parameter cases separate from
13 pure vehicle-geometry cases. The calculator py_binary uses ament_index_python
and the pinned RCR PyYAML provider with the same resource index. Libraries/tests
retain the geometry and RCLCPP shared closure.

Select the package-local 0.0.0 module with a root local_path_override. Component
definitions use only package/external labels, with no root bazel/ dependency.
Source compatibility patches and dependency upgrades remain hypotheses until
this target exposes a need.

Verification commands:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors \
  --local_test_jobs=1 @autoware_vehicle_info_utils//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel run @autoware_vehicle_info_utils//:min_turning_radius_calculator
```

Result: both implementations, both include layouts, resource index, calculator
and both existing test targets build. All 15 cases (2 parameter and 13 geometry)
pass without failures, errors, disabled cases or skips, confirmed in GTest XML.
The complete module gate takes 11.905 seconds with cached dependencies. Both
test files lack main, so both targets use gtest_main. The unchanged parameter
tests find the declared YAML through ament_index_cpp with no host ROS setup.

The calculator's default run succeeds through ament_index_python's runfiles
lookup, reporting rear radius 3.253042620027102 m and front radius
4.253220695862465 m. These equal wheel_base / tan(max_steer_angle) and
wheel_base / sin(max_steer_angle) for the indexed YAML's 2.74 m and 0.70 rad.
The package reuses the pinned pip_ros PyYAML wheel; the lockfile records the
additional extension usage without changing its dependency versions. No host
pip installation, source patch or custom package-index wrapper is required.
Targeted buildifier and whitespace checks pass. Launch files are exposed as
resources; launch composition and shared/install exports remain for consumers
that require them. No Fast DDS check, pre-commit, broad communication matrix or
container rerun is performed. This closes the five-module Phase 5.1 gate.

## 2026-11-16: Root integration test layout

Purpose: Keep root-owned integration targets in one intuitive package,
bazel/tests, without distinguishing bootstrap from core behavior by directory.

Inputs: all five interface consumer tests, four talker/listener executables and
nine unchanged C++ sources previously in the bootstrap test package. Move them
together without aliases. Keep target names unchanged; update journal commands
to the current namespace. Package-owned tests belong in each component's test/
directory; root targets select product runtime dependencies. Genuine Bazel rule
implementations remain with their owning component or workspace support.

Verification (Cyclone DDS, sequential builds and local_test_jobs=1):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  //bazel/tests:autoware_common_msgs_test \
  //bazel/tests:autoware_localization_msgs_test \
  //bazel/tests:autoware_interface_layer_test \
  //bazel/tests:autoware_interface_base_test \
  //bazel/tests:autoware_interface_dependent_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build //bazel/tests:talker //bazel/tests:listener \
  //bazel/tests:localization_talker //bazel/tests:localization_listener
```

For each pair, run the built listener before its talker in a clean environment
with HOME, PATH, ROS_DISTRO=lyrical, RMW_IMPLEMENTATION=rmw_cyclonedds_cpp,
LD_LIBRARY_PATH=lib and ROS_HOME=/tmp/autoware-bazel-ros-home. Use ROS_DOMAIN_ID=81
for the plain pair and 82 for localization. Capture listener output with a
20-second timeout; require exit 0 and respectively `Received 5 messages; smoke
test passed` or `Received 5 localization states; behavior test passed`. Send
SIGINT to the continuously running talker and wait up to five seconds, killing
and reaping only on timeout. The Python subprocess harness ran both pairs in
this order; both received five messages and exited successfully. All five
interface tests pass with no failures. Targeted buildifier and whitespace
checks pass. No Fast DDS, container sweep or pre-commit was run.

## 2026-11-16: autoware_qos_utils test layout

Purpose: Keep the personal QoS consumer test alongside conventional package
tests. Move qos_compatibility_test.cpp from the package's Bazel-specific test
directory into test/, update its BUILD srcs and remove the empty directory.
The source, target name, dependencies and contract are unchanged.

Verification:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_qos_utils//...
```

The complete module builds and its one consumer case passes from test/, with
no failures/errors/skips in GTest XML. Targeted buildifier and whitespace checks
pass. No Fast DDS, pre-commit or communication matrix was run.

## 2026-11-16: autoware_node test layout

Purpose: Keep the personal pub/sub consumer test beside the existing upstream
initialization test. Move test_pubsub.cpp into test/, update its BUILD srcs and
remove the empty Bazel-specific test directory. Preserve source and target
behavior and the original test_init/shutdown source.

Verification:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_node//...
```

The complete module builds and both cases pass from test/, with no
failures/errors/skips in GTest XML. Targeted buildifier and whitespace checks
pass. No Fast DDS, pre-commit or communication matrix was run.
