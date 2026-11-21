# Autoware + Bazel integration journal

## Before starting your integration

Complete the [adoption checklist](PLAN.md#before-starting-your-integration)
before following the dated experiments. It points to the current rules and the
entries that established them. Apply those conventions from the beginning;
earlier entries retain historical layouts and decisions that later entries may
supersede. Re-derive technical fixes for your current target and source snapshot.

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

## 2026-11-16: Phase 5.2.1 — autoware_vehicle_velocity_converter

Purpose: Introduce the first core sensing executable using the available
Agnocast wrapper and geometry providers. Validate speed scaling, axis mapping,
covariance and headers across a real standalone node before bringing in new
point-cloud, map or solver dependencies.

Prerequisites and inspected inputs: completed Phase 5.1; the package's
CMakeLists.txt, package.xml, two implementation/header pairs, both existing test
files, parameter YAML and XML launch file; the wrapper's registration CMake and
switchable-main template; pinned RCR rclcpp_components rules and rosdistro ament
index/resource and dynamic-closure rules. The two existing test files contain
five algorithm cases and one RCLCPP-only node case, with no local test main.

Minimum integration pattern: create a local 0.0.0 module and select it with a
root local_path_override. Export the algorithm as a cc_library; keep its
source-local headers at their existing paths. Layer a node cc_library and
cc_shared_library over it. Include the already-validated geometry message/TF
closure and wrapper shared provider at the final link boundary. Use gtest_main
for both existing test files, retaining the RCLCPP-only node suite rather than
following CMake's ENABLE_AGNOCAST-dependent omission.

Add a wrapper-owned defs.bzl adapter that expands the existing main template
through a native Bazel action. Get the actual shared-library basename from
CcSharedLibraryInfo instead of guessing an output filename. Fix the executor
pair to the current package's declared AgnocastOnlyCallbackIsolatedExecutor and
SingleThreadedExecutor; keep other executor variants out until needed. Reuse
RCR component records and register the autoware_node_plugins resource as
`<package>__<executable>` with `<class>;1`, matching the launch extension's
compiled-support marker. Put component records, YAML and launch files in an
ament_index, with the executable registered under lib/<package> for package
lookup. The adapter resolves its own template and compile dependency
labels in the wrapper repository, independent of the consumer's checkout path.

Product-owned behavior composition lives in bazel/tests. Package-owned Python test source stays in test/test_standalone.py alongside
the existing tests; root composition supplies the two Python unittest targets.
These targets launch the actual executable with the real YAML and a scale override
of 1.5, send VelocityReport with ordinary RCLCPP peers, and check a scaled
longitudinal speed of 3.0, lateral/yaw values, frame and configured covariance.
They require normal SIGINT exit. The enabled case exercises automatic bridging
using the existing all-interface plugins, which already include vehicle and
geometry message types. The component has no dependency on these root targets.
The driver is never heap-preloaded; only the enabled native child gets the
heap hook. Both variants use the established root runtime launcher.

Acceptance commands (results below):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build @autoware_vehicle_velocity_converter//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results --test_output=errors \
  --local_test_jobs=1 @autoware_vehicle_velocity_converter//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results --test_output=errors \
  --local_test_jobs=1 --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:vehicle_velocity_converter_rclcpp_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors \
  --local_test_jobs=1 --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:vehicle_velocity_converter_agnocast_test
```

The enabled command needs the compatible loaded kernel module and accessible
/dev/agnocast, plus the declared runtime_index and heap-hook targets. Reuse the
Phase 4 setup rather than skipping this gate if a prerequisite is absent.
Inspect GTest XML for all six original cases executed with no failures/skips;
each standalone variant must execute its one behavior case. The runtime launch
checks do not establish XML launch composition or install-image behavior; those
remain with later product composition and packaging gates. Existing unchanged
wrapper runtime suites retain their Phase 4 evidence; this task specifically
exercises the new main-generation path.

Result: the complete 14-target module surface builds (28.251 seconds), and
both existing targets execute all six cases without failures/errors/skips,
confirmed in GTest XML. The standalone checks pass from test/test_standalone.py
under the unified bazel/tests package: RCLCPP in 1.1 seconds and Agnocast in
1.3 seconds. Both verify dynamic component loading, YAML/override resolution,
payload/header/covariance and normal SIGINT exit. Each target explicitly selects
the native child's ENABLE_AGNOCAST and asserts its startup backend log, so a
global test environment flag cannot silently switch the tested mode. The
Python driver is not heap-preloaded.

Device checks must use host execution: the ordinary command sandbox has an
isolated /dev and misleadingly showed no device. Host execution confirms an
accessible /dev/agnocast and loaded 2.4.1 module. An attempted sudo -n modprobe
required authentication; the developer also ran modprobe successfully. No
kernel rebuild or device workaround was needed. No source compatibility patch
or additional dependency version was required by this module. Targeted
buildifier, Python syntax/template inventory and whitespace checks pass.
Existing unchanged wrapper suites retain their Phase 4 evidence. No Fast DDS,
pre-commit or broad communication/container sweep was run.

## 2026-11-16: Phase 5.2.1 — autoware_stop_filter

Purpose: Expose stopped-vehicle odometry filtering and the timestamped stop flag
as a switchable standalone node, reusing the converter's packaging pattern.

Inputs: package.xml, CMakeLists.txt, both source/header pairs, YAML/launch files,
six existing pure cases and one RCLCPP node case. The node case is omitted by
CMake with ENABLE_AGNOCAST=1; retain it under the explicit no-agnocast gate.
Neither file has main. The current source requires BoolStamped and Odometry.
RCR metadata supplies autoware_internal_debug_msgs 1.16.0-1.rcr.0, matching the
source snapshot's version; select it without making an unnecessary local
interface module. nav_msgs 5.9.3-1.rcr.0 is already in the resolved ROS graph.
The module build below validates these selections.

Integration: package-local algorithm library, node library/shared component,
wrapper-owned switchable main, ament index with component/plugin records and
lib/<package> executable, existing YAML/launch resources, and both unmodified
existing test files using gtest_main. The final closure needs debug/nav message
shared providers and the wrapper/component runtime. Add debug/nav interfaces
to the product's all-interface roots and bridge compile/shared lists because
the enabled standalone test exposes these types to ordinary ROS peers.

Behavior gate: the actual standalone binary reads its YAML, consumes Odometry
with velocities below the configured thresholds, publishes zeroed twist and a
true BoolStamped stop flag, preserving frame/pose and timestamp. Test both
backends and normal SIGINT exit, without preloading the Python driver. Existing
pure tests cover moving/threshold/axis behavior. No source compatibility patch
is anticipated or applied.

Verification commands (executed sequentially):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results --test_output=errors \
  --local_test_jobs=1 @autoware_stop_filter//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results --test_output=errors \
  --local_test_jobs=1 --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime //bazel/tests:stop_filter_rclcpp_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors \
  --local_test_jobs=1 --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime //bazel/tests:stop_filter_agnocast_test
```

Result: the full module surface builds and both existing targets pass all
seven cases, confirmed in XML with no failures/errors/skips (106.749-second
initial build/test). The standalone RCLCPP and Agnocast checks pass in
1.3 and 1.6 seconds respectively from test/test_standalone.py, checking both
outputs and clean exit. Each target selects and asserts the actual backend.
The published debug messages and pinned nav_msgs resolve without a local
interface override or host dependency installation.

Adding debug/nav package roots expands the existing generated plugin library's
compile include set and invalidates many plugin compile actions; the first
expanded-runtime invocation takes 472.823 seconds, while its actual standalone
check takes 2.3 seconds. Reuse this built runtime for remaining checks; do not
mistake generated-plugin compilation cost for a slow node test. Later runs use
cached compilation. No plugin-rule redesign is introduced here. RCR metadata
was inspected at
https://bazel.ros.org/modules/autoware_internal_debug_msgs/metadata.json.

No source compatibility patch was needed. Targeted buildifier, Python syntax
and whitespace checks pass. XML launch composition/install-image checks belong
to later product gates. No Fast DDS, pre-commit or broad communication/container
sweep was run.

## 2026-11-16: Phase 5.2.1 — autoware_twist2accel

Purpose: Expose finite-difference acceleration estimation and a standalone node
that accepts odometry or twist-with-covariance, completing this velocity group.

Inputs: package.xml, CMakeLists.txt, both sources/headers, YAML and launch file,
nine estimator cases and three RCLCPP node cases across the two existing test
files. The original node suite covers twist input, odometry input and ignored
odometry when use_odom is false. Neither test file has main. Dependencies on
signal processing, geometry and ROS/nav messages are already selected.

Integration: layer an estimator cc_library beneath the node library/shared
component and wrapper-owned switchable main. Reuse geometry's complete shared
closure plus nav_msgs and wrapper providers. Register component/plugin metadata,
YAML/launch resources and the executable's lib/<package> alias in ament_index.
Keep all twelve existing cases with gtest_main in two separate targets. The
existing RCLCPP fixtures run with no-agnocast; the enabled executable retains
AgnocastOnlyCallbackIsolatedExecutor support unconditionally.

Standalone behavior: run two cases per backend, one per use_odom selection,
loading the real YAML and overriding accel_lowpass_gain to zero for a precise
finite-difference contract. Send increasing velocities at one-second message
timestamp intervals; require linear acceleration 2.0 and yaw acceleration 0.5,
frame propagation, configured fixed covariance and normal SIGINT shutdown.
The Python driver uses ordinary ROS peers. Enabled checks use the same product
bridge/runtime and native-child-only heap preload as the previous modules.
No new interface roots are required beyond the selected geometry/nav closure.

Verification commands (executed sequentially):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results --test_output=errors \
  --local_test_jobs=1 @autoware_twist2accel//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results --test_output=errors \
  --local_test_jobs=1 --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime //bazel/tests:twist2accel_rclcpp_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors \
  --local_test_jobs=1 --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime //bazel/tests:twist2accel_agnocast_test
```

Result: the whole module builds and both existing targets pass all twelve
cases (9 estimator and 3 node cases), verified in GTest XML without
failures/errors/skips. Initial build/test takes 23.696 seconds. The standalone
RCLCPP and Agnocast targets each execute two cases; unittest logs confirm both
input modes pass, with no skips and normal node exits. They pass in 1.9 seconds each from test/test_standalone.py under bazel/tests.
Each target selects and asserts its actual backend. Bazel's fallback XML counts
one invocation, so use the unittest log's two-case count for these targets.

No source patch or new dependency version was required. Targeted buildifier,
Python syntax and whitespace checks pass. The module reuses the same built
bridge runtime as stop filter. This closes Phase 5.2.1: all 25 existing cases
and eight standalone behavior cases pass across the three packages. No Fast
DDS, pre-commit, broad communication matrix or container sweep was run.

## 2026-11-16: Phase 5.2.2 — autoware_localization_util

Purpose: Supply pose buffering/interpolation, covariance visualization, pose/TF
helpers and the tree-structured Parzen estimator required by gyro/EKF localization.
Move the active Phase 5 section ahead of the future Phase 6–8 scope markers in
PLAN.md; its subphase numbering and existing acceptance boundaries stay intact.

Inputs: CMakeLists.txt, package.xml, all four implementations, five public
headers and four existing test files. The complete gate is 33 cases: eight
smart-pose-buffer, six TPE, six ellipse/marker and thirteen utility cases. Preserve four test executables; only pose-buffer supplies global main, while
the utility file's similarly named function is namespaced and needs gtest_main. No existing case is disabled or skipped by inspection.

Integration pattern: local 0.0.0 module selected through the root override;
one compiled cc_library with the existing scoped include tree; direct geometry,
math, Eigen, ROS message/RCLCPP and TF compile providers. Select tf2_eigen
0.45.10-1.rcr.0 to match the already-pinned TF family. Keep the final shared
closure on each test, including compiled geometry's message dependencies and
TF/Eigen/visualization providers. The pose-buffer source includes ament-index
and YAML-parser headers, so declare those test dependencies; it does not read
resources and needs no artificial package-index or YAML fixture target.
Compile the existing library once rather than duplicating its source in tests.
No source patch is introduced without an observed failure.

Verification command (executed in a clean caller environment):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_localization_util//...
```

Result: all four sources and exported headers build, and all 33 cases execute
successfully across the four targets, confirmed in GTest XML without failures,
errors, disabled cases or skips. The corrected complete gate takes 6.376 seconds
with dependencies compiled during the initial invocation; TPE executes for
1.1 seconds and the other suites each for 0.1 seconds.

The first utility-test link failed on undefined main. Its apparent main is
inside namespace autoware::localization_util and is not a process entry point.
Select gtest_main for that target, as well as the two suites without main; only
smart-pose-buffer uses gtest with its own global entry point. No source change
is needed. Header-only TF Eigen and its declared shared provider resolve at the
matching 0.45.10-1.rcr.0 pin. No host ROS, apt/pip dependency or new resource
wrapper is required. Targeted buildifier and whitespace checks pass. No Fast
DDS, pre-commit, communication matrix or container rerun was performed.

Phase 5.2.2 remains in progress; gyro odometer is the next package boundary.

## 2026-11-16: Phase 5.2.2 — autoware_gyro_odometer

Purpose: Integrate IMU/vehicle-twist fusion as a switchable standalone node,
including diagnostics and TF lookup. Inputs: package.xml, CMakeLists.txt, three
source/header pairs, config/launch files and all three existing test files.
The complete existing gate is 35 cases: 20 fusion, six diagnostics and nine
node cases. Keep the node suite despite CMake omitting it for enabled builds;
its ordinary RCLCPP peers/executor require the no-agnocast runtime selection.
All three files need gtest_main. No source case is disabled or skipped.

Pattern: separate algorithm/diagnostics cc_library and wrapper-derived node
library/shared component. Reuse the switchable-main adapter and ament resource
pattern established in Phase 5.2.1, with tests under test/. Declare actual
geometry, diagnostics, fmt and sensor-message compile dependencies and the
complete final ROS/TF/logging/diagnostics shared closure. Select sensor_msgs
5.9.3-1.rcr.0 with the same ROS interface release family; expose rosgraph_msgs
for the existing simulated-clock node tests. No source patch is pre-applied.

The root standalone checks load the actual YAML and relax only message timeout
to ten seconds. Send matching vehicle/IMU samples in base_link, require all
four fused output topics, angular covariance reduction and healthy diagnostic
status, then require normal SIGINT exit. Existing node tests cover rotated and
unresolvable TF cases and simulated-clock diagnostics. Both backend checks
select and assert the actual child runtime. Enabled tests use the real heap
hook and bridge; Python peers remain unpreloaded. Add the specific Imu and
DiagnosticArray interface roots (and their declared closure), rather than
unrelated sensor interfaces, to the product bridge selection. Component files
have no root-module labels.

Verification commands (executed sequentially):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results --test_output=errors \
  --local_test_jobs=1 @autoware_gyro_odometer//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:gyro_odometer_rclcpp_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:gyro_odometer_agnocast_test
```

Result: the complete module surface builds and all 35 original cases pass
without failures/errors/skips, confirmed in GTest XML. The final complete
existing-suite invocation takes 21.051 seconds. Initial compilation exposed
missing test-private header lookup: these tests include headers by basename,
so put includes=["src"] on each test, matching CMake's PRIVATE src path,
rather than changing source includes or exporting a broad library include path.

Both standalone checks pass: RCLCPP in 1.8 seconds and Agnocast in 1.5 seconds,
including all four output topics, covariance, healthy diagnostics and normal
SIGINT shutdown. Unittest logs confirm one executed case in each. The expanded
bridge compiles once (311.632-second invocation), then the enabled invocation
reuses it (3.484 seconds total). The specific Imu/DiagnosticArray root selections
avoid adding unrelated sensor plugins. The sensor_msgs pin is validated; no
host dependencies, source patches or new executor variant were needed.
Targeted buildifier, Python syntax and whitespace checks pass. No Fast DDS,
pre-commit, broad matrix or container rerun was performed. EKF is next.

## 2026-11-16: Phase 5.2.2 — autoware_ekf_localizer

Purpose: Integrate EKF prediction/measurement updates and the standalone ROS
node with activation, initialization, odometry and clean process shutdown.
Inputs: package.xml, CMakeLists.txt, nine implementation files, source headers,
all ten C++ test files, existing Python launch test, YAML and XML launch file.
The current complete C++ inventory is 49 cases; none supplies main or disables
cases. Retain node integration despite CMake filtering it for enabled builds.
The Python launch suite has one live behavior case and one post-shutdown case.

Integration: one source-scoped cc_library mirrors the CMake target, reusing
Kalman/localization utilities, wrapper, geometry/logging/system, Eigen/fmt and
ROS/TF/message providers. Tests need the package root for src/... includes;
the library exposes src for utils/... includes. Preserve all ten test targets,
using gtest_main and the complete final shared closure, including visualization
and TF Eigen reached through localization utilities. Build a shared component
and switchable node with the existing adapter and ament index; register YAML,
XML launch, component/plugin metadata and the lib/<package> executable alias.

Root launch_test targets reuse the original scenario under both backends. The
package-owned test/launch_adapter.py executes the original suite and prepends
one launch configuration: agnocast_heaphook_path from the declared runfiles,
avoiding the XML default's host /opt/ros path. It selects ENABLE_AGNOCAST per
target before the scenario creates processes; the Python driver is unpreloaded.
Both tests use real XML launch/package-index discovery and the original
activation service, initial-pose publication, odometry assertion and process
exit assertion. No scenario assertion or production node is changed. A discovery race exposed
by the enabled check is corrected in the test source as recorded below. Existing bridge roots already cover this node's
geometry, navigation, diagnostics, debug and SetBool types.

Verification commands (executed sequentially):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results --test_output=errors \
  --local_test_jobs=1 @autoware_ekf_localizer//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:ekf_localizer_rclcpp_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:ekf_localizer_agnocast_test
```

Result: all 49 C++ cases pass across ten targets without
failures/errors/skips, confirmed against the currently declared source/target
list in GTest XML (35.434 seconds for the initial module invocation). The
original XML scenario passes in RCLCPP but fails in Agnocast: activation is
successful, yet the one-shot volatile initial pose can precede bridge discovery
and the 200 ms output observation returns empty. Shutdown still passes.

Fix test synchronization: create the output subscriber first, poll for both
input/output endpoints before publishing the initial pose once, then poll for
odometry with a monotonic ten-second maximum deadline. Finish immediately when
ready; retain the output and exit-code assertions. The corrected original
suite passes both assertions under RCLCPP (1.4 seconds) and Agnocast
(3.3 seconds), with actual startup backend messages confirmed in launch logs.
The adapter supplies the declared heap-hook path; no host /opt/ros library is
used. Existing unchanged C++ results are reused after this Python-only change.

Upstream main's launch-test blob 1db0863636b041e18c653c3e049eeb4fa042ff89
matches the local pre-fix source. Searches and the actual #1190 patch show no
matching discovery fix; #1190 conditionally omits enabled launch tests instead.
Lyrical trackers #1538/#7404 concern the distribution transition, not this
readiness race. UPSTREAM_AWF.md records the dated association/limitations. Keep
the test-source correction in a separate fix(src/autoware_core) commit from the
Bazel definitions. No production-node patch is needed. Targeted Python syntax,
buildifier and whitespace checks pass; no Fast DDS, pre-commit or broad
communication/container sweep was run. Source correction: 5515a02e. The complete module gate passes. Geography/GNSS
remain pending.

## 2026-11-16: Phase 5.2.2 — autoware_geography_utils

Purpose: Supply map projection and WGS84/EGM2008 height conversion for GNSS.
Inputs: package.xml, CMakeLists.txt, all three implementations/public headers,
four existing test files (one main plus 21 cases), and the RCR Lanelet2
extension overlay/module. The published extension 0.7.2-3.rcr.2 exposes the
required MGRS and transverse-Mercator projectors and pins Lanelet2 1.2.1-8.rcr.2.
Reuse its compile/shared providers and rosdistro's GeographicLib provider;
no local extension snapshot or unrelated map-package adoption is required yet.

The height suite has mutually exclusive dataset-present and dataset-absent
cases. A single host-dependent invocation would skip one side. Declare the
real egm2008-1 archive in the package's MODULE with http_archive, official URL
and SHA256 bdb382d0be7ece9142450eacc24b7b7f0889ee3e0ba4f535b04ec383f94c0fb5.
Its geoids/egm2008-1.pgm file is exposed as geoid_grid for tests and later
product consumers. The downloaded archive is 162388303 bytes; keep it in Bazel's
external cache, never git or an apt-installed host resource directory.

Build the original four test sources into one executable using their own main.
Run all 19 non-missing-grid cases with GEOGRAPHICLIB_GEOID_PATH pointing at the
declared grid's runfiles directory. Run the two missing-grid cases separately
with that environment forced to an empty TEST_TMPDIR directory. Filters select
the mutually exclusive cases across both targets; their union must execute all
21 cases with no dependency-related skips. Preserve the original assertions
and test sources. The small shell launcher forwards GTest XML to Bazel and
finishes by exec; no custom test generator or production resource wrapper is
needed. Library sources and scoped include paths stay package-owned.

Verification command (executed):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_geography_utils//...
```

The initial invocation additionally used --distdir="$task_geo_distdir" to
reuse the downloaded, checksum-verified official archive. Without a preseeded
archive, Bazel fetches the declared URL. The normal command above also passes
after the shared-provider rename recorded in the GNSS entry.
The current experiment's download directory is temporary; no developer path is
part of a committed build definition. Require full library compilation and all
21 cases across both XML reports without failures/errors/skips.

Result: the full module surface builds and all 21 cases execute successfully:
19 with the declared grid and two with the deliberately empty grid path. GTest
XML confirms no failures/errors/skips. The complete invocation takes 50.439
seconds using the preseeded official archive and cached RCR dependencies. The
GeographicLib and Lanelet2 extension providers link without source patches or
host dependency installation. The published extension 0.7.2-3.rcr.2 and its
Lanelet2 1.2.1-8.rcr.2 inputs are now validated for this boundary. Targeted
buildifier, shell syntax and whitespace checks pass. No Fast DDS, pre-commit,
broad communication matrix or container rerun was performed. GNSS poser is the
remaining Phase 5.2.2 package gate.

## 2026-11-16: Phase 5.2.2 — autoware_gnss_poser

Purpose: Integrate the final localization slice's GNSS projection node, with
real geoid conversion and the existing runtime/characterization suites.
Inputs: package.xml, CMakeLists.txt, source/header, config/launch files and both
existing test files. Their complete gate is 44 cases (13 node plus 31
characterization), including a real EGM2008 case that would skip without data.
Both supply global main and initialize their ROS context; retain separate
executables with gtest rather than introducing another runner. CMake omits
both suites in enabled builds; keep the full RCLCPP gate and enabled standalone
behavior separately.

Pattern: one node cc_library/shared component, the existing switchable-main
adapter with executable name gnss_poser, and ament resources plus lib/<package>
entry. Compile with geography, wrapper, message/TF and narrow Boost.CircularBuffer
providers. Reuse geography's shared closure under the production-facing name
runtime_dynamic_deps (rename its test-specific label now that a consumer needs
it). Its geoid_grid alias supplies data without exposing the private external
repository name to root or GNSS consumers.

Wrap each existing test executable with a tiny test-owned shell launcher that
sets GEOGRAPHICLIB_GEOID_PATH to that declared grid and forwards GTest XML.
Run every original case, without filters or dataset-related skips. The
characterization source's GNSS_POSER_CONFIG_DIR macro points at
share/autoware_gnss_poser/config, supplied by its own ament_index in runtime
data. This is a package-relative resource convention, not a metarepository
source path; it avoids embedding a developer checkout or canonical repository
name. Keep the private src include path only on tests.

Standalone checks load the real YAML, selecting motion-derived orientation for
a deterministic single-fix scenario. Publish a transient-local LocalCartesian
map origin and an EGM2008 vertical datum, then valid NavSatFix samples at that
origin. Require map-frame pose with zero horizontal displacement, nontrivial
geoid height correction, unit orientation, configured covariance and timestamped
true gnss_fixed, followed by normal SIGINT exit. Run both actual backends with
ordinary ROS peers and native-child-only heap preload. Add only NavSatFix and
its declared type closure to existing sensor plugin selection; the compile/shared
sensor providers already exist, so unrelated interfaces need not be added.

Verification commands (executed sequentially):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_geography_utils//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results --test_output=errors \
  --local_test_jobs=1 @autoware_gnss_poser//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:gnss_poser_rclcpp_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:gnss_poser_agnocast_test
```

Result: the complete module builds, and all 44 original cases execute and
pass without failures/errors/skips, including the EGM2008 characterization
case. GTest XML confirms 13 and 31 cases. The final existing-suite invocation
takes 42.094 seconds; characterization runs for 25.0 seconds and the node
suite for 0.7 seconds. The renamed geography provider also passes its full
21-case regression in 3.986 seconds with the normal cached-input command.

The first characterization compile treated the config macro as identifiers:
Bazel flag tokenization consumed unescaped quotes around the path. Preserve
escaped quotes in the BUILD defines value, e.g.:

```starlark
defines = ['GNSS_POSER_CONFIG_DIR=\\"share/autoware_gnss_poser/config\\"']
```

The resulting C++ macro is a string literal and the unchanged test loads the
YAML from its ament-index runfiles. No source workaround or checkout-specific
path is needed. Both standalone checks pass in 1.5 seconds each, including
real grid-backed height correction, pose/covariance/fix status, backend startup
assertions and normal SIGINT exit. The RCLCPP invocation builds the two added
NavSatFix/Status bridge sources (22.013 seconds total); existing plugin objects
remain cached because the sensor compile/shared provider selection is unchanged.
The enabled invocation then takes 3.711 seconds total. The final
15-target build also passes in 3.752 seconds after marking the two test binaries
test-only; their tested source/runtime configuration is unchanged.

Targeted buildifier, Python syntax, shell syntax and whitespace checks pass.
No production GNSS source fix or additional dependency version was required.
No Fast DDS, pre-commit, broad communication matrix or container sweep was run.
This completes Phase 5.2.2: 182 C++ cases across five packages plus eight
node/launch assertions pass, with no dependency-related skips. EKF's launch
readiness source correction remains isolated in 5515a02e; all other production
sources are unchanged. Phase 5.2.3 point-cloud sensing/CPU perception is next.

## 2026-11-16: Phase 5.2.3 — autoware_point_types

Purpose: Establish point layouts and PointCloud2 field/memory compatibility
before adopting point-cloud nodes. Inputs: package.xml, CMakeLists.txt, both
public headers and both existing test files. The complete gate is 33 cases:
11 point/equality/classification/wrapper and 22 memory/field cases. Neither
source supplies main or contains a disabled/skipped case by inspection.

Pattern: a header-only package-local cc_library with scoped includes, direct
sensor interfaces and point_cloud_msg_wrapper, and the pinned rosdistro's narrow
pcl_common target. The public types include PCL definitions; this first target
does not need broad pcl_ros or the all-PCL shared provider. Select published
point_cloud_msg_wrapper 1.0.7-6.rcr.2 at the root, keeping the component dependency
at 0.0.0. The inspected RCR overlay exports the wrapper as a header-only library
with sensor interfaces. Preserve both existing suites as separate cc_test
executables using gtest_main; collect sensor_msgs:cc_shared at their final
link boundary. No new ROS context or middleware matrix is needed for this
layout/memory behavior. Treat pins and ABI assumptions as pending until tested.

Verification command (executed with sequential tests):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_point_types//...
```

Result: both complete suites build and all 33 cases pass (11 plus 22),
confirmed in GTest XML without failures/errors/skips or disabled cases. The
invocation completes in 19.718 seconds; both tests execute rather than reusing
cached test results. The header-only library, PCL common and published wrapper
work with the pinned toolchain and sensor providers without a source patch or
broad PCL dependency. This validates the wrapper 1.0.7-6.rcr.2 selection.
Targeted buildifier and whitespace checks pass. No CUDA target, host dependency
install, Fast DDS, pre-commit, broad communication matrix or container rerun is
needed for this module. Debug utilities are the next package gate.

## 2026-11-16: Phase 5.2.3 — autoware_utils_debug

Purpose: Supply processing-time, debug and published-time instrumentation for
crop-box and later point-cloud nodes. Inputs: package.xml, CMakeLists.txt,
time_keeper.cpp, all public headers, five existing case files and main.cpp.
The complete suite has six cases: two traits cases and four instantiation
cases, under its existing ROS-context environment/main. Preserve the suite's
actual coverage; the later filter-node behavior gates exercise a runnable
consumer. No case is disabled or skipped by inspection.

Pattern: one compiled package-local cc_library with scoped headers and direct
system utility, fmt, RCLCPP, diagnostic, standard and internal message compile
providers. PublishedTime comes from autoware_internal_msgs, distinct from the
already selected debug-message package. Select its published 1.16.0-1.rcr.0
source at the root; no local interface module is needed for this consumer.
Root metadata inspection identifies the debug utility's published counterpart
as 1.4.2-4.rcr.2; choose that plus the local override. Export a generic
runtime_dynamic_deps collector for final tests and subsequent nodes. Keep
existing main.cpp and gtest, not a second test runner. Source code and context
lifecycle remain unchanged.

Verification command (executed):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_utils_debug//...
```

Result: the compiled source, public headers and full existing suite build;
all six cases execute successfully with no failures/errors/skips, confirmed in
GTest XML. The invocation takes 21.988 seconds and the suite runs for 0.1
seconds. The published internal-message provider supplies PublishedTime and
the debug-message provider supplies ProcessingTimeTree without a local
interface override. This validates both new root selections. No source
compatibility patch is needed. Targeted buildifier and whitespace checks pass.
No Fast DDS, pre-commit, broad matrix or container rerun was performed.
Crop-box filtering is the next runnable package boundary.

## 2026-11-16: Phase 5.2.3 — autoware_crop_box_filter

Purpose: Adopt the first runnable point-cloud filter, preserving its ordinary
RCLCPP backend. Inputs: CMakeLists.txt, package.xml, both source/header pairs,
YAML/launch files and both existing test files. The full suite is ten algorithm
cases plus one pub/sub/TF integration case. Both files have global main; the
integration main owns ROS init/shutdown. No case is disabled or skipped.

Pattern: separate algorithm and node cc_library targets, then the shared
component. Reuse RCR rclcpp_components_register_node for its normal executable
and component record; no Agnocast wrapper or new executor is required by these
sources. Register resources, component metadata and lib/<package> executable
in ament_index. Keep src includes test-private. The algorithm uses Eigen,
geometry/sensor messages and TF Eigen; the node adds debug/system/TF utilities,
RCLCPP components and parameter interfaces. Reuse the debug utility's exported
runtime closure and include geometry's compiled message/TF shared providers.
The package manifest lists point types, but these source files do not include
it; do not invent a compile edge for an unused manifest dependency.

Keep both original suites with gtest and their own main. The existing integration
case exercises non-identity static transforms and output points. A test-owned
Python source, composed at root under bazel/tests, launches the actual ordinary
component executable with its shipped YAML. Send two XYZIRC points (one inside,
one outside the configured box), require the exact kept-point bytes/header,
16-point polygon and published-time metadata carrying the input stamp, then
normal SIGINT exit. Use a best-effort cloud subscriber to match SensorDataQoS;
polygon and timestamp topics use their normal reliable subscriptions. Poll
until all outputs arrive rather than sleeping for a fixed discovery interval.
No heap hook or bridge runtime is needed for this RCLCPP-only node.

Verification commands (executed sequentially):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_crop_box_filter//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local //bazel/tests:crop_box_filter_test
```

Result: the full module surface builds and all eleven existing cases execute
successfully, confirmed in GTest XML with no failures/errors/skips. The initial
module invocation takes 14.268 seconds. The standalone case passes in 0.9
seconds (2.874-second final invocation), checking the actual shared component
executable, shipped crop_box_filter_node.param.yaml, exact kept-point bytes,
frame/stamp, 16-point polygon, published-time metadata and normal SIGINT exit.
No source compatibility patch or additional PCL provider is needed.

Initial standalone setup failed before launching the node: Python's C
typesupport loader found sensor_msgs but could not locate its std_msgs C
introspection dependency. Both shared files were present under the declared
runfiles lib directory. Set LD_LIBRARY_PATH=lib on the root py_test so the
loader sees that prefix from process startup; do not preload Agnocast, install
host ROS, or patch message sources. This preserves the ordinary RCLCPP node's
actual middleware and avoids an unnecessary runtime wrapper. Targeted
buildifier, Python syntax and whitespace checks pass. No Fast DDS, pre-commit,
broad matrix or container rerun was performed. Downsampling is next.

## 2026-11-16: Phase 5.2.3 — autoware_downsample_filters

Purpose: Integrate voxel-grid and random-sample filtering as two ordinary
RCLCPP executables/components. Inputs: package.xml, CMakeLists.txt, four source/
header pairs, both YAML files/launch resources, both existing test files and
RCR PCL conversion/TF overlays. The complete existing suite is six algorithm
and six pub/sub integration cases. The integration source supplies global main;
the core source needs gtest_main. No disabled or skipped case was found.

Pattern: package-local compiled library matching the existing CMake source set,
shared component with two registrations/executables, combined ament records
and resource index. Source needs PCL filters/conversions, pcl_ros transforms,
TF Eigen, debug/system/TF utilities and tl::expected. Published ROS tl_expected
metadata has only a source stub; use rosdistro's pinned native tl_expected
provider rather than attempting a host package install. Keep source unchanged
until compilation exposes any required compatibility work. Select the current
RCR pcl_conversions 2.10.0-1.rcr.0 and pcl_ros 2.10.0-1.rcr.1 at the root.
Final executables/tests use their shared providers, which own PCL and its
native dependencies consistently. CUDA targets are not selected.

Standalone checks launch each actual executable with its shipped YAML and
match cloud output using best-effort subscriptions. Feed three XYZIRC points:
two share a voxel and one is separate. Voxel output must contain the two
expected centroids independent of output order. Random output uses sample_num=1
and must be one original point (no assumption about which sample is chosen).
Supply its input_frame/output_frame parameters as base_link, matching normal
launch composition; these required parameters are not in the YAML. Both cases
require frame/stamp and published-time metadata plus normal SIGINT exit.
The root py_tests set LD_LIBRARY_PATH=lib for declared C introspection libs,
without Agnocast preload or bridge dependencies.

Verification commands (executed sequentially):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_downsample_filters//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local //bazel/tests:voxel_downsample_filter_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local //bazel/tests:random_downsample_filter_test
```

Result: all twelve existing cases pass (six core and six integration), with
GTest XML confirming no failures/errors/disabled/skipped cases. Both executable
checks pass: voxel 1.8 seconds, random 1.6 seconds; complete invocations take
32.176 and 6.155 seconds respectively. The successful module invocation takes
81.686 seconds, including the remaining first-use PCL dependency compilation.
Targeted buildifier, Python syntax, package XML and whitespace checks pass.

The first build fails before tests because the legacy tl_expected/expected.hpp
include is not provided by the native expected library. Change only this
package's include to tl/expected.hpp and manifest dependency to libexpected-dev,
matching AWF core PR #1539 (actual patch and main reviewed 2026-11-16). No host
package is installed. Under the pinned C++23 toolchain the original unexpected
error returns compile and the suite passes; the PR's additional make_unexpected
changes are unnecessary here. UPSTREAM_AWF.md records the exact association.

The first PCL shared closure also brings Qt/VTK visualization dependencies from
the published provider, despite these targets selecting CPU point filtering.
Its first invocation takes 595.048 seconds and fails at our legacy include;
subsequent invocations reuse the prepared dependencies. Native CUDA targets
are not selected. Keep heavy builds sequential with user.bazelrc limits. No
Fast DDS, pre-commit, full communication matrix or container rerun was performed.
Next: prepare the test helper needed by ground filtering, then integrate the
complete ground-filter module.


## 2026-11-16: Phase 5.2.3 — autoware_test_utils

Purpose: prepare ground filtering's existing test dependency without a host ROS
installation. Inspected its CMake sources, three test files, runtime resources,
headers and snapshot executable. RCR metadata for autoware_test_utils returns
404; use a local-only module rather than inventing a registry counterpart.
Select published autoware_adapi_v1_msgs 1.9.0-4.rcr.2 for the actual OperationModeState
includes. The gate is all 21 existing cases (six parser, fourteen helper and one
relay-manager), plus snapshot-executable service/file behavior.

Pattern: compile both existing library sources, expose their two public headers,
use native yaml-cpp/Boost serialization and the published Lanelet extension,
and collect the existing ROS shared providers. Keep config/maps/test-data/rviz
in an ament index and register the executable. The optional visualization.hpp
header is separate from this compiled API: no source or existing test includes
it, and its plotting dependency is test-only in package.xml. Defer that unused
header until a plotting consumer requires it; do not omit any existing test.
Tests use gtest_main and the indexed resources, including parser fixture YAML.

Verification: full module suite under Cyclone DDS and a root behavior
test that launches the actual snapshot executable with shipped configuration,
publishes odometry, calls its Empty service, validates saved YAML and clean
SIGINT exit. Build/test invocations are sequential and honor user.bazelrc.


```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_test_utils//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local //bazel/tests:topic_snapshot_saver_test
```


Result: all 21 existing cases pass without failures/errors/disabled/skipped
cases, confirmed in GTest XML; the test process exits normally. Final module
invocation: 13.084 seconds, test 0.5 seconds. The snapshot case passes in 1.6
seconds (24.295-second invocation), validating the real shipped sample config,
service response, format version, map URI, odometry frame/stamp/position, other
configured output fields, and clean SIGINT exit. Run the child in a temporary
working directory for its generated YAML and make LD_LIBRARY_PATH absolute
before changing cwd; the RCR ament library uses declared runfiles resources.
The root explicitly imports the RCR pip_ros extension for pinned PyYAML rather
than relying on a host pip installation.

Diagnosed failure: all 21 assertions originally passed but the process ended
with SIGSEGV. GDB shows static default-context destruction calling logging
shutdown after spdlog registry lifetime. Only the relay test initializes ROS,
and it never shuts down. Give that test a fixture with SetUp init and TearDown
shutdown, so normal completion and failed assertions clean up before static
exit. The entire suite then passes with clean exit. AWF main still lacks this
fix; core PR #1181 applies the same fixture pattern to a different package,
so it is related rather than a matching fix. UPSTREAM_AWF.md records the exact
source blob, scope and backtrace. No production helper behavior was changed.
The initial dependency preparation/module invocation takes 254.656 seconds;
consolidate that setup failure into this final result, not a separate milestone.
Targeted buildifier, Python syntax and whitespace checks pass. No Fast DDS,
pre-commit, container or full system matrix was run. Ground filtering is next.


## 2026-11-16: Phase 5.2.3 — autoware_ground_filter

Purpose: integrate CPU ground removal with its actual ordinary RCLCPP component.
Prerequisites: prepared PCL providers and fully validated autoware_test_utils.
Inspected CMake, manifest, three sources, public/private headers, YAML/launch
resources and all three test files. Gate: 14 algorithm, five sanity and five
integration cases (24 total), including radial/grid behavior, exact/approximate
indices synchronization and invalid-input rejection, plus a real executable
cloud/published-time/shutdown check. No disabled/skipped cases were found.

Pattern: one compiled library, shared component/standard registration, ament
resource index/executable alias; reuse package-owned shared dependency providers.
Tests retain their existing mains (integration uses gtest_main), include src
privately, and carry both ground-filter and test-helper ament resources. Use
pinned tf2_sensor_msgs 0.45.10-1.rcr.0, existing PCL conversion/TF libraries and
native PCL providers; no host ROS packages or CUDA are selected. The existing
rcpputils expected header remains available in the selected release, so no
expected-header transition is required by this package.
Verification commands (sequential):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_ground_filter//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local //bazel/tests:ground_filter_test
```

Result: all 24 existing cases pass with no failures/errors/disabled/skipped
cases, confirmed in GTest XML (14 algorithm, five sanity, five integration).
Final module invocation: 29.746 seconds, tests 0.5/0.5/1.6 seconds. Standalone
passes in 1.8 seconds (13.325-second invocation). Load the shipped vehicle and
filter YAML, override radial mode, base_link frames and 15-degree slopes as in
the existing deterministic integration fixture. Feed four XYZIRC points along
one ray: two ground and two elevated obstacles. Require only the two expected
obstacles, original frame/stamp, PublishedTime stamp and normal SIGINT exit.

Failures and fixes: grid.hpp uses unavailable float-specific math macros;
derive float pi, half-pi and quarter-pi from the existing project constant,
preserving precision/arithmetic. The next compile exposes raw-profile arguments
to the two message-filter subscriptions; pass the equivalent SensorDataQoS
object for rclcpp major >=32 and retain the older profile path. The complete
suite exercises the changed exact/approximate synchronization paths; older
ROS configurations remain unverified. Inspected AWF main and repository-scoped
PR searches on 2026-11-16; no matching fix found. UPSTREAM_AWF.md records the
source blobs, exact errors and limitations. No expected-header patch is needed.
Targeted buildifier, changed-line clang-format, Python syntax and whitespace
checks pass. No Fast DDS, pre-commit, container or broad matrix rerun was run.
Next: CPU Euclidean clustering/object detection.


## 2026-11-16: Phase 5.2.3 — autoware_euclidean_cluster_object_detector

Purpose: build CPU Euclidean clustering and both ordinary RCLCPP node variants.
Inspected CMake, all four source files, conversion/algorithm/node headers,
config/launch and five test files. Complete gate: nine voxel-clustering,
four standard-clustering, four conversion, five node and three integration
cases (25 total), plus both executable behavior checks. Tests include size
limits, empty/degenerate inputs, centroids and existing parameter-update
behavior. No test disables/skips are declared.

Pattern: one package-owned compiled library/shared component with two standard
registrations/executables and an ament resource index. Use existing PCL filter,
segmentation/search/kdtree targets, conversion provider, debug/diagnostics/system
utilities and perception/geometry/sensor messages. Each original test source
gets a separate target preserving its main; only integration needs gtest_main.
Point types are a real test dependency, not a production include. No new host
package, CUDA or middleware conversion is required.

Verification commands (sequential):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_euclidean_cluster_object_detector//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local //bazel/tests:euclidean_cluster_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local //bazel/tests:voxel_cluster_test
```

Result: all 25 existing cases pass, confirmed in GTest XML with no failures,
errors, disabled or skipped cases. Module invocation: 46.844 seconds. Both
standalone cases pass in 1.7 seconds (14.024-second standard and 4.607-second
voxel invocations). No source compatibility patch or new dependency pin is
needed. Reuse the prepared PCL closure; do not add autoware_utils_pcl merely
because the node uses PCL directly.

Standalone pattern: launch each real component executable with its own shipped
YAML, without overriding the clustering defaults. Publish 24 XYZ points in two
well-separated groups of twelve, meeting each YAML's minimum of ten. Check two
detected objects, order-independent original 3D centroids, classification
probabilities, frame/stamp, all 24 points in the debug cloud and clean SIGINT
exit. The voxel implementation clusters flattened centroids but reports original
point centroids; this test preserves that behavior, not a new algorithm policy.
Polling requires both normal and debug outputs, so discovery cannot silently
omit the optional subscribed debug path. Root Python tests use LD_LIBRARY_PATH=lib
and no Agnocast preload for these ordinary RCLCPP components.

Targeted buildifier, Python syntax and whitespace checks pass. No Fast DDS,
pre-commit, container or full matrix rerun was performed. Object conversion is
next; its existing six cases and an executable round-trip will close this slice.


## 2026-11-16: Phase 5.2.3 — autoware_perception_objects_converter

Purpose: make the detected-to-predicted converter runnable with Bazel. Inspected
its one compiled source, conversion/component headers, launch XML and complete
six-case conversion suite with its own global main. Existing cases cover header,
exact field mapping, both twist branches and deterministic/default UUID behavior.
Gate: all six existing cases plus actual executable pub/sub and clean shutdown.

Pattern: package-local library/shared component and standard RCLCPP registration,
ament launch resources and executable alias. Reuse the existing UUID utility,
perception/geometry/UUID messages and RCLCPP shared providers; no new version pin
or host dependency is introduced. The root selects and composes the module.
Standalone behavior sends two detected objects with different has_twist flags,
requires header/shape/classification/pose mapping, the appropriate twist/default,
empty prediction paths and nonzero distinct UUIDs.

Verification commands (sequential):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_perception_objects_converter//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local //bazel/tests:perception_objects_converter_test
```

Result: all six existing cases pass, confirmed in GTest XML without failures,
errors, disabled or skipped cases. Full module invocation: 11.162 seconds,
test 0.1 seconds. Standalone passes in 1.3 seconds (11.791-second invocation),
including normal SIGINT exit. No source patch or setup failure is needed.
Targeted buildifier, Python syntax and whitespace checks pass. No Fast DDS,
pre-commit, container or full communication-matrix rerun was performed.

Phase 5.2.3 is complete at its package gates: point types 33, debug utilities
six, crop box eleven, downsampling twelve, shared test helpers 21, ground
filtering 24, CPU clustering 25 and object conversion six = 138 existing C++
cases, plus eight standalone checks. These are the recorded sequential module
results, not a new aggregate system sweep. Ordinary RCLCPP backend behavior is
preserved. The unused PCL utility and optional test-helper visualization header
remain outside this slice until a real consumer needs them. Phase 5.2.4 starts
with map loading and projection prerequisites; CUDA stays in Phase 6.


## 2026-11-16: Phase 5.2.4 — autoware_component_interface_specs

Purpose: prepare the projection loader's actual domain/QoS header dependency.
Inspected its CMake, manifest, all domain/utility headers, two producer tools,
committed manifest/hash fixtures and all tests. RCR metadata returns 404 for
this package and the projection loader; use local-only modules. Gate: all
28 current-platform domain/concept/manifest/hash cases plus two C++17 consumer
cases (the conditional no-hash-API skip case is not selected on Lyrical).
Enable AUTOWARE_CIS_CHECK_TYPE_HASHES=1 to execute the existing opt-in freshness
gate rather than accepting its default skip.

Pattern: scoped header library over declared message targets and RCLCPP/RMW,
package-owned shared dependency provider, both producer binaries, tests with
declared tool/fixture data and rootpath compile definitions. The C++17 test
keeps its explicit language mode and original #error guard; other targets use
the pinned C++23 default. Retain the committed Jazzy hash fixture initially:
if it fails, inspect the exact type/schema differences before deciding a fix.
No runtime node is introduced by this header-only package.

Verification:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_component_interface_specs//...
```

Result: all 30 cases pass with no failures/errors/disabled/skips, confirmed in
GTest XML of the two declared targets (28 primary and two C++17). Both producer
tools build and execute via the original tests. The original committed manifest
and Jazzy type hashes match this pinned Lyrical interface graph; no refresh or
source compatibility patch is needed. The C++17 source's original #error guard
passes with target copts -std=c++17 while generators and other cases retain
C++23. This is consumer language validation, not a Humble/Jazzy runtime claim.
Final invocation: 8.993 seconds; tests 0.6/0.1 seconds.

Build declaration know-how: include the existing spec_test_utils.hpp in test
sources, and escape quotes around rootpath-based C++ macro definitions; Bazel
tokenization otherwise removes the C++ string quotes. Generator and committed
fixtures must also be declared as test data. Inspect XML only for current test
targets, since the shared output cache can retain logs for unrelated old labels.
Targeted buildifier and whitespace checks pass. No Fast DDS, pre-commit,
container or communication-matrix check was run. Next: map projection loader.


## 2026-11-16: Phase 5.2.4 — autoware_map_projection_loader

Purpose: run map projection publication from YAML or Lanelet2 metadata. Inspected
three sources, public/node headers, CMake, parameters/launch and every existing
test. Gate: eleven YAML/selection C++ cases, three Lanelet metadata C++ cases,
all four original ROS launch suites (live assertion and clean-exit assertion),
and standalone publication with both RCLCPP and enabled Agnocast. Host device
and module were verified outside the sandbox: /dev/agnocast exists, version 2.4.1
matches running kernel 7.0.0-43-generic. No Fast DDS is selected.

Pattern: keep pure parser sources separate from the wrapper-derived node;
public core header target and core shared closure let parser tests run without
kernel/heap-hook requirements. Node adds the validated specifications headers,
wrapper shared provider and standard switchable launcher/plugin/index support.
Register config/launch/test fixtures in the ament index. Original launch tests
run unchanged with ENABLE_AGNOCAST=0; use a small explicit standalone probe for
the enabled backend, preloading only its child and selecting the root bridge
runtime. MapProjectorInfo already belongs to the top-level interface closure.
Verification commands (sequential):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_map_projection_loader//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local \
  //bazel/tests:map_projection_mgrs_launch_test \
  //bazel/tests:map_projection_local_launch_test \
  //bazel/tests:map_projection_local_cartesian_utm_launch_test \
  //bazel/tests:map_projection_transverse_mercator_launch_test
for backend in rclcpp agnocast; do
  env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
    bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
    --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
    //bazel/tests:map_projection_${backend}_test
done
```

Result: all fourteen C++ cases pass without failures/errors/disabled/skips,
confirmed in current-target GTest XML. All four original launch suites pass
(two cases each: live output and clean exit), plus both standalone probes.
Module invocation: 18.419 seconds; launch invocation 25.209 seconds, each suite
2.8–2.9 seconds. Standalone RCLCPP 0.7 seconds (2.043-second invocation) and
Agnocast 1.2 seconds (3.540-second invocation). Enabled mode uses the built heap
hook and root interface bridge with an ordinary rclpy transient-local peer;
assert projector type, datum, complete origin and clean SIGINT exit.

Setup know-how: the pinned Googletest target is gtest (also exporting gmock),
not a separate gmock label. ROS CLI needs lanelet2_map_path:="" for an empty
string; a bare := fails argument parsing before node initialization. Keep the
runtime wrapper's AGNOCAST_TEST_NO_PRELOAD=1 so the Python peer stays ordinary
ROS and only the enabled child preloads the hook. These are test/build setup
corrections, not production patches. Original launch sources remain unchanged.
Targeted buildifier, Python syntax and whitespace checks pass. No Fast DDS,
pre-commit, container or broad matrix rerun was performed. Next: the Lanelet2
utility library needed by the actual map-loader source.


## 2026-11-16: Phase 5.2.4 — autoware_lanelet2_utils library

Purpose: provide the actual map-loader dependency for map parsing, geometry and
routing. RCR metadata has no published module. Inspected all eleven library
sources/public headers, CMake/manifest and eleven existing test sources plus
helpers, sample maps and YAML fixtures. Scope: compiled utility API and its
complete existing suite, 173 static cases. Plotting example executables and
Python map-maintenance tools are separate consumers and are not introduced by
this library task. No runtime node is defined by this scope.

Pattern: scoped header/compiled library, published Lanelet2 extension/core/io/
routing/traffic rules and existing Autoware geometry/math/message providers;
native range-v3/Eigen/Boost serialization. Ament index declares sample_map and
test_data. Each original suite gets its own target; conversion and nearest-
neighbor search need gtest_main, other files retain their mains. Local test
helpers include the prepared mock parser; carry its shared provider and yaml-cpp
explicitly. Preserve all existing cases. Conversion's defensive GTEST_SKIP
branches follow assertions and must never trigger in the passing gate.
The first build exposes a real dependency gap: published extension 0.7.2 lacks
traffic_rules/autoware_traffic_rules.hpp, which map_handler includes. RCR
metadata confirms 0.7.2-3.rcr.2 is its newest provider. The local extension task
below prepares and validates the current provider before this full gate resumes.

Verification:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_lanelet2_utils//...
```

Result: all 173 existing cases across eleven suites pass with no failures,
errors, disabled or skips, confirmed in XML of exactly the declared test labels.
Conversion's defensive skip branches never execute. Final invocation 16.165
seconds; nearest-search 0.5 seconds and other suites 0.1–0.7 seconds. No new
runtime node or plotting dependency is needed by this compiled-API task.

The first full suite after preparing the extension exposes four nearest-search
ID assertion failures. Temporary diagnostic logging shows returned and expected
lanelets have identical zero distance at overlapping/boundary polygons:
001/P1 2267->2262, 001/P2 2262->2281, 002/P0 2270->2305, 002/P2 2268->2282.
The production comparison has no ID tie-break; different nth_element ordering
is allowed. Correct all nine nearest-search fixture assertions to preserve
count 1, reported distance and actual returned-polygon distance against the known
reference geometry, including positive-distance cases. Do not change production
selection semantics. The temporary diagnostics are removed. AWF main still has
these ID assumptions; PR #839 introduces the feature, not this fix. The dated
source blobs, search scope and evidence are in UPSTREAM_AWF.md.

Targeted buildifier, changed-line clang-format and whitespace checks pass. Source
test correction is committed separately from build definitions. No Fast DDS,
pre-commit, container or full communication matrix was run. The complete utility
API/test gate is closed; map loaders are next. Plotting examples and Python
maintenance tools remain separate consumers outside this compiled-library task.


## 2026-11-16: Phase 5.2.4 — autoware_lanelet2_extension

Purpose: supply current traffic-rule support required by the Lanelet2 utility
library. Prerequisite evidence is its missing-header compile failure against
the newest published 0.7.2-3.rcr.2 extension. Inspected current 1.3.0 snapshot's
CMake/manifest, all 21 library sources, public/private headers and six test files.
Gate: all 19 active existing cases over projector, query, regulatory elements, utilities,
route validation and angle normalization. All tests supply their own main and
construct fixtures in memory; no dependency skip is declared. Source inventory
initially counted 20 TEST tokens, but copyZ is inside an existing block comment;
there are 19 compiled cases, and that dormant test is not activated by this work.

Pattern: retain the published library target names so existing consumers keep
independent labels. Compile current lib sources/headers and link shared ROS,
Lanelet and native providers consistently. Always link the library to retain
factory/projector/regulatory/traffic-rule static registrations. Keep Lanelet
modules pinned to the previously validated1.2.1-8.rcr.2 family. Root selects a
local override; components still refer only to module-owned labels. Tests link
the actual shared extension, not independent embedded copies.

Verification commands (sequential):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_lanelet2_extension//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results --test_output=errors \
  --local_test_jobs=1 @autoware_geography_utils//... \
  @autoware_map_projection_loader//... @autoware_gnss_poser//... @autoware_test_utils//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local //bazel/tests:map_projection_mgrs_launch_test \
  //bazel/tests:map_projection_local_launch_test \
  //bazel/tests:map_projection_local_cartesian_utm_launch_test \
  //bazel/tests:map_projection_transverse_mercator_launch_test \
  //bazel/tests:topic_snapshot_saver_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:map_projection_rclcpp_test //bazel/tests:map_projection_agnocast_test \
  //bazel/tests:gnss_poser_rclcpp_test //bazel/tests:gnss_poser_agnocast_test
```

Result: all 19 active extension cases pass with no failures/errors/disabled/skips,
confirmed in XML of its six declared targets. Final extension invocation 9.013
seconds. The selected complete consumer suites also pass (geography 21 including
both grid states, projection 14, GNSS 44 and test helpers 21 = 100 cases); invocation
74.527 seconds. All four original projection launch suites pass again, as does
the snapshot executable (27.400-second invocation). Both GNSS and projection
standalone backends pass (four cases, 19.454-second invocation). This checks the
actual changed shared provider without expanding to a full DDS/system matrix.

Initial extension test links fail with unresolved Lanelet map-layer symbols:
test sources call Lanelet directly, so declare its shared providers directly
alongside the extension rather than relying on transitive ELF dependencies.
No source compatibility fix is needed. Targeted buildifier/whitespace checks
pass. No Fast DDS, pre-commit or container check was performed. Resume the
incomplete utility gate against this prepared provider.


## 2026-11-16: Phase 5.2.4 — autoware_map_loader

Purpose: run both point-cloud and Lanelet map loaders using the prepared
extension/utility/specification/geography providers. Inspected all eleven C++
sources/private/public headers, CMake/manifest, YAML/launch resources, twelve
existing C++ suites (55 cases) and two original launch suites. Gate: all 55 cases
with their current RCLCPP test contract (including node/service tests), both
original launch suites, and relevant point-cloud/Lanelet executable checks in
both backends. No existing case disables/skips were found. The separate legacy
external-API map_hash_generator Python node belongs to system/API composition
in Phase 5.3; it is not required by these two C++ executables.

Pattern: package-owned compiled library/shared component and two switchable
executables/records. Reuse actual Lanelet/geography providers and native PCL
common/io/filter plus conversion, yaml-cpp and fmt. All test resources are ament
data. The private test include root include/ also resolves existing ../src
includes without source copies or checkout symlinks. Each suite retains its
own main; cylinder-overlap and Lanelet integration need gtest_main. Set
ENABLE_AGNOCAST=0 on the full existing C++ suites (their original CMake contract)
and validate enabled behavior separately with the real hook/bridge runtime.
Verification commands (sequential):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_map_loader//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local //bazel/tests:map_loader_lanelet_launch_test \
  //bazel/tests:map_loader_pointcloud_launch_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:map_loader_pointcloud_rclcpp_test \
  //bazel/tests:map_loader_lanelet_rclcpp_test \
  //bazel/tests:map_loader_pointcloud_agnocast_test \
  //bazel/tests:map_loader_lanelet_agnocast_test
```

Result: all 55 C++ cases execute successfully with no failures/errors/disabled/
skips, confirmed in XML of the twelve declared targets (87.441-second module
invocation). Both original launch suites pass unchanged: one Lanelet clean-exit
case and three PCD metadata/no-whole-topic/clean-exit cases; invocation 19.605
seconds. All four standalone cases pass (26.505-second invocation): Lanelet
RCLCPP 1.9/Agnocast 2.1 seconds, point-cloud RCLCPP 2.0/Agnocast 2.4 seconds.
No source compatibility patch or additional native dependency pin is required.

Standalone know-how: write a temporary three-point ASCII PCD and concrete
parameters (the deployment YAML contains launch substitutions). Require whole
cloud and single-cell metadata, fetch the cell via differential service, then
repeat with cached_ids and require no reload/removal. For Lanelet, use the
shipped test_map.osm, publish MGRS/WGS84 projector info, enable selected loading,
and require both full serialized map and single-cell metadata. Fetch its cell
through GetSelectedLanelet2Map and verify nested map/header data. Both modes
check map frame and clean SIGINT exit, using ordinary transient-local rclpy
peers. Enabled children preload the built heap hook and use the existing root
bridge graph, including these map services. No extra interface list is needed.
The original C++ suite remains the full RCLCPP contract; none of its conditional
node/service groups is omitted.

Targeted buildifier, Python syntax and whitespace checks pass. No Fast DDS,
pre-commit, container or full matrix was run. Phase 5.2.4 remains open for height
fitting, visualization and then map-dependent localization prerequisites.


## 2026-11-17: Central interface dependency providers

Purpose: remove the repeated interface-package registry from Agnocast product
assembly before continuing Phase 5.2.4 height fitting. Inspected the existing
interface aspect/provider, source renderer/plugin macro, both BUILD declarations
and pinned RCR shared-dependency collector/aspect implementation. Current scope
is the existing selection: 22 roots over 21 compiled/shared package providers.
Height-fitting additions are kept separate; no new interface is selected here.

Integration pattern: declare each package once through
ros_interface_package(cc, shared, roots=None) in the central BUILD. Default roots
select the package's cc closure; diagnostic and sensor entries retain their
specific original roots. Both providers are explicit labels, supporting different
package layouts without guessing sibling targets from names. The set macro
creates a private RCR shared-dependency aggregate and declares all dependencies
before analysis. Its rule retains the metadata aspect/manifest, merges CcInfo
with cc_common.merge_cc_infos, forwards the aggregate CcSharedLibraryInfo and
propagates runtime runfiles. The manifest remains the public default output.

The Agnocast macro consumes the set as a C++ dependency and a shared dependency,
adding only its own Agnocast/RCLCPP providers. Its BUILD call needs only name and
interface_set. Collection has no dependency on the plugin or Agnocast. Another
compiled consumer can use the same set in deps/dynamic_deps; a metadata consumer
can read RosInterfaceSetInfo. Application-target roots remain supported through
the set's optional roots override and explicitly declared package providers.
No directory-completeness validator or representative interface target is added.

Static verification: buildifier and whitespace checks; a one-time declaration
comparison evaluates the package/set macro's loading-time inputs against the
previous declarations. It confirms the exact same 22 roots and 21 compile/shared
provider pairs, including the narrow selections. An application-root override
check confirms that it leaves the declared providers intact. These checks do not
validate Bazel provider analysis, generated C++ compilation, linking or runtime.

Verification commands, executed sequentially from an unsourced environment:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  //bazel/agnocast:template_tests
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build //bazel/interfaces:all_interfaces //bazel/agnocast:plugins
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --nocache_test_results --test_output=errors --local_test_jobs=1 \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
  //bazel/agnocast:bridge_plugins_test
```

The runtime prerequisite is the built heap hook, compatible loaded kernel module
and device, plus declared product runtime index. Existing bridge-plugin tests
exercise actual loading, standard/local payloads, service replies and service-
event messages.

Result: the template target passes in 0.1 seconds (7.854-second invocation).
Bazel analysis accepts the merged C++ and forwarded shared providers. The full
plugin library compiles and links successfully (444.398-second invocation,
including recompilation against the changed dependency layout). The public
manifest is sorted/unique and contains 207 interfaces: 180 messages and 27 services;
service-event factories are additionally generated as before. No height-fitting
interface is selected by this isolated change.

All eleven existing runtime cases pass in 8.3 seconds (50.181-second invocation),
confirmed in GTest XML with no failures/errors/disabled/skips. This covers actual
BridgeLoader loading, standard/local messages in both directions, service
request/reply, service-event traffic, automatic handoff and discovery. Runtime
runfiles propagation is exercised by the real product index/heap-hook launcher.
The host device and loaded Agnocast 2.4.1 module were available for this gate.
No build/link/runtime correction or source patch was required after the prepared
refactor. Targeted buildifier and whitespace checks also pass. Fast DDS,
pre-commit and a full communication matrix were not run.

This refactor gate is complete. Resume Phase 5.2.4 height fitting separately,
porting its prepared interface additions into the central declaration rather
than restoring consumer-owned package lists.

## 2026-11-17: Wrapper launcher ROS executor selection

Purpose: preserve the first height-fitting consumer's existing multithreaded
ROS executor contract. Inspected its CMake registration and synchronous partial-
map client wait in a separate callback group, plus the wrapper's existing Bazel
launcher/template. Scope: shared launcher build API; no wrapper production C++
source changes. Complete gate: 37 C++ cases in each backend, 66 launch-action
cases, and real height-fitting whole/partial service behavior in both backends.

Pattern: add an optional ros2_executor argument to autoware_agnocast_node and
pass it to the template-expansion rule. Constrain it to SingleThreadedExecutor
or MultiThreadedExecutor. Keep the former as default, so existing callers retain
the same generated executor type. Agnocast retains its callback-isolated
executor. A consumer selects the executor its callback progress requires;
height fitting is the first opt-in caller. No consumer depends on root labels.

Verification (sequential, unsourced caller environment):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --config=no-agnocast \
  --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_agnocast_wrapper//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --nocache_test_results \
  --test_output=errors --local_test_jobs=1 --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_heaphook @autoware_agnocast_wrapper//:unit_tests
```

Result: 37 fallback and 37 enabled C++ cases pass without failures/errors/disabled/
skips in GTest XML. All 66 Python cases pass according to the pytest log (Bazel's
XML wraps that process as one case). Invocations 70.541/11.371 seconds; native
cases 0.4/2.3 seconds and Python 2.3 seconds. The four real height-fitting service
probes also pass, including the partial ROS path that needs callbacks to run
while the service waits; their exact commands and results are in the height-
fitting entry. This validates the new opt-in executor with its actual consumer,
without adding a synthetic demonstration node. Targeted buildifier and whitespace
checks pass. No Fast DDS, pre-commit or broad communication matrix was run.

## 2026-11-17: Phase 5.2.4 — autoware_map_height_fitter

Purpose: expose map-based height fitting through its actual service component.
Prerequisites: completed Lanelet2 utility/map-loader providers and existing QoS
and TF support. Inspected CMake/manifest, all three implementation sources,
public/private headers, deployment YAML and both existing test files. Gate:
fifteen kernel cases (including vector maps, non-finite inputs and reference
comparisons) plus two fitting/translated-frame cases, with the complete original
RCLCPP test contract. Add whole-cloud and partial-map executable probes in both
RCLCPP and Agnocast. No existing test case declares a skip or disable.

Integration pattern: package-local compiled library and shared component with
scoped include root, standard switchable executable/plugin/index and original
config/launch resources. Kernel tests use gtest_main; fitting tests retain their
existing ROS init/shutdown main. Both run with ENABLE_AGNOCAST=0 rather than
omitting the fitting group under the default enabled environment. Runtime probes
use ordinary rclpy peers, child-only heap preload for Agnocast, declared bridge
runtime and bounded readiness/service/shutdown waits.

Current CMake selects MultiThreadedExecutor for the ROS fallback. Preserve that
choice: partial-map fitting waits for a client response in a separate callback
group while serving the fit request. Add a constrained ros2_executor choice to
the existing launcher rule, keeping SingleThreadedExecutor as its default. No
production C++ compatibility or algorithm patch is introduced speculatively.

Select the published autoware_internal_localization_msgs 1.16.0-1.rcr.0 provider
for the concrete PoseWithCovarianceStamped service; the complete module and
service gates below validate this pin. At the root, select that service and the
rcl_interfaces family for
bridge generation, using central ros_interface_package entries. The Agnocast
plugin BUILD declaration remains unchanged after the provider refactor. Source
inspection shows AsyncParametersClient waits for all
six parameter services before fetching enable_partial_load, so exporting only
GetParameters would leave startup waiting. Component modules do not depend on
these root labels.

Standalone probes construct a two-point cloud at the query location with z=5
and z=8, then require fitted z=5, preserved XY/header/orientation/covariance and
normal SIGINT exit. The ROS peer declares the map-loader parameter and exposes
a partial-map service. Whole mode must consume the latched cloud without a
partial request; partial mode must make the expected area request and complete
the fitting service. This last path exercises the multithreaded fallback's
callback progress as well as the enabled service bridges.

Verification commands, executed sequentially from an unsourced environment:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --nocache_test_results \
  --test_output=errors --local_test_jobs=1 @autoware_map_height_fitter//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --config=no-agnocast \
  --nocache_test_results --test_output=errors --local_test_jobs=1 \
  @autoware_agnocast_wrapper//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --nocache_test_results \
  --test_output=errors --local_test_jobs=1 --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_heaphook @autoware_agnocast_wrapper//:unit_tests
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --nocache_test_results \
  --test_output=errors --local_test_jobs=1 --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:map_height_fitter_whole_rclcpp_test \
  //bazel/tests:map_height_fitter_partial_rclcpp_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --nocache_test_results \
  --test_output=errors --local_test_jobs=1 --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:map_height_fitter_whole_agnocast_test \
  //bazel/tests:map_height_fitter_partial_agnocast_test \
  //bazel/agnocast:bridge_plugins_test
```

Result: all seventeen existing cases pass, confirmed in the two declared GTest
XML files with no failures/errors/disabled/skips: fifteen kernel and two fitting
cases. Module invocation 110.064 seconds, each target 0.7 seconds. No production
source compatibility patch was needed. The complete wrapper regressions pass:
37 C++ cases in each backend and 66 Python launch-action cases (pytest log),
with no skips; the separate launcher entry records that boundary.

All four actual-executable probes pass: whole-cloud RCLCPP 2.6/Agnocast 2.2 seconds,
partial-service RCLCPP 2.0/Agnocast 2.0 seconds. They verify fitted z=5, unchanged
XY/header/orientation/covariance, correct partial-area request only in partial
mode, and clean SIGINT exit. The partial ROS probe exercises callback progress
while its fitting service waits for the map response, validating the selected
MultiThreadedExecutor fallback. Enabled probes also exercise the ordinary-peer
parameter-service bridges during startup.

The ROS probe invocation takes 1234.848 seconds, including rebuilding the full
bridge library against the expanded interface-provider closure; this also
builds the new fitting/parameter factories. The following enabled/bridge
invocation reuses that library and takes 17.631 seconds. All eleven existing
bridge-plugin cases pass in 7.3 seconds with no skips, confirmed in XML. The
central manifest now contains 229 interfaces (193 messages, 36 services), including
the selected fitting service and all six parameter services. No package list
was added back to the Agnocast consumer BUILD.

Targeted buildifier, Python syntax and whitespace checks pass. No dependency
omission, skipped test group, deferred failure or source patch was necessary.
No Fast DDS, pre-commit, container or full communication matrix was run. Height
fitting's complete module gate is closed; map visualization is the next package
boundary in Phase 5.2.4, which remains in progress.

## 2026-11-17: Phase 5.2.4 — autoware_lanelet2_map_visualizer

Purpose: turn the existing binary Lanelet2 map into visualization markers using
its actual switchable component. Prerequisites are the validated map loader, local
Lanelet2 extension/utilities and visualization utility. Inspected the manifest,
CMake registration, both implementation/header pairs, launch XML, all three C++
test files and the original launch test. There are seven C++ cases: five marker-
construction cases, one node initialization and one map-publication case. Each
file supplies its own GTest main. The original launch suite checks process exit;
it does not publish projector information or assert marker delivery.

Minimum pattern: add package-local MODULE/BUILD declarations, compile both
sources into a library, link a shared component using existing dynamic providers,
and register the standard launcher, executable index, component metadata and
original launch XML. Keep the CMake executable name and SingleThreadedExecutor
fallback. Declare all three existing test targets with ENABLE_AGNOCAST=0 to
retain the complete original RCLCPP contract. No production source or existing
test is modified. Root composition supplies the original launch test with both
package indexes and the loader's existing map fixture. Component build support
has no root labels.

The ordinary rclpy behavior peer sends MGRS projector information to the real
loader, which publishes the fixture map to the real visualizer. The visualizer
input is remapped to /map/vector_map. Require nonempty map-frame markers,
road-lanelet triangles with vertices, and a left-boundary marker. Stop the loader
before adding a second transient-local marker subscriber, then require the
retained output and clean SIGINT exits. Readiness and shutdown waits are bounded;
child logs are included on failure. Both executable children use the chosen
backend, with heap preload confined to enabled children. Root product assembly
adds only the MarkerArray metadata root and its paired C++/shared providers to
the existing central interface set; the plugin consumer needs no duplicate list.
The generated dependency closure supplies referenced marker types.

Verification, sequential from an unsourced caller environment:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @autoware_lanelet2_map_visualizer//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests:lanelet2_map_visualizer_launch_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --local_test_jobs=1 \
  --nocache_test_results --test_output=errors --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:lanelet2_map_visualizer_rclcpp_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --local_test_jobs=1 \
  --nocache_test_results --test_output=errors --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:lanelet2_map_visualizer_agnocast_test \
  //bazel/agnocast:bridge_plugins_test
```

The enabled gate requires the built heap hook, loaded compatible Agnocast module
and device, and product runtime index, as in the preceding module. Inspect test
XML/logs for all seven original cases, the complete original launch suite, both
behavior checks and eleven bridge cases with no skips. These commands also
build the component, executable, indexes and expanded plugin library. Reuse
passing wrapper results because its implementation/configuration is unchanged.

Result: the component, executable and index build successfully. All seven
original C++ cases pass across three targets (five construction, one node and
one initialization), confirmed in GTest XML without failures/errors/skips.
The module invocation takes 63.111 seconds; target times are 0.2/0.4/0.2 seconds.
The unchanged original launch suite passes its single exit-code case in
3.8 seconds (31.256-second invocation). It validates launcher/index composition,
while actual map delivery is checked separately by the behavior probes.

Both real loader-to-visualizer probes pass, including map-frame road triangles
with vertices, left-boundary markers, retained output for a late subscriber
after stopping the loader, and clean shutdown. RCLCPP takes 2.5 seconds;
Agnocast takes 2.1 seconds. The RCLCPP invocation takes 1134.051 seconds including
compiling/linking the bridge against its expanded interface-provider closure.
The subsequent enabled/bridge invocation reuses that library and takes
74.905 seconds. All eleven bridge-plugin regression cases pass in 7.6 seconds.
Test XML confirms the original launch case, both behavior cases and eleven
bridge cases without failures/errors/skips. The enabled tests use the built
heap hook and loaded Agnocast 2.4.1 module/device. The sorted/unique manifest
contains 234 interfaces: 198 messages and 36 services, including MarkerArray
and its referenced Marker, MeshFile and UVCoordinate types.

No production compatibility patch, existing-test modification, dependency skip
or deferred failure was required. The existing lockfile remains unchanged after
module resolution. Targeted buildifier, Python AST parsing and whitespace checks
pass. No Fast DDS, pre-commit, container or full communication matrix was run.
The complete module gate is closed; Phase 5.2.4 remains in progress. The next
boundary is the NDT prerequisite inventory.

## 2026-11-17: Phase 5.2.4 — autoware_utils_pcl in the NDT group

Purpose: provide NDT's actual point-cloud transformation dependency. Work on
this prerequisite and its NDT consumer is grouped to reduce handoffs, while
completion and eventual commits remain package-scoped. Inspected both public
headers, both existing test files, package manifest and CMake. The complete
suite is seven cases: five conversion/layout/affine/organized/empty-cloud cases
and two transformation/empty-cloud cases. No cases are omitted.

Pattern: add a header-only package-local cc_library with a scoped include root.
Use the existing pcl_conversions, sensor_msgs, RCLCPP and rosdistro PCL common
providers referenced by the headers, with corresponding ROS dynamic providers
on final tests. The two test sources have no main, so use gtest_main. Root
selection pins the published 1.11.0-3.rcr.2 counterpart with a local override.
The headers do not consume the manifest's pcl_ros/autoware_utils_tf packages;
no extra build dependency is needed for an unused include. No source change is
required. NDT's real consumer gate exercises the same header library.

Verification, sequential builds and tests from an unsourced environment:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_utils_pcl//... @autoware_ndt_scan_matcher//...
```

Result: all seven utility cases pass in 0.2 seconds with no failures/errors/
skips in GTest XML. The final combined invocation takes 113.003 seconds and
also passes all 52 NDT cases. The NDT entry records its linker diagnosis and
full executable checks; no utility source correction was necessary. Targeted
buildifier and whitespace checks pass. No Fast DDS, pre-commit or container
rerun was performed. This package gate is complete.

## 2026-11-17: Phase 5.2.4 — autoware_ndt_scan_matcher in the NDT group

Purpose: build and run the actual NDT registration kernel and switchable node,
using the now-integrated point-cloud utility and existing localization, geometry,
diagnostics, logging, QoS and visualization providers. Inspected CMake/manifest,
all kernel/node sources and headers, deployment YAML/XML, original launch test,
three initialization scenarios, characterization harness and three unit suites.
Gate: 52 existing C++ cases across seven targets (3 initialization scenarios,
25 characterization, 8 covariance, 8 helper and 8 map-update cases), both original
launch cases, and real executable behavior with RCLCPP and Agnocast.

Pattern: keep separate compiled kernel and node libraries, a shared component,
standard switchable executable, component/plugin metadata and declared config/
launch index. Preserve MultiThreadedExecutor for the ROS fallback. The kernel
uses the same SSE4.2 instruction contract as CMake on x86_64; node/tests use the
same setting to preserve Eigen data layout. Select libomp from the existing
LLVM module extension pinned to LLVM 21.1.8 and enable -fopenmp on the kernel.
All dependency targets are Bazel providers; no host ROS/PCL/OpenMP installation
is used. Preserve every original C++ case under ENABLE_AGNOCAST=0, including
CMake's conditionally excluded node tests. Own-main files use gtest; the three
unit suites use gtest_main. Declare all harness headers and the YAML/index data.
Keep tests sequential and retain their full long timeout.

Diagnosed link failure: -fopenmp compilation succeeds, but final links report
undefined omp_get_thread_num, omp_get_max_threads and __kmpc_* symbols. The
transitioned LLVM libomp provider supplies compilation context, but does not
participate correctly in the native cc_shared_library graph aspect. Attempting
to make it a direct shared-library root also reports missing GraphNodeInfo.
Fix: add a native cc_import whose static_library is the public libomp target's
declared archive. Keep the original provider for generated omp.h/include paths,
and use the native import for linker ownership. The archive still builds from
pinned LLVM sources with the runtime's own configuration transition. No private
LLVM label, library copy, system search path, new dependency patch or production
source modification is needed. Full tests and both real executables below
validate the fix. Initial label/mapping mistakes were corrected by declaring
platforms explicitly, using the LLVM extension's actual path, generated :cc
interface labels and the wrapper's :ament_package resource target.

The original launch suite includes the unmodified deployment XML. Supply both
NDT's index and the wrapper's package resources so find-pkg-share resolves the
included Agnocast environment XML. The fallback launch verifies activation and
clean exit. The separate behavior peer is ordinary rclpy; only an enabled child
preloads the built heap hook. It provides a differential-map service with a
three-plane half-cube (0.2 m samples, translated by 100 m in XY), publishes the
matching 1 m scan and identity base_link-to-sensor static TF, and activates the
node. Require a map request, successful initial alignment with finite XYZ within
2 m of (100,100,0), successful deactivation and clean SIGINT exit. All waits are
bounded and include child logs on failure. The probe uses 30 initial particles/
trials and one NDT thread; original C++ test parameters remain unchanged.
It checks synthetic-map initialization, not production-map accuracy/performance.

Verification commands (sequential, unsourced caller environment):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_utils_pcl//... @autoware_ndt_scan_matcher//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests:ndt_scan_matcher_launch_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --jobs=1 --local_resources=cpu=1 --local_test_jobs=1 \
  --nocache_test_results --test_output=errors --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:ndt_scan_matcher_rclcpp_test \
  //bazel/tests:ndt_scan_matcher_agnocast_test
```

Result: all 52 existing C++ cases pass with no failures/errors/skips in the seven
declared GTest XML files. Combined module invocation 113.003 seconds; the three
initialization targets take 0.8/0.8/0.5 seconds, covariance 0.5, helper 0.4,
map update 0.4 and characterization 16.2 seconds. All seven utility cases also
pass. Both original launch cases pass in 3.5 seconds (27.840-second invocation).
Both real executable probes pass, RCLCPP in 5.1 and Agnocast in 6.4 seconds
(56.912-second invocation), validating map-service replies, TF/scan inputs,
alignment service, activation/deactivation and shutdown. XML confirms both
launch cases and both standalone cases without failures/errors/skips.

The enabled gate uses the built heap hook, loaded compatible Agnocast module/
device and existing product runtime index. No new central-interface roots or
plugin-source changes are required: all exercised service/message types are
already selected. Reuse the preceding passing bridge/wrapper regressions for
unchanged implementations and configuration; do not rebuild an unchanged full
communication matrix. Targeted buildifier, Python AST and whitespace checks
pass. Module resolution leaves the existing lockfile unchanged. No Fast DDS,
pre-commit or container run was performed. The grouped package gates are
complete, with no skipped group or deferred failure. Pose initialization is next
within Phase 5.2.4.

## 2026-11-17: Phase 5.2.4 — component-interface utilities in the pose group

Purpose: supply PoseInitializer's typed NodeAdaptor and its service/state
registration. Group this header-only prerequisite with motion utilities and
pose initialization, retaining full module gates and separate eventual commits.
Inspected CMake/manifest, all public headers and all four tests plus both
committed manifest fragments. Complete suite: 25 cases (12 wrapper/callback/
introspection, 6 registration, 5 JSON mapping and 2 manifest drift). Preserve
both matching and divergent fragments without refreshing them.

Pattern: package-local header library with scoped includes, existing specs,
ADAPI, RCL/RMW/RCLCPP/component/runtime providers and pinned nlohmann_json.
Declare four cc_tests with gtest_main. Use the specs' existing runtime provider
alongside direct shared dependencies: these tests create ChangeOperationMode
services whose generated type support must actually be available to dlopen.
The initial incomplete closure reported missing system_msgs generator_c/type-
support libraries, then subsequent tests reported context already initialized.
Adding that declared runtime closure fixes the first exception and the cascade;
do not remove the service tests. Manifest fixtures are declared data, passed
through rootpath string macros with the escaping already used by the specs
package. Incorrect macro quoting was a build-definition error, not fixture drift.

Current source fix: six member callback adapters pass a named rvalue-reference
member pointer to std::bind as an lvalue. Under Clang 21.1.8/libc++, RCLCPP traits
cannot match the resulting binder member-pointer-reference type; compiling
member callback tests reports an overloaded operator() trait error. Move the
member pointer into bind in legacy and returning subscription/service forms.
The existing utility include already provides std::move. This does not change
callback signatures, object ownership or pointer invocation semantics. The
complete existing suite verifies pointer/reference subscriptions, service
callbacks, registration and introspection. UPSTREAM_AWF records the dated
tracker/main comparison; keep the eventual source fix commit separate from
Bazel definitions. No other source compatibility change is introduced.

Verification, unsourced environment, sequential tests and configured build limits:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  @autoware_component_interface_utils//... @autoware_motion_utils//... \
  @autoware_pose_initializer//...
```

Result: all 25 cases pass without failures/errors/skips in the four declared
XML files, target times 0.3/0.1/0.1/0.2 seconds. The final grouped invocation
takes 16.632 seconds and also passes all 118 motion and 23 pose cases. This
module gate is complete; executable enabled behavior remains the pose module's
separate gate below. No Fast DDS, pre-commit, container or colcon/ament/CMake build-path check.


## 2026-11-17: Phase 5.2.4 — motion utilities in the pose group

Purpose: provide PoseInitializer's VehicleStopCheckerBase through the complete
motion utility package. Inspected CMake/manifest, ten compiled sources, public
headers and all eleven C++ test files plus the helper header. No new prerequisite
beyond already integrated interpolation, geometry/math/system/visualization and
message providers is required. Complete suite: 118 cases, including vehicle
stop checks, resampling, distances, trajectory/path operations, marker behavior,
overlap characterization and the existing lateral-offset benchmark test.

Pattern: package-local compiled library over src/**/*.cpp and scoped public
headers, explicit compile dependencies on referenced providers, and a final
runtime provider matching the ROS interface/shared dependencies. Preserve
CMake's single complete test binary, whose test_motion_utils.cpp supplies main.
Declare the helper header as an input. No source patch, fixture replacement,
benchmark omission or new solver dependency is needed for this current target.
The VehicleStopCheckerBase constructor already accepts the switchable node
through its existing template; do not alter the stop-check algorithm.

Verification, run as part of the same sequential group:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  @autoware_component_interface_utils//... @autoware_motion_utils//... \
  @autoware_pose_initializer//...
```

Result: all 118 cases pass in 7.2 seconds, confirmed in XML without failures/
errors/skips. The grouped invocation takes 16.632 seconds. The existing pose
integration cases also exercise this library's stop checker. No production
source or existing-test modification is required. No Fast DDS, pre-commit or
container check was run. This module gate is complete.

## 2026-11-17: Phase 5.2.4 — pose initializer

Purpose: complete the map-dependent localization slice with the real switchable
pose initialization node. Prerequisites: the two modules above, existing height
fitter, specs, diagnostics/logging/geometry and ROS interfaces. Inspected all six
implementation sources, headers, CMake/manifest, launch/config and all three
existing test files. Complete C++ gate: 23 cases (3 vector/array, 7 pose utility,
13 node integration including initialization/rejection/recovery/startup cases).
There is no original launch test in this package. Require real executable
DIRECT/AUTO initialization behavior in both backends, plus the expanded bridge's
existing eleven regression cases before declaring completion.

Pattern: package-local compiled library/shared component, MultiThreadedExecutor
ROS fallback and existing Agnocast executor through the standard launcher, plus
executable/component/plugin index and original deployment resources. Use
RCLCPP for the complete original test contract, not CMake's enabled omission.
The pinned GoogleTest module exposes gtest_main (including mock headers) rather
than a separate gmock_main target. The integration test supplies its own main.
Height-fitter/motion runtime providers and diagnostics/logger shared providers
supply their actual dynamic closure. No pose initializer source change is needed.

Root behavior probes use ordinary rclpy peers for NDT alignment and EKF/NDT
trigger services. Require initial UNINITIALIZED state, DIRECT reset with the
original pose, AUTO reset with a simulated 1 m alignment shift, configured
covariance on both, exact deactivate/deactivate/activate/activate trigger order
per request, no DIRECT aligner call, empty-DIRECT failure with UNINITIALIZED
state, and clean SIGINT exit. Readiness, service and teardown waits are bounded.
Only enabled executable children preload the built heap hook. The probe disables
GNSS/stop/error modules to isolate transaction routing; all their original C++
module tests remain included. A generated JSON/YAML parameter file supplies the
concrete standalone settings; installed YAML contains launch substitutions and
is retained for normal launch use rather than parsed as standalone parameters.

Select only the needed ADAPI LocalizationInitializationState metadata root with
its paired C++/shared providers centrally. Published ADAPI uses domain-specific
labels: //localization/msg:LocalizationInitializationState, not //msg. The
initial incorrect label was corrected after analysis rejected it. Keep the
component independent of root labels and leave plugin composition on the root.
This new interface requires regenerating/linking the product bridge; retain its
full regression gate.

Verification:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  @autoware_component_interface_utils//... @autoware_motion_utils//... \
  @autoware_pose_initializer//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:pose_initializer_rclcpp_test \
  //bazel/tests:pose_initializer_agnocast_test //bazel/agnocast:bridge_plugins_test
```

Result: all 23 existing C++ cases pass in 0.5/0.5/6.7 seconds, with no
failures/errors/skips in XML (16.632-second grouped invocation). The enabled
runtime prerequisite is the built heap hook, compatible loaded Agnocast module
and device, verified available at 2.4.1. Both executable probes pass: RCLCPP
2.2 seconds and Agnocast 2.1 seconds. They verify DIRECT and AUTO resets with
the expected X positions and configured covariance, exact trigger ordering,
only one aligner call, expected initialization-state transitions, empty-request
failure and clean SIGINT shutdown. All eleven existing bridge-plugin regression
cases pass in 7.5 seconds; XML confirms both standalone cases and eleven bridge
cases without failures/errors/skips.

The runtime invocation takes 438.156 seconds, including recompiling/linking the
bridge against the added ADAPI compile/shared provider closure. Its sorted,
unique manifest has 235 interfaces (199 messages, 36 services), adding exactly
the initialization-state message. No dependent test group is omitted. Reuse
unchanged wrapper gates from the preceding validated tasks; no full system DDS
matrix is needed for this package integration. Targeted buildifier, Python AST,
clang-format on the changed callback header and whitespace checks pass. Module
resolution records the new nlohmann_json minimum-version lookup in the lockfile;
the existing resolved 3.12.0.bcr.2 provider remains selected. No Fast DDS,
pre-commit, container, Humble/Jazzy or colcon/ament/CMake build was run.

All three module gates are complete, and Phase 5.2.4 is closed. The callback
source fix and the three package integrations use separate commits. Next is the
Phase 5.2.5 planning/control dependency-group inventory; do not pre-apply future
solver or planner patches.


## 2026-11-17: Source selection and package preservation conventions

Require checked-in dependency snapshots, adding missing Bazel declarations
transitively instead of selecting registry sources for convenience. Preserve
original implementations and tests except for demonstrated blockers,
compatibility fixes or genuinely incorrect behavior. Keep new tests, adapters
and fixtures under bazel/tests/<package>/ and expose original tests through
component-owned Bazel rules. AGENTS.md, PLAN.md and DEVELOPING.md define these
conventions. This documentation-only task has no runtime validation; subsequent
module and test-layout entries record their complete acceptance gates.

## 2026-11-17: Local interface adoption — autoware_adapi_v1_msgs

Purpose: use the actual local ADAPI contracts required by specs, command gate,
pose initialization, component-interface utilities and test helpers. Inspected
all 79 registered CMake inputs and their definitions. The local family has no
existing functional suite; its complete declared generation and real consumers
form this gate. Keep domain-specific labels such as
//localization/msg:LocalizationInitializationState and //operation_mode/msg:OperationModeState.
Direct external field types come from builtin_interfaces, geographic_msgs,
geometry_msgs, shape_msgs, std_msgs and unique_identifier_msgs; none has a local
ROS package snapshot. Same-package references resolve to the unique local
message definitions rather than registry-generated types.

Snapshot 1.9.2 declares 61 messages and 18 services.

Integration pattern: declare a package-local module at the snapshot's manifest
version, retain its manifest-level interface dependencies, add explicit
ros_message/ros_service targets in the existing interface directories and call
default_generators at the package root. Each interface target names only its
referenced message dependencies; same-package edges use their actual directory
labels and external edges use the referenced package's message target. Root
local_path_override selects the snapshot; consumers keep their existing labels.
The BUILD declarations are the supported interface list. No source-completeness
validator, generator verb or new message definition is introduced.

Verification: the shared local-provider command below builds every declared
surface in these five modules and runs complete existing consumer suites. The
successful invocation takes 257.564 seconds, builds 217 non-test targets and
passes 24 test targets / 185 XML cases, including unchanged interface manifest/
type-hash freshness and C++17 checks, component-interface utilities, debug
utilities, height fitting, NDT, pose initialization and both native control
packages. No failures/errors/skips occur. Original message files are unchanged.
The relocated root executable/bridge validation is recorded in the relocation
entry. Standard ROS dependencies absent locally still come from pinned RCR.
No Fast DDS, pre-commit, container or
colcon/ament/CMake run was performed.

Shared local-provider verification command (executed sequentially before the
test-source relocation):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  @autoware_adapi_v1_msgs//... @autoware_adapi_version_msgs//... \
  @autoware_internal_debug_msgs//... @autoware_internal_localization_msgs//... \
  @autoware_internal_msgs//... @autoware_adapi_specs//... \
  @autoware_command_gate//... @autoware_simple_pure_pursuit//... \
  @autoware_component_interface_specs//... @autoware_component_interface_utils//... \
  @autoware_utils_debug//... @autoware_ndt_scan_matcher//... \
  @autoware_pose_initializer//... @autoware_map_height_fitter//...
```

The header consumer is now //bazel/tests:adapi_specs_headers_test, replacing
@autoware_adapi_specs//:test_headers from that invocation. Add the root label
when repeating this gate after the relocation; the unchanged original module
suites retain their labels.


## 2026-11-17: Local interface adoption — autoware_adapi_version_msgs

Purpose: satisfy the ADAPI specification's version-service dependency from its
checked-in source. The package contains srv/InterfaceVersion.srv: an empty
request and uint16 major/minor/patch response. Its generators need no external
field-type package. Inspected CMake, manifest and the full definition. The ADAPI
public-header consumer compiles this service against the local generator output;
there is no original functional test suite to omit.

Snapshot 1.9.2 declares one InterfaceVersion service.

Integration pattern: declare a package-local module at the snapshot's manifest
version, retain its manifest-level interface dependencies, add explicit
ros_message/ros_service targets in the existing interface directories and call
default_generators at the package root. Each interface target names only its
referenced message dependencies; same-package edges use their actual directory
labels and external edges use the referenced package's message target. Root
local_path_override selects the snapshot; consumers keep their existing labels.
The BUILD declarations are the supported interface list. No source-completeness
validator, generator verb or new message definition is introduced.

Verification: the shared local-provider command below builds every declared
surface in these five modules and runs complete existing consumer suites. The
successful invocation takes 257.564 seconds, builds 217 non-test targets and
passes 24 test targets / 185 XML cases, including unchanged interface manifest/
type-hash freshness and C++17 checks, component-interface utilities, debug
utilities, height fitting, NDT, pose initialization and both native control
packages. No failures/errors/skips occur. Original message files are unchanged.
The relocated root executable/bridge validation is recorded in the relocation
entry. Standard ROS dependencies absent locally still come from pinned RCR.
No Fast DDS, pre-commit, container or
colcon/ament/CMake run was performed.

## 2026-11-17: Local interface adoption — autoware_internal_debug_msgs

Purpose: remove an older registry source selection for the debug contracts used
by current localization/perception utilities and the bridge. Inspected all
fourteen CMake registrations and field definitions. External fields reference
builtin_interfaces and std_msgs. Declare ProcessingTimeTree's same-package
ProcessingTimeNode edge explicitly. NDT's complete characterization suite and
the debug utility's existing suite consume these local outputs.

Snapshot 1.16.0 declares 13 messages and one String service.

Integration pattern: declare a package-local module at the snapshot's manifest
version, retain its manifest-level interface dependencies, add explicit
ros_message/ros_service targets in the existing interface directories and call
default_generators at the package root. Each interface target names only its
referenced message dependencies; same-package edges use their actual directory
labels and external edges use the referenced package's message target. Root
local_path_override selects the snapshot; consumers keep their existing labels.
The BUILD declarations are the supported interface list. No source-completeness
validator, generator verb or new message definition is introduced.

Verification: the shared local-provider command below builds every declared
surface in these five modules and runs complete existing consumer suites. The
successful invocation takes 257.564 seconds, builds 217 non-test targets and
passes 24 test targets / 185 XML cases, including unchanged interface manifest/
type-hash freshness and C++17 checks, component-interface utilities, debug
utilities, height fitting, NDT, pose initialization and both native control
packages. No failures/errors/skips occur. Original message files are unchanged.
The relocated root executable/bridge validation is recorded in the relocation
entry. Standard ROS dependencies absent locally still come from pinned RCR.
No Fast DDS, pre-commit, container or
colcon/ament/CMake run was performed.

## 2026-11-17: Local interface adoption — autoware_internal_localization_msgs

Purpose: select local PoseWithCovarianceStamped and InitializeLocalization
contracts for height fitting, NDT and pose initialization. Inspected both
service definitions, manifest and CMake. Field dependencies are geometry_msgs
and the already-local autoware_common_msgs; retain std_msgs from the manifest
without adding it as a generator dependency when no field references it.
Keep the product's existing narrowed PoseWithCovarianceStamped metadata root;
source adoption does not require broadening product interface selection.

Snapshot 1.16.0 declares two services.

Integration pattern: declare a package-local module at the snapshot's manifest
version, retain its manifest-level interface dependencies, add explicit
ros_message/ros_service targets in the existing interface directories and call
default_generators at the package root. Each interface target names only its
referenced message dependencies; same-package edges use their actual directory
labels and external edges use the referenced package's message target. Root
local_path_override selects the snapshot; consumers keep their existing labels.
The BUILD declarations are the supported interface list. No source-completeness
validator, generator verb or new message definition is introduced.

Verification: the shared local-provider command below builds every declared
surface in these five modules and runs complete existing consumer suites. The
successful invocation takes 257.564 seconds, builds 217 non-test targets and
passes 24 test targets / 185 XML cases, including unchanged interface manifest/
type-hash freshness and C++17 checks, component-interface utilities, debug
utilities, height fitting, NDT, pose initialization and both native control
packages. No failures/errors/skips occur. Original message files are unchanged.
The relocated root executable/bridge validation is recorded in the relocation
entry. Standard ROS dependencies absent locally still come from pinned RCR.
No Fast DDS, pre-commit, container or
colcon/ament/CMake run was performed.

## 2026-11-17: Local interface adoption — autoware_internal_msgs

Purpose: select local PublishedTime, ResourceUsageReport and
MissionRemainingDistanceTime used by current debug support. This package is not
a source-less metapackage: its three real message definitions need Bazel targets.
Inspected all definitions and CMake. Preserve manifest-level internal-message
module relationships, while the generated field closure uses only
builtin_interfaces and std_msgs. Do not integrate an otherwise unused interface
family merely because the manifest groups it; add concrete target support when
an actual consumer requires it.

Snapshot 1.16.0 declares three messages.

Integration pattern: declare a package-local module at the snapshot's manifest
version, retain its manifest-level interface dependencies, add explicit
ros_message/ros_service targets in the existing interface directories and call
default_generators at the package root. Each interface target names only its
referenced message dependencies; same-package edges use their actual directory
labels and external edges use the referenced package's message target. Root
local_path_override selects the snapshot; consumers keep their existing labels.
The BUILD declarations are the supported interface list. No source-completeness
validator, generator verb or new message definition is introduced.

Verification: the shared local-provider command below builds every declared
surface in these five modules and runs complete existing consumer suites. The
successful invocation takes 257.564 seconds, builds 217 non-test targets and
passes 24 test targets / 185 XML cases, including unchanged interface manifest/
type-hash freshness and C++17 checks, component-interface utilities, debug
utilities, height fitting, NDT, pose initialization and both native control
packages. No failures/errors/skips occur. Original message files are unchanged.
The relocated root executable/bridge validation is recorded in the relocation
entry. Standard ROS dependencies absent locally still come from pinned RCR.
No Fast DDS, pre-commit, container or
colcon/ament/CMake run was performed.

## 2026-11-17: Keep added integration tests outside component packages

Purpose: preserve the original source snapshots while keeping the full Bazel
acceptance coverage. Existing implementations and upstream tests change only for
a demonstrated integration/testing blocker, compatibility issue or genuinely
incorrect implementation. AGENTS.md, PLAN.md and DEVELOPING.md now specify this
boundary. New tests, adapters and fixtures belong under bazel/tests/<package>/.

Prerequisites and inspected inputs: compare each moved file with the starting
source snapshot, inspect its BUILD exports and root consumers, and retain the
original package test suite. All 26 relocated files were additions to that
snapshot: 18 standalone Python probes, three C++ consumers, two geoid wrappers,
an EKF launch adapter, a snapshot helper and a CLI version regression. Original
upstream tests and production implementations remain in place.

Integration pattern: move each added source to bazel/tests/<package>/ and change
root srcs/main labels to that path. Remove obsolete component exports. Keep
original test binaries package-local; root wrappers consume their executable
and resource providers. Components never acquire dependencies on root labels.
The node package retains its original initialization test; its added pub/sub
consumer moves to //bazel/tests:node_pubsub_test. Other renamed consumers are
//bazel/tests:adapi_specs_headers_test, :qos_compatibility_test,
:geography_with_grid_test, :geography_without_grid_test, :gnss_poser_unit_test,
:gnss_poser_characterization_test and :ros2agnocast_version_test. Existing root
standalone target names remain unchanged.

Diagnosed failures and fixes in the added test infrastructure:

- Python backend variants sharing one source generated conflicting bytecode
  actions. Disable precompilation on root Python/launch test targets; keep one
  source per probe rather than duplicate it for backend variants.
- DDS domain 102 selected port 32900 inside the host ephemeral port range and
  failed to bind. Use domains 60–63 for the affected native-control and pose
  checks instead of 102–105.
- The gyro covariance check sent repeated IMUs while waiting, legitimately
  averaging several samples and changing its expected covariance. Wait for
  endpoint discovery and diagnostic acknowledgements, then submit one IMU and
  one vehicle sample. Keep the original numeric assertions.
- Repeated projector publication triggered repeated map reloads and service
  replacement. Publish one transient-local projector and wait for the service.

Verification (Cyclone DDS, sequential invocations, existing user.bazelrc limits):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --strategy=TestRunner=local --test_env=AGNOCAST_TEST_NO_PRELOAD=1 \
  --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:all @autoware_node//... @ros2agnocast//:tests

env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
  //bazel/agnocast:bridge_plugins_test
```

The root gate passes all 61 targets in 110.141 seconds, with 131 XML cases and
no failures/errors/skips. The original ros2agnocast pytest suite reports 93
cases and the relocated version regression reports two cases (pytest wrapper
XML counts represent processes). Agnocast-enabled children explicitly preload
the heap hook; the first command keeps unrelated parent processes unpreloaded.
The bridge gate requires parent preloading and passes all 11 cases in 10.742
seconds. The installed Agnocast kernel module and device are available.

Repeat the preceding shared local-provider command with the additional
//bazel/tests:adapi_specs_headers_test label. This current-layout invocation
passes all 24 test targets / 185 XML cases and builds 217 non-test targets in
40.710 seconds, including both complete original native-control suites. There
are no failures/errors/skips. Together these commands cover the relocated
consumers, unchanged module suites and bridge behavior after local message
provider selection. Targeted formatting, Python parsing, shell syntax and
whitespace checks accompany the runtime gates.

No Fast DDS, pre-commit or colcon/ament/CMake run is included. Phase 5.2.5 continues with its remaining package groups;
this layout change does not mark the entire phase complete.
## 2026-11-17: Phase 5.2.5 — ADAPI specifications in the native-control group

Purpose: supply command gate's public operation-mode contracts while preserving
all headers of the ADAPI specification package. Inspected CMake/manifest and all
eleven public headers. There is no existing functional test suite; CMake installs
this header-only package. Add a root-owned compile/link consumer under bazel/tests/autoware_adapi_specs
including every public header, then validate concrete use through command gate. This is not
an interface-directory completeness validator or a new interface registry.

Pattern: package-local header library with scoped includes, autoware_adapi_v1_msgs,
autoware_adapi_version_msgs and RCLCPP compile providers, plus a corresponding
shared runtime provider for final consumers. Keep this API prerequisite in
Phase 5.2.5 rather than waiting for Phase 5.3's full system/API composition.
The version-interface header uses the checked-in ADAPI 1.9.2 snapshots, with
new package-local Bazel declarations for their existing definitions. Select
both with root local_path_override and retain package-local 0.0.0 dependencies.
Do not replace an available snapshot with a registry overlay because that
snapshot lacks Bazel files. The local-interface entries below record the
complete generated surfaces and consumer gate. Registry providers remain for
standard ROS dependencies absent from the checkout.

Verification (all three packages and executable checks in one final gate):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  @autoware_adapi_specs//... @autoware_command_gate//... \
  @autoware_simple_pure_pursuit//... //bazel/tests:command_gate_rclcpp_test \
  //bazel/tests:simple_pure_pursuit_rclcpp_test
```

Result: the public-header consumer passes in 0.1 seconds, with no failures/
errors/skips. All command-gate cases and its real executable also pass with
these contracts. The final complete-group invocation takes 17.772 seconds.
No source compatibility patch or generated interface change is required. The
package gate is complete. Fast DDS,
pre-commit, container and colcon/ament/CMake checks were not run.

## 2026-11-17: Phase 5.2.5 — command gate in the native-control group

Purpose: expose mode-change service transactions, state and gear outputs using
the current native ROS component. Prerequisites are the ADAPI specifications
above and already validated component-interface specs/utilities and message
providers. Inspected both implementation sources, the mode-builder header,
CMake/manifest, launch resources, fourteen unit cases and four original ROS
integration cases. Complete original suite: 18 cases, including actual component
discovery/loading through ComponentManager. The source derives from rclcpp::Node;
this task preserves that backend rather than introducing an Agnocast conversion.

Pattern: package-local compiled library, package-specific shared component,
RCR rclcpp_components_register_node with plugin and node_name (which enables
the generated native executable), <name>_record in ament_index, executable
registration and original launch resources. Preserve the CMake executable name
and SingleThreadedExecutor contract. The package has no config directory, so
its resource glob includes launch files only. Keep the unit target on gtest_main;
the integration file supplies its own ROS/GTest main.

Diagnosed linker failure: ComponentManager constructor is undefined even though
the test compiles against rclcpp_components headers. RCR exposes the manager
separately from node-factory registration. Add component_manager_library to the
integration test's deps and component_manager to dynamic_deps, alongside its
existing class_loader pair. Keep the plugin-loading cases rather than replacing
them with direct construction. No production source fix is required.

The root-owned behavior target runs the actual executable with an ordinary
rclpy peer. Call STOP/AUTONOMOUS/LOCAL/REMOTE on the system mode service and
require matching API/system states and PARK/DRIVE/NONE/NONE gear commands, with
matching state/gear timestamps. An unknown mode must return failure and publish
no new state or gear output during the bounded observation. Require normal
SIGINT exit. Service/output/shutdown waits are bounded and include child logs.

Diagnosed runtime setup failure: Python can locate the top-level message type-
support library but dlopen fails on dependent std_msgs introspection C support.
The package indexes already supply the files; the native py_test environment
did not contain LD_LIBRARY_PATH. common --run_env configures bazel run, not
this test environment. Set LD_LIBRARY_PATH=lib in the root py_test env and
keep the package index in data. A temporary additional C-shared runtime index
was removed after the complete gate passed with existing package indexes alone.
No extra runtime index, heap preload, kernel device or Agnocast bridge is needed
for this native ROS contract. Explicitly select ENABLE_AGNOCAST=0 in the peer.

Verification: use the complete-group command in the preceding ADAPI entry;
for this module alone, bazel test @autoware_command_gate//... runs both original
targets. Result: all 18 original cases pass without failures/errors/skips,
14 unit cases in 0.1 seconds and 4 plugin-loading cases in 0.2 seconds. The real
executable peer passes in 1.5 seconds. The successful final group invocation
takes 17.772 seconds. This module gate is complete, without an omitted test
group or deferred failure. No source patch, Fast DDS, Agnocast backend conversion,
pre-commit, container or colcon/ament/CMake validation is included. Retain this validated module as a separate commit.

## 2026-11-17: Phase 5.2.5 — simple pure pursuit in the native-control group

Purpose: run the native trajectory-to-control component against existing motion,
vehicle-info and polling-subscriber providers. Inspected core and node sources/
headers, CMake/manifest, config/launch and both original test files. Complete
original suite: 13 cases, seven core and six node integration cases, including
waiting for both inputs, terminal braking, empty trajectory, external velocity,
lookahead fallbacks and lateral-offset correction. Reuse validated motion
utilities and test helpers; autoware_testing is only a CMake find dependency here,
with no include/runtime consumer in these source files, so no artificial Bazel
module is added for its CMake helpers.

Pattern: package-local compiled library with explicit generated ROS compile
providers, package-specific shared component, native RCR node launcher and
component record, original executable name, declared config/launch index and
registered executable. Preserve plain rclcpp::Node and SingleThreadedExecutor.
Core tests use gtest_main; the node suite has its own main. Both tests declare
the compiled autoware_test_utils helper and runtime provider, and its index so
vehicle configuration resolves through ament rather than checkout paths.

The standalone peer resolves the original vehicle/test-helper and controller
configuration through their package indexes and starts the actual executable.
Feed ordinary ROS odometry and trajectories. Require straight tracking with
velocity/acceleration 1/1 and zero steering; trajectory-end stopping with zero
velocity, -10 acceleration and defined acceleration flag; empty-trajectory zero
velocity/acceleration; and negative steering for a 1 m lateral offset. Give each
case a distinct input stamp and require output with that stamp so a retained
command from a preceding case cannot satisfy a later assertion. Readiness is
bounded by a 15-second deadline, inputs are republished while waiting, child logs
are preserved on failure, and SIGINT must produce a clean exit. Set the peer's
LD_LIBRARY_PATH=lib as diagnosed for command gate; existing declared indexes
supply the runtime libraries. No product bridge change is involved.

Verification: the complete-group command in the ADAPI entry builds every target,
runs all original suites and both actual-executable checks sequentially with
Cyclone DDS. The five existing/core-header targets and two standalone targets
all pass in the final 17.772-second invocation. This package's seven core cases
pass in 0.2 seconds, six original node cases in 0.6 seconds and executable check
in 1.5 seconds. XML confirms no failures/errors/skips. No production source or
existing test is modified. Targeted buildifier, Python AST, clang-format on the
new C++ header consumer and whitespace checks pass. No Fast DDS, Agnocast
conversion, pre-commit, container or colcon/ament/CMake check is run.

All three native-control group gates are complete. Phase 5.2.5 remains in progress. Next inventory the trajectory,
plotting and route-handler group together, preserving plotting consumers when
their complete existing suites require them rather than omitting those checks.


## 2026-11-17: Surface adoption rules before the chronological experiments

Purpose: let a reader beginning a new integration use established conventions
before encountering the experiments that motivated them. Inspected the current
working rules, PLAN.md decisions and the journal's reproduction guidance.

Changes: add one adoption checklist in PLAN.md, linking to the existing rules
and supporting journal entries. The journal starts with a link to that checklist
and explains how current conventions relate to historical entries. AGENTS.md
requires updating the checklist in the same commit whenever an experiment
establishes a generally applicable convention. Keep steady-state development
instructions in DEVELOPING.md; no additional rule catalogue is added there.

Verification: check the changed Markdown's local links and heading anchors and
run git diff --check. Both pass. Documentation only; no build or runtime checks
are required or run. Future tasks maintain the checklist and their own evidence
entries together rather than duplicating experiment details in the checklist.


## 2026-11-17: Root dependency declarations and source selection

Purpose: make root source selection readable before adding more planning
packages. Registry dependencies come first; extensions, toolchains and pinned
archives follow; local dependencies come last, with each bazel_dep immediately
followed by its matching local_path_override. Root local declarations use
version 0.0.0 regardless of package-local version metadata. Registry release
versions remain pinned. DEVELOPING.md owns the layout convention; the adoption
checklist links there rather than duplicating it.

Method: collect existing declarations and overrides by module name, preserve
all override paths and external versions, and order prerequisites before
consumers using package-local MODULE dependencies and available registry module
metadata. Module-level test dependencies contain cycles, so this is an
approximate dependency order rather than a claim that the module graph is a
DAG. Do not infer a target dependency or source-selection change from ordering.
Verify unique names, adjacent matching pairs, root version 0.0.0 on every local
entry, and no configuration statements after the local group. The current tree
has 43 registry declarations and 79 local pairs, including the three planning
modules below. Existing selections are unchanged.

Verification: the trajectory and route-handler commands below successfully
analyze, build and test against this layout and local version convention.
Targeted buildifier and whitespace checks pass. No middleware matrix is needed
for declaration organization; the affected module gates remain complete.


## 2026-11-17: Phase 5.2.5 — autoware_pyplot and embedded Python

Purpose: supply the plotting dependency required by the original trajectory
and route-handler suites, preserving their PLOT-enabled compilation. Inspect
pyplot's manifest, eight implementation sources, public headers and its one
existing GTest case. It embeds Python with pybind11 and exercises line plots,
subplots, patches, axes forwarders and text rotation while saving real figures.

Integration pattern: package-local MODULE/BUILD, a static cc_library exposing
include/, @rosdistro//cc:pybind11 and rules_python's current_py_cc_headers and
current_py_cc_libs, plus a cc_test over the unchanged original test. The root
selects the local package. Root //bazel/tests:with_python provides the declared
Matplotlib wheel closure from the existing RCR pip hub and the selected Python
runtime to native tests. Set PYTHONHOME from sys.base_prefix and PYTHONPATH from
the launcher's declared import paths. Resolve runtime files with rules_python's
repository-mapped runfiles API: run_under does not apply a py_binary's ordinary
run environment. Keep this harness and its sitecustomize module under bazel/;
components acquire no root-label dependencies.

Runtime decisions and diagnosed failures:

- Native embedding aborted on a font-load std::runtime_error; preparing a font
  cache merely exposed a subsequent uncaught pybind11 attribute_error. A minimal
  Python C-API executable reproduced the abort independently of Autoware, while
  the same wheel rendered successfully in the standalone selected interpreter.
  Set RTLD_DEEPBIND for extension imports through the launcher's sitecustomize
  so wheel symbols resolve in their own dependency scope rather than the native
  LLVM C++ runtime. The complete original test then passes. This is Linux-specific
  runtime composition, not a production source change or general ABI guarantee.
- The numerical wheels otherwise resolve GNU C++/unwind libraries from the host.
  Declare SHA-256-pinned conda-forge Linux x86_64 archives for libstdcxx-ng
  12.2.0 h46fd767_19 and libgcc-ng 12.2.0 h65d4601_19. Verify downloaded bytes
  against the provider's published MD5 metadata, compute SHA-256 pins, and expose
  their runtime libraries as data. Prepend their runfile directories to the
  child's LD_LIBRARY_PATH. No apt/pip/rosdep installation supplies these inputs.
- Select Agg, put MPLCONFIGDIR under TEST_TMPDIR, and run the native test in
  TEST_UNDECLARED_OUTPUTS_DIR so plots remain inspectable without a display.
  Preserve absolute resource/library paths when changing directory and forward
  XML_OUTPUT_FILE through GTEST_OUTPUT. Preserve the native process exit status.

Verification, sequentially with user.bazelrc resource limits:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --run_under=//bazel/tests:with_python @autoware_pyplot//...
```

Result: one original case passes, with two PNGs and one SVG in test.outputs;
no failures, errors or skips. The successful runtime-pinned invocation takes
5.065 seconds. Repeat with --test_env=LD_DEBUG=libs to inspect the native test
PID's initialization records: both GNU libraries resolve to the pinned archive
contents, not /lib. That complete-case diagnostic invocation passes in 3.380
seconds. Bazel's own linux-sandbox host process is separate from the native
consumer when interpreting the trace. No source patch, Fast DDS, container or
CMake validation is included. Reuse this runtime composition for the two
plotting consumers below; other architectures need their own runtime inputs.


## 2026-11-17: Phase 5.2.5 — optional test-helper visualization export

Purpose: expose autoware_test_utils/visualization.hpp for the planning suites
without adding plotting to its ordinary helper target. Inspect the inline
header's pyplot, geometry, lanelet and interface includes; the existing ordinary
compiled helper already supplies the non-plotting closure.

Pattern: add a package-local :visualization cc_library containing that original
header, strip_include_prefix=include, and dependencies on :autoware_test_utils
and @autoware_pyplot. Add the latter module dependency. Leave original helper
sources, snapshot behavior and tests unchanged. The plotting consumers name the
optional target explicitly; no metarepository label enters the package.

Verification: the route-handler/test-helper command below passes all 21 original
helper cases without failures/errors/skips. The header is additionally compiled
and exercised by trajectory and route-handler's complete plotted suites. The
ordinary helper remains usable without depending on the optional target.
No Fast DDS or broad communication sweep is required for this header export.


## 2026-11-17: Phase 5.2.5 — autoware_trajectory

Purpose: integrate the continuous spatial/temporal trajectory foundation before
its route/path consumers. Inspect manifest and CMake source lists, all 20
original test translation units, all 19 declared examples, test_data YAML files
and their ament-index lookups. Required suites include interpolation, shifting,
containers, reference-path valid/invalid map specifications, temporal behavior,
footprints and lateral metrics. Preserve PLOT and ENABLE_TEST_PLOT=1.

Pattern: package-local library with explicit ROS compile providers, lanelet and
geometry/math dependencies, Boost.Geometry and Boost.ScopeExit, fmt and
TL expected. Pair generated ROS libraries with shared runtime providers through
collect_transitive_dynamic_deps. Register original test_data in ament_index;
tests/examples also declare the existing lanelet/test-helper indexes. Compile
the complete original test source list with its original main, not gtest_main,
and declare every CMake example as a cc_binary. Tests consume the optional
visualization helper and root embedded-Python composition from the preceding
entries. Keep the implementation library on its upstream C++17 standard:
reference_path.cpp's deleted default constructors remain aggregates there,
while C++23 rejects their brace initializers. No constructor rewrite is needed.

Current-target compatibility fixes, kept separate from build definitions:

- tl_expected/expected.hpp is absent from the selected TL expected provider.
  An initial forwarding-header experiment compiled that include but would
  preserve the removed ROS spelling. Inspect AWF's Lyrical trackers and the
  actual merged #1539 patch; align only this package's four affected headers
  and manifest with its tl/expected.hpp/libexpected-dev change instead. The
  temporary forwarding rule is removed. Bazel still supplies the library.
- pose.cpp repeats tf2_geometry_msgs in its include path; add_offset.hpp and
  its existing test use removed .h TF headers. Use the canonical geometry
  include and .hpp TF forms. Original assertions and behavior are unchanged.
- ParabolicTrajectoryNotNearest fails exact equality: cross-product distance
  0.15676701343069127 versus independently projected distance
  0.15676701343069119, a three-ULP difference. Use EXPECT_DOUBLE_EQ's four-ULP
  comparison, retaining both calculations and the side-of-trajectory assertion.
  UPSTREAM_AWF.md records matching/related PRs, current-main checks and limits.

Verification:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --run_under=//bazel/tests:with_python @autoware_trajectory//...
```

Result: builds 22 non-test targets, including all 19 examples, and passes all
293 original cases in 35.312 seconds (test action 13.3 seconds). XML reports no
failures/errors/disabled cases/skips; 87 SVG figures are retained. Original
fixtures and implementation formulas are unchanged. Targeted formatting,
source-list checks, Python parsing and whitespace checks pass. No Fast DDS,
Agnocast conversion, example runtime sweep or colcon/ament/CMake run is included.
The route-handler gate below additionally consumes this library and its YAML
resources. Compatibility changes are limited to the demonstrated blockers.


## 2026-11-17: Phase 5.2.5 — autoware_route_handler

Purpose: integrate route/lane queries and centerline-path generation using the
validated trajectory and lanelet providers. Inspect the one implementation,
public header, all six CMake test sources, shared test headers, test_route YAML
files and lookups of trajectory/lanelet/test-helper resources. Preserve the
original PLOT definition, test main and upstream-disabled disconnected-lane
case; introduce no filter or dependency-driven omission.

Pattern: package-local library with trajectory, lanelet extension/utilities,
ROS interfaces, geometry/math and TF dependencies; collect their shared runtime
providers. Register test_route through ament_index. The original cc_test uses
its own main, optional visualization helper, fmt/YAML/range dependencies and all
four resource indexes (own, trajectory, lanelet utilities, test utilities).
Use the same root Python launcher and headless output directory as pyplot.
No production or original test source change is required in this package.

Verification:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --run_under=//bazel/tests:with_python \
  @autoware_route_handler//... @autoware_test_utils//...
```

Result: both complete original targets pass in 57.287 seconds. Route-handler
runs 158 active cases in 16.2 seconds; its XML lists 159 cases including the one
original DISABLED_ case (not run), with no failures/errors/runtime skips. All 21
test-helper cases pass in 0.9 seconds. Route-handler produces 87 SVG figures.
The disabled upstream case is retained as an explicit limitation, not newly
excluded to make this gate pass. No Fast DDS, Agnocast conversion, container or
colcon/ament/CMake check is included. This dependency group is complete; Phase
5.2.5 remains in progress. Next inventory path generation, its local planning-
test-manager prerequisite and generated-parameter support with their full gates.


## 2026-11-17: Phase 5.2.5 — planning test manager

Purpose: prepare reusable planning-node input/output and route helpers for path
generation and later planners. Inspect the manifest, implementation, both
public headers and both original test files. The public manager header contains
GTest assertions, so GTest is a real compile dependency of the helper library,
not just a dependency of its test executable.

Pattern: package-local MODULE/BUILD with a compiled library and explicit
component-interface specs, motion, route, test-helper, geometry and ROS compile
providers. Pair their shared runtime providers; use rosidl_runtime_cpp_library
for compile information rather than its shared target. Expose the original
include tree and keep original resources in their owning packages. Manager
tests declare route-handler/test-helper indexes; the manager itself has no
resource tree requiring a new index. Split its two existing test files into
their original separate targets, using gtest_main. Root selects the local module
at version 0.0.0, adjacent to its local_path_override.

Shared six-package verification command (Cyclone DDS, sequential tests and
user.bazelrc build limits):

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_planning_test_manager//... @autoware_planning_factor_interface//... \
  @autoware_objects_of_interest_marker_interface//... \
  @autoware_planning_topic_converter//... @autoware_path_generator//... \
  @autoware_mission_planner//...
```

Result: the shared invocation passes all 11 targets / 108 original cases and
builds 39 non-test targets in 50.231 seconds, with no failures/errors/skips or
disabled cases. Manager's four interface cases and nine utility cases pass in
13.6 and 0.3 seconds. Subsequent planning-factor test-executor and test-only dependency-closure
changes are validated by the focused gates below; unchanged module results
are reused. For one package, use the same flags with only its @package//... label. The
path-generator gate additionally consumes these helpers.
No manager source change, Fast DDS, broad middleware matrix or CMake run is
required. Larger groups retain separate package commits and complete gates.

## 2026-11-17: Phase 5.2.5 — planning factor interface

Purpose: expose planning-factor accumulation, arc-length forwarding and stamped
publication, including its templated wrapper consumer. Inspect the implementation,
public header and all seven cases in the existing test. The ordinary library
uses native ROS providers; only its test needs the wrapper library, its shared
runtime collector and the heap hook. Do not turn a test dependency into a
production backend conversion. The existing all-interface selection already
contains PlanningFactorArray and its dependencies; no bridge expansion is needed.

Pattern: package-local compiled library plus a runtime_dynamic_deps collector,
then one cc_test with its original main. Use the wrapper's
:autoware_agnocast_wrapper_library for compile information and :test_dynamic_deps
for its shared closure. Declare @agnocast_heaphook in test data so the launcher
can resolve the built library through runfiles. The module depends on the
heap-hook module for that test prerequisite; there is no metarepository label.

Diagnosed failure: enabling the backend first exits without the undeclared heap
hook. Declare it and preload only the native process. With this fixed, six cases
pass but publication times out: rclcpp::spin_some never dispatches Agnocast's
queue. Correct the existing test to select its backend executor and call spin()
asynchronously. Synchronize the receive flag, retain the five-second wait,
repeat cancellation for the cancel-before-spin race, join before inspecting
payloads or destroying nodes, and propagate executor errors. Preserve every
case and assertion. UPSTREAM_AWF.md records the current-main comparison.

Verification after the executor correction:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_planning_factor_interface//:test_planning_factor_interface

env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --test_env=ENABLE_AGNOCAST=1 \
  --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_heaphook \
  @autoware_planning_factor_interface//:test_planning_factor_interface
```

Both complete seven-case suites pass (2.512 seconds for RCLCPP, 15.873 seconds
including enabled rebuild; each test action takes 0.3 seconds), with no failures,
errors, skips or disabled cases. The enabled gate uses the installed matching
Agnocast 2.4.1 module/device and built hook. No product bridge, full DDS matrix,
Fast DDS or CMake test is involved. The executor convention is linked from the
adoption checklist and explained in DEVELOPING.md for future test authors.

## 2026-11-17: Phase 5.2.5 — objects-of-interest markers

Purpose: expose coloring, geometry marker construction and marker accumulation
for later planning consumers. Inspect all three implementation files, public
headers and the original six-case test. Existing perception, geometry/math,
visualization utility and standard ROS interface providers cover the closure.

Pattern: package-local library and shared runtime collector, one original test
with gtest_main and its original ROS initialization fixture. Declare direct
compile providers and corresponding generated shared libraries; avoid an empty
ament index for a package without resources. Keep source and test files intact.
The root's local 0.0.0 declaration/override pair follows its prerequisites.

Verification: the shared six-package command above passes all six original
cases in 0.2 seconds with Cyclone DDS, without failures/errors/skips/disabled
cases. This is a library behavior gate; no standalone executable exists here.
No source patch, Fast DDS, additional middleware matrix or CMake run is included.

## 2026-11-17: Phase 5.2.5 — planning topic converter

Purpose: expose native Path-to-Trajectory conversion and its ROS component.
Inspect both implementation files, private headers, three conversion cases and
one node-interface case. Preserve field copying, header forwarding and empty
path behavior; retain the plain rclcpp::Node execution contract.

Pattern: compile the source/header set into a package library, link a named
shared component against collected ROS runtime providers, and use the existing
rclcpp_components_register_node macro with plugin and node_name to produce
path_to_trajectory_converter. Register its component record and executable in
the package's ament index. Original tests use their existing fixtures plus
gtest_main. The root composes the executable behavior check without a reverse
dependency from this component into bazel/.

Verification: all four original cases across two targets pass in the shared
six-package invocation (0.2 seconds each), with no failures/errors/skips/disabled
cases. The root executable gate below additionally sends nonempty and empty
paths through the actual binary and checks header, pose, velocity and heading
rate fields. No production/test source fix, Agnocast conversion, Fast DDS or
CMake run is required.

## 2026-11-17: Phase 5.2.5 — path generator and generated parameters

Purpose: integrate lanelet/route-based path generation with vehicle limits,
goal connection, path cutting and turn signals. Inspect both implementations,
public/private headers, original parameter schema/configuration, test_route
fixtures and all five original test translation units (38 cases, including six
real node-interface cases). Add the required local planning test manager before
selecting a registry fallback. The parameter generator is absent locally.

Pattern: use the existing RCR generate_parameter_library macro from
@generate_parameter_library//:defs.bzl with the original
param/path_generator_parameters.yaml. Pin 1.3.0-1.rcr.0 in the root, matching
this RCR snapshot. The rule runs its declared Python generator in the build
execution configuration and emits include/autoware_path_generator/
path_generator_parameters.hpp. Add that target to the component library, and
include the generator's rsl and rclcpp_lifecycle shared providers in the runtime
closure. Do not hand-maintain generated C++, invoke host pip or invent a local
renderer. Register the native PathGenerator component, path_generator_node
executable, original config and test_route resources. Tests additionally need
the vehicle, lanelet and test-helper resource indexes.

Diagnosed failure: UtilsTest.connectPathToGoal compares an interpolated start
point against a raw fixture with exact whole-message equality. Quaternion
interpolation changes a component by about 3.09e-13; position and all other
fields match. Compare each quaternion component with 1e-12 absolute tolerance,
then compare the remaining message fields exactly. Preserve the original goal
assertions and every test case; no production interpolation or fixture changes
are needed. UPSTREAM_AWF.md records the upstream-main check and scope.

Verification: the shared six-package invocation passes all 38 original cases in
2.1 seconds, without failures/errors/skips/disabled cases. The actual executable
check below loads the original vehicle/nearest-search/generator configs, queries
generated parameter values and verifies no path is fabricated before inputs
arrive. This also validates generated-header compilation, runtime parameter construction,
configuration lookup and clean shutdown. Keep manager/test-helper compile and
shared dependencies on the test target through a separate test_dynamic_deps
collector; neither belongs in the production component closure. No Agnocast conversion, Fast DDS or
CMake test is included.

## 2026-11-17: Phase 5.2.5 — mission planner

Purpose: expose lanelet route planning, arrival/reroute checks, route service
transactions and goal-pose visualization using the integrated route foundation.
Inspect seven implementation sources, private headers, original configuration/
launch resources and all five original test sources. Complete suite: 14 plugin/
utility, nine arrival, 11 reroute-safety and six real-node transaction cases.

Pattern: package-local library and shared component containing both existing
registered factories, with separate native launchers/records for mission_planner
and goal_pose_visualizer. Preserve each original executable name. Register
config, launch and executable resources. Pair ROS compile/shared providers,
component-interface, lanelet/route, motion and vehicle dependencies. Tests keep
their original four groups and main functions, plus test-helper/vehicle/lanelet/
route resource indexes where used.

Diagnosed failures: the arrival test includes removed
tf2/LinearMath/Quaternion.h; change only that spelling to Quaternion.hpp.
After compilation, all six node cases fail to construct a logging service:
logging_demo introspection C++ dlopen cannot find its generator-C dependency.
LoggerLevelConfigure needs logging_demo's complete shared provider, so add
@logging_demo//:cc_shared to the component runtime collector and a direct module
dependency. Do not replace the node API tests or alter service behavior.

Verification: all 40 original cases pass across four targets in the shared
six-package invocation (plugin 0.3 seconds, arrival/reroute 0.2 seconds each,
node transactions 2.3 seconds), without failures/errors/skips/disabled cases.
Both actual executables additionally pass the root gate below. Keep test-helper
compile/shared providers in test targets, outside the production component
closure. Only the
one-line demonstrated test-header correction changes original package files;
no production source is changed. UPSTREAM_AWF.md records its dated association
status. No Agnocast conversion, Fast DDS or CMake build is included.

## 2026-11-17: Phase 5.2.5 — composed planning executables

Purpose: validate the produced native launchers and runtime/resource composition
in addition to the component-class C++ suites. Put the shared driver in
bazel/tests/planning/, outside every original package. Declare four py_test
consumers over that source, with precompilation disabled as for other shared
Python probes. Each explicitly selects ENABLE_AGNOCAST=0 and LD_LIBRARY_PATH=lib,
and declares executables, package indexes and generated Python message providers.

The driver uses ordinary rclpy peers, bounded readiness/response waits, captured
child logs and SIGINT/clean-exit assertions. Check converter nonempty/empty paths
with distinct stamps and all copied fields; durable route-to-goal-pose forwarding;
path-generator's original parameter values (10 Hz, 300 m forward length) and
missing-input gate; mission-planner clear-route NO_EFFECT and waypoint-route
ERROR_INVALID_STATE before map/odometry initialization. Use declared API constants
rather than expecting a particular error text. Full successful routing and path
payload scenarios remain covered by the unchanged original C++ node suites.

Verification:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests:planning_converter_runtime_test \
  //bazel/tests:planning_goal_visualizer_runtime_test \
  //bazel/tests:planning_path_generator_runtime_test \
  //bazel/tests:planning_mission_planner_runtime_test
```

The initial four-case invocation passes in 10.338 seconds. After narrowing
production closures to exclude test helpers, repeat the same command with
@autoware_path_generator//... and @autoware_mission_planner//... added: all nine
targets pass in 120.625 seconds, including all 78 original cases for those two
modules and all four executable cases. This verifies the complete module gates
after that dependency change. Each executable target declares only its own
required package indexes; the final four-target gate verifies this closure in
10.706 seconds.
Together with the complete 108-case existing module gate and both seven-case
planning-factor backend gates, the six-package group is complete. Reuse unchanged
results; no system-wide communication matrix or Fast DDS sweep is implied by
this step. Targeted buildifier, Python parsing, changed-line C++ formatting,
source/test inventories and XML/log case checks accompany the runtime gates.
Phase 5.2.5 remains in progress; next take OSQP/QP and velocity smoothing together
before the behavior/motion planner groups. No container or CMake run is included.

## 2026-11-17: Phase 5.2.5 — OSQP interface

Purpose: provide the current Autoware solver API before velocity smoothing.
Inspect package.xml, CMakeLists.txt, both implementation sources, public headers
and both original test sources. The complete existing suite is eight cases in
one target, including CSC conversion and solver behavior. Prerequisites are the
existing LLVM/Lyrical workspace, Eigen, RCLCPP and GTest; no host OSQP install.

The first attempt through @rosdistro//cc:osqp_library fails on
osqp/glob_opts.h. Inspecting the consumer shows the OSQP 0.6 API (c_float,
OSQPWorkspace and OSQPData), whereas RCR selects 1.0.0.bcr.3. Select the maintained
[BCR 0.6.3.bcr.3 overlay](https://github.com/bazelbuild/bazel-central-registry/tree/main/modules/osqp/0.6.3.bcr.3)
through a component-local osqp_legacy repository alias. At the root use
multiple_version_override with 0.6.3.bcr.3 and 1.0.0.bcr.3; this preserves RCR's
selection for separate consumers instead of globally downgrading it. API
compatibility and include layout are separate questions: the BCR overlay exposes
bare osqp.h/glob_opts.h, so a package-local genrule creates two forwarding headers
under osqp_include/osqp/, with strip_include_prefix removing osqp_include. Each
forwards to the bare upstream header. No production API or source patch is needed.

Integration pattern: package-local MODULE with rules_cc, rosdistro, rclcpp,
googletest and the pinned legacy solver; BUILD library over both original sources
and public headers, Eigen/RCLCPP compile dependencies, a RCLCPP shared-runtime
collector, and one cc_test over both original test sources with gtest_main.
Select the package through an adjacent root bazel_dep(version=0.0.0) and
local_path_override. Keep the forwarding support component-owned, without root
labels. Commit the registry lock inputs.

Verification on Ubuntu 24.04, LLVM/libc++ 21.1.8, Cyclone DDS:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @autoware_osqp_interface//...
```

Result: four non-test targets build and all eight original cases pass, with no
failures/errors/skips/disabled cases (13.077 seconds total, test 0.1 seconds).
This validates the old API against the selected input, not OSQP 1.x support.
Only the legacy ABI is linked into current consumers. Loading both OSQP ABIs in
one process remains unsupported/unverified; revisit the input strategy before
introducing such a consumer. No Fast DDS, CMake or container run is included.
Next add the QP abstraction and its ProxQP backend.

## 2026-11-17: Phase 5.2.5 — QP interface

Purpose: expose both existing OSQP and ProxQP implementations for smoothing.
Inspect the four implementation and four original test sources and CMake's
single test group. The complete suite is 16 cases covering the base abstraction,
CSC utilities, OSQP and ProxQP. Reuse the validated legacy solver selection from
the preceding entry; ProxSuite is absent from local snapshots, so pin RCR's
proxsuite 0.7.3-1.rcr.0 at the root. Its existing header-only overlay supplies the
configuration headers and Eigen dependency; no host apt/pip provider is used.

Add a package-local module and library over all four original sources/public
headers, depending on @proxsuite, the package-local legacy OSQP forwarding-header
library, Eigen and RCLCPP. Keep the same two-header namespace adapter locally so
this independent package does not depend on another interface just for build
support. A RCLCPP runtime collector and the original single test_osqp_interface
target (all four test sources, gtest_main) complete the surface. Select the local
module with the root 0.0.0 dependency/override pair after its solver prerequisite.
No implementation or original test changes are required.

Verification:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @autoware_qp_interface//...
```

All four non-test targets build and all 16 original cases pass without failures,
errors, skips or disabled cases (26.265 seconds total; test 0.1 seconds).
Both solver backends are covered on the selected Cyclone/LLVM platform. The
preceding entry's dual-OSQP-ABI limitation still applies. No Fast DDS, CMake or
container run is included. Next consume both interfaces in velocity smoothing.

## 2026-11-17: Phase 5.2.5 — velocity smoother

Purpose: integrate the four existing smoothing algorithms and the real node
on both wrapper backends. Prerequisites: both solver interfaces above, the
existing trajectory/motion/vehicle/test-manager modules, Lyrical message
providers and the prepared Agnocast 2.4.1 kernel module/device and built heap
hook. Inspect all nine implementation sources, public headers, original YAML/
launch files, seven existing test sources and CMake test registration before
selecting the gate. Keep all five registered C++ targets and additionally expose
the existing stop-velocity source omitted from CMake. Complete inventory:
pseudo-jerk 12, base functions four, smoother/resampling 18, stop-velocity nine,
trajectory utilities 22 and node interface two: 67 cases across six targets.
Upstream's node-interface test is RCLCPP-only; run all C++ cases with
ENABLE_AGNOCAST=0, then exercise the actual executable's four algorithms on
both backends. This does not claim enabled coverage for the C++ suite.

Minimum pattern: separate the eight algorithm/helper sources into :smoother
and node.cpp into :autoware_velocity_smoother, exposing the original public
headers. Pair ROS compile/shared providers; use the wrapper's unconditional
compile provider and runtime collector. Link a shared component and declare
velocity_smoother_node through autoware_agnocast_node with the original plugin
class and backend executor contracts. Register the executable, component/plugin,
original config and launch resources in the ament index. Keep test-manager and
test-helper dependencies in a separate test runtime collector. Preserve original
test mains (use gtest_main only where needed) and original assertions. Each test
explicitly selects RCLCPP and declares the package/test-helper resource indexes.
Root MODULE selects the local source after both solver modules.

Demonstrated original-test blockers, fixed separately from build definitions:

- test_resample.cpp and test_pseudo_jerk_smoothers.cpp include the removed
  tf2/LinearMath/Quaternion.h. Use the provider's Quaternion.hpp spelling only.
- EmptyTrajectoryCase creates one point before asserting that construction fails.
  The current trajectory builder accepts one point; eight other cases pass.
  Remove the push so the case actually exercises empty input, preserving its
  failure assertion. No production algorithm or builder change is required.

UPSTREAM_AWF.md records dated upstream-main checks and association status.

Put the added executable driver under bazel/tests/autoware_velocity_smoother/.
Compose two root py_tests declaring the executable, original config indexes,
generated Python messages and heap hook. The driver uses ordinary rclpy peers,
spawns the actual executable with original vehicle/common/search/algorithm YAML,
and runs Analytical, JerkFiltered, L2 and Linf independently. Publish odometry,
acceleration, autonomous mode, a 101-point trajectory and an external 3 m/s limit.
Wait for the limit's sender/value acknowledgement before sending a new stamped
trajectory, then require matching output, finite nonnegative velocities, positive
motion, maximum <=3.02 m/s, finite acceleration and terminal zero speed. Assert
clean SIGINT exit. Check /proc/<child>/maps for the heap hook exactly when enabled
and the absence of RCR's libosqp.so; only the legacy solver ABI is exercised.
Keep the Python parent unpreloaded; preload only the enabled child through the
existing with_runtime launcher and declared hook artifact.

Diagnosed harness failure: reusing topic names let stale DDS discovery satisfy
readiness for a later child before initialization, causing a missing-hook map
assertion and premature SIGINT. Give each algorithm/PID its own namespace.
Bounded endpoint/response waits and fresh output stamps then work without host
network tuning or arbitrary sleeps. Interface selection/bridge definitions need
no changes for this current target.

Verification, sequentially in a clean caller environment:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @autoware_velocity_smoother//...

env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:velocity_smoother_agnocast_test

env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors --strategy=TestRunner=local \
  --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:velocity_smoother_rclcpp_test
```

Results: 13 non-test targets build, all 67 original C++ cases pass in six targets
(16.224 seconds total), and all four executable cases pass in each backend
(enabled 5.241 seconds total/3.5-second action; RCLCPP 5.171/2.9 seconds).
No final failures/errors/skips/disabled cases. With the two solver suites this
completes 91 existing C++ cases and eight runtime case executions. Targeted
buildifier, Python parsing, changed-line C++ formatting, declaration/source
inventories and XML/log count checks supplement these gates. No Fast DDS,
CMake/container or system-wide communication sweep is included. The runtime
scenario is straight-line motion; it does not establish full scenario simulation
or mixed OSQP-ABI safety. Next take behavior/motion planner foundations and
plugins; Phase 5.2.5 remains open.

## 2026-11-17: Phase 5.2.5 — object-recognition utilities

Purpose: satisfy the motion-planner common library's local dependency without
substituting registry sources. Inspect package.xml, both implementation sources,
eight public headers and all eight original test sources (including the public
API compile-check translation unit). CMake collects all test/**/*.cpp in one
suite. Existing dependencies include interpolation, local perception messages,
point types and geometry/math utilities, plus pinned PCL/ROS/TF providers.

Pattern: a package-local MODULE declares those dependencies; BUILD exposes both
sources and public headers, pairing generated message compile/shared targets
and ROS library/runtime providers. Keep the RCR pcl_ros provider already selected
by this workspace; no host ROS/PCL installation is required. One cc_test over
all eight original sources preserves the complete suite. Register the local
source with a root 0.0.0 dependency/override pair before the planner consumers.

Diagnosed blocker: test_transform.cpp includes removed Quaternion.h, so the
complete target cannot compile with the selected Lyrical provider. Use
Quaternion.hpp only. Upstream main still contains the old spelling on
2026-11-17 (blob c35e2dd42a1e4eec1726f223d2026754fa5353c1); the repository-scoped
object_recognition_utils/Quaternion PR search found no match. Leave buffer.h
unchanged because the current provider still supplies it. Keep this source
correction separate from Bazel declarations. No production source is changed.

Verification, sequential clean-environment foundation gate:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_object_recognition_utils//... \
  @autoware_behavior_velocity_planner_common//... \
  @autoware_motion_velocity_planner_common//...
```

All 33 existing cases pass with successful process exit and no failures,
errors, skips or disabled cases (test action 0.3 seconds). The complete three-
module invocation builds ten non-test targets and passes all 105 active cases in
16.498 seconds. Motion-common retains its one upstream-disabled benchmark.
No Fast DDS, CMake or container check was run. Reuse these
results for unchanged foundations in the downstream planner/plugin gate.


## 2026-11-17: Phase 5.2.5 — behavior-planner common library

Purpose: expose both original and experimental scene-module interfaces and
shared planner data/utilities. Inspect all nine implementation sources, installed
headers, the src/utilization/util_internal.hpp private header, original common
YAML, and six original test sources. CMake glob-selects all test/src/*.cpp into
one suite; preserve its helper utils.hpp and all assertions. Dependencies are
already integrated motion/trajectory/route/solver, planning-factor/marker and
vehicle libraries plus local messages, PCL, TF and lanelet providers.

Pattern: a package-local module/library includes all nine implementations and
public/private headers. Preserve include/ and the private directory-qualified
src/ paths. Pair ROS generated and native compile/shared providers, and compose
local dependency runtime collectors. Keep the original common YAML in the ament
index. The single full-suite cc_test declares test-helper, smoother, route,
lanelet and vehicle indexes and explicit RCLCPP/Cyclone DDS selection; no
Agnocast-enabled suite is claimed for these native nodes. Component rules use
only their own paths and dependency labels.

Diagnosed exit failure: all 43 assertions pass, but the process segfaults during
static destruction. A gdb backtrace shows spdlog registry lookup/drop from
rcl_logging_external_shutdown via the global RCLCPP context destructor.
smoothPath.nominal calls rclcpp::init without shutdown. Add a local ShutdownGuard
immediately after init; its destructor runs after the node/planner locals and
before process teardown, including early assertion returns. Keep every test and
assertion. Upstream main still lacks cleanup on 2026-11-17 (blob
c43a390befa3df5ba5acecab482cbadcccf941b6); a smoothPath/shutdown PR search found no
match. This is an original-test lifecycle correction, not a production fix.
Capture the stack by running the unmodified test binary under gdb -batch -ex run
-ex bt from its runfiles directory with ENABLE_AGNOCAST=0,
RMW_IMPLEMENTATION=rmw_cyclonedds_cpp and LD_LIBRARY_PATH=lib.

Verification, sequential clean-environment foundation gate:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_object_recognition_utils//... \
  @autoware_behavior_velocity_planner_common//... \
  @autoware_motion_velocity_planner_common//...
```

All 43 existing cases pass with successful process exit and no failures,
errors, skips or disabled cases (test action 0.7 seconds). The complete three-
module invocation builds ten non-test targets and passes all 105 active cases in
16.498 seconds. Motion-common retains its one upstream-disabled benchmark.
No Fast DDS, CMake or container check was run. Reuse these
results for unchanged foundations in the downstream planner/plugin gate.


## 2026-11-17: Phase 5.2.5 — motion-planner common library

Purpose: provide collision checking, polygon and planner utilities for the next
motion-planner group. Inspect all four implementation sources, public headers
and CMake's three test sources. The local object-recognition dependency lacks
Bazel definitions, so integrate it first with its complete suite (entry above).
Behavior-planner common is also a local dependency; do not replace either with
a registry counterpart. Remaining prerequisites are already integrated motion,
route, marker/factor, geometry/vehicle and smoother libraries and ROS providers.

Pattern: a package-local library exposes all four original implementations and
public headers. Pair ROS compile/shared providers and local runtime collectors;
keep the ament package index and a single cc_test over collision-checker,
polygon and utility sources, matching CMake. Explicitly select RCLCPP/Cyclone DDS.
This foundation has no executable or plugin factory of its own; behavior is
validated through its full original suite, without a system-wide DDS matrix.

Verification, sequential clean-environment foundation gate:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_object_recognition_utils//... \
  @autoware_behavior_velocity_planner_common//... \
  @autoware_motion_velocity_planner_common//...
```

All 29 active existing cases pass with successful process exit and no failures,
errors or skips; the original DISABLED_Benchmark remains disabled (test action 0.4 seconds). The complete three-
module invocation builds ten non-test targets and passes all 105 active cases in
16.498 seconds. No Fast DDS, CMake or container check was run. Reuse these
results for unchanged foundations in the downstream planner/plugin gate.


## 2026-11-17: Phase 5.2.5 — behavior planner

Purpose: produce both standard and experimental behavior-planner executables
against the current common library. Inspect all six original implementation
sources, installed/private headers, launch/config files, and both test sources.
Preserve CMake's two test targets: node-interface nominal/empty/off-track checks
and all data-processing cases. Both node classes derive from rclcpp::Node;
retain the native RCR component launcher rather than changing middleware APIs.

Pattern: production library contains five sources, excluding src/test_utils.cpp
and its header. Expose that existing helper in a separate testonly library for
both planner and plugin tests, with test-manager/test-helper dependencies kept
outside the production closure. Link the two registered node factories in one
shared component; create both original executable names and ament component
records with rclcpp_components_register_node. Register launch/config and
executables. Pair ROS generated/native compile/shared providers, including the
logging_demo service's dynamic typesupport dependency. Tests preserve source
and assertion inventories, original configuration resources and RCLCPP selection.

Verification, sequential complete module/executable gate:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_behavior_velocity_planner//... \
  @autoware_behavior_velocity_stop_line_module//... \
  //bazel/tests:behavior_velocity_planner_standard_runtime_test \
  //bazel/tests:behavior_velocity_planner_experimental_runtime_test
```

Result: 20 non-test targets build and all five test targets pass in 427.489
seconds. Both original planner targets pass all nine cases (node two in 10.2
seconds; data processing seven in 0.6 seconds), and the complete stop-line suite
passes all 23 cases in 10.2 seconds. No failures/errors/skips/disabled cases in
these modules. Standard and experimental executable checks each pass one
lifecycle case (2.3 and 2.0 seconds), including the matching plugin factory,
original parameter values, unload/reload and clean shutdown.

Reuse the unchanged 105-active-case foundation gate, retaining motion-common's
one original disabled benchmark. Together the five-package group passes all
137 active existing cases and both executable checks. Targeted buildifier,
Python parsing, changed-line C++ formatting, full source/test inventories,
component-boundary checks and XML/log counts also pass. No Fast DDS, CMake,
container or system-wide communication matrix was run. Original standard node
and stop-line suites cover nominal/empty/off-track paths; the experimental
executable gate establishes factory assembly/lifecycle, not full map/scenario
parity. Next take motion planner and obstacle-stop plugin dependencies;
Phase 5.2.5 remains open.

## 2026-11-17: Phase 5.2.5 — stop-line plugin

Purpose: make both existing stop-line factories discoverable by the actual
behavior-planner executables. Inspect plugins.xml, all six standard/experimental
implementation sources, their private headers, stop_line.param.yaml and all
three original test sources. Preserve CMake's single scene/utility/node-interface
suite, including its plugin-loading/off-track case.

Pattern: package-local library over all six sources, shared library named
libautoware_behavior_velocity_stop_line_module.so to match the original XML,
and ament_index(libraries=[...], plugin_categories={"plugins.xml":
"autoware_behavior_velocity_planner"}). This registers the original XML beneath
the base package's pluginlib resource category, rather than the plugin package's
own name. The XML and shared library are declared runfiles; no post-build ament
scan or host installation is involved. Tests use the planner's testonly helper
and all original fixture/config indexes. Components have no root labels.

Put the added executable driver in bazel/tests/behavior_velocity_planner/ and
compose two root py_tests. Each launches the corresponding native executable
with original test vehicle/common/search, smoother, planner and stop-line YAML.
Select the matching fully-qualified plugin class at startup, prove its load log
and mapped shared library, query original parameter values, unload by module
name stop_line, reload by plugin class, and require clean SIGINT exit. The service
responses have no success payload, so verify lifecycle logs as well as replies.
Give each child a unique PID-derived ROS namespace to avoid stale discovery. No heap hook
is preloaded for this native RCLCPP gate. Full path/stop geometry remains covered
by the original C++ suites; this driver tests assembly and plugin lifecycle.

Verification, sequential complete module/executable gate:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_behavior_velocity_planner//... \
  @autoware_behavior_velocity_stop_line_module//... \
  //bazel/tests:behavior_velocity_planner_standard_runtime_test \
  //bazel/tests:behavior_velocity_planner_experimental_runtime_test
```

Result: 20 non-test targets build and all five test targets pass in 427.489
seconds. Both original planner targets pass all nine cases (node two in 10.2
seconds; data processing seven in 0.6 seconds), and the complete stop-line suite
passes all 23 cases in 10.2 seconds. No failures/errors/skips/disabled cases in
these modules. Standard and experimental executable checks each pass one
lifecycle case (2.3 and 2.0 seconds), including the matching plugin factory,
original parameter values, unload/reload and clean shutdown.

Reuse the unchanged 105-active-case foundation gate, retaining motion-common's
one original disabled benchmark. Together the five-package group passes all
137 active existing cases and both executable checks. Targeted buildifier,
Python parsing, changed-line C++ formatting, full source/test inventories,
component-boundary checks and XML/log counts also pass. No Fast DDS, CMake,
container or system-wide communication matrix was run. Original standard node
and stop-line suites cover nominal/empty/off-track paths; the experimental
executable gate establishes factory assembly/lifecycle, not full map/scenario
parity. Next take motion planner and obstacle-stop plugin dependencies;
Phase 5.2.5 remains open.

## 2026-11-17: Phase 5.2.5 — motion obstacle-stop plugin

Purpose: provide the current obstacle-stop plugin for the motion planner,
using the already integrated motion-common, object-recognition, planning-factor,
signal-processing and geometry/vehicle libraries. Inspect package.xml,
CMakeLists.txt, plugins.xml, the single implementation source, private headers,
original config and all seven test sources. Preserve all 39 original cases
across decision helpers, outside/cut-in checks, parameter priority, path-length
buffering, planning factors and classification. test_utils.cpp supplies the
original fixture helpers and stays in the same test target. Declare its sibling
test_utils.hpp in that target too: without it the sandboxed compile fails on
the helper include even though the file is present in the source directory.

Minimum pattern: a package-local module/library over src/**/*.cpp and private
headers, preserving src include paths and explicit local/ROS compile providers.
Compose runtime collectors and paired message shared providers. Create
libautoware_motion_velocity_obstacle_stop_module.so to match plugins.xml and
register its XML under autoware_motion_velocity_planner's pluginlib category
with ament_index(plugin_categories={"plugins.xml":
"autoware_motion_velocity_planner"}, libraries=[":obstacle_stop_plugin"]).
Include original config, test-helper resources and full runtime closure in the
single original cc_test. No component-to-root labels, host package discovery or
post-build generation is required. grid_map_core appears in package.xml but is
not referenced by these current implementation/headers; add dependencies for
actual compile inputs rather than selecting unused manifest entries blindly.

Verification:


```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_motion_velocity_planner//... \
  @autoware_motion_velocity_obstacle_stop_module//... \
  //bazel/tests:motion_velocity_planner_runtime_test
```

The final complete invocation builds 15 non-test targets and passes all four
test targets in 23.321 seconds. Planner node five cases pass in 1.0 seconds,
node utilities nine in 0.5 seconds, obstacle-stop all 39 in 0.6 seconds, and the
real executable lifecycle regression in 1.4 seconds. No failures/errors/skips/
disabled cases remain. The planner node suite sees the valid obstacle-stop
factory, while its intentional test_plugin requests remain unavailable; do not
mistake those expected error logs for a missing real runtime dependency.

Targeted buildifier, Python parsing, complete source/test/header inventories,
component boundary checks, changed-line source formatting and XML/log counts
also pass. Existing source changes are limited to the demonstrated duplicate-
load guard; original tests are unchanged. No Fast DDS, CMake, container or
Agnocast-enabled gate is claimed for this native node. The runtime regression
checks assembly and lifecycle; full scenario simulation remains a later phase.
Next inspect the core planning metapackage's launch/resource composition and
remaining Phase 5.2.5 completion criteria before moving to Phase 5.3.

## 2026-11-17: Phase 5.2.5 — motion velocity planner

Purpose: expose the actual motion planner and its obstacle-stop plugin consumer
against the integrated foundations. Inspect all three implementation sources,
private headers, both original test sources, seven test-config inputs, YAML
message fixtures and lanelet maps. Complete suite: five node cases and nine
node-utility cases in CMake's two targets; preserve the original custom node-test
main, all topic/service assertions and traffic-light scenario. Prepare the real
obstacle-stop plugin's index even though the original test also intentionally
requests an unavailable test_plugin name.

The current class derives directly from rclcpp::Node and uses native RCLCPP
subscriptions. CMake's wrapper registration macro does not establish an
Agnocast node contract. Preserve this implementation with the RCR native
component launcher and select RCLCPP/Cyclone DDS for the whole module gate,
without changing source inheritance or claiming enabled Agnocast coverage.
Similarly, grid_map_core and the wrapper appear in the manifest but are not
current C++ input providers; no speculative registry input or middleware change
is needed just to mirror those declarations.

Pattern: package-local library over the three sources/private headers, shared
component and rclcpp_components_register_node with the original class and
executable name. Pair local and ROS compile/shared dependencies, including
logging_demo's service typesupport used by LoggerLevelConfigure. Register
config, launch, test_config, test_data, test_map and executable/component records
in the ament index. Keep test-helper and YAML parser dependencies on tests.
Declare the obstacle plugin as test data and a direct module dependency, while
the plugin's production API depends on motion-common rather than the node.
This avoids a build-target cycle and tests actual discovery.

Put the added driver in bazel/tests/motion_velocity_planner/. The root py_test
starts the real executable with its original seven test YAML files, a unique
PID-derived ROS namespace and the fully-qualified ObstacleStopModule class.
Enable debug logging because successful plugin loading uses a debug log.
Require service readiness, the successful load log and the mapped plugin shared
library; check smoother enablement and the 5 m stop margin through GetParameters.
Reject a duplicate active load, then unload/reload by the full module name.
Verify lifecycle logs beyond empty service
responses, and require clean SIGINT exit without a heap hook. Original C++ node
scenarios cover trajectory/traffic-light inputs and output; this extra driver
checks component/resource/plugin assembly.

Diagnosed production failure: startup successfully loads ObstacleStopModule,
unload reports success, but reloading reports "already loaded" and the lifecycle
check times out. load_module_plugin uses plugin_loader_.isClassLoaded, which
checks class/library availability rather than membership in loaded_plugins_.
The cached library survives instance removal. Check loaded_plugins_ with
std::any_of and get_module_name() instead; keep the duplicate warning and every
other loading/unloading behavior. Include <algorithm> explicitly. Both node
construction and the load service call this manager method, so one correction
covers both paths. The added root regression verifies active duplicate rejection
before unload and successful creation after unload, preserving its original
25-second response bound. No timeout increase or omission satisfies the gate.
Upstream main still contains the same check on 2026-11-17 (blob
21ca3ddca6d157cb1ff833294141e205fdc45727); repository PR searches for motion
planner reload and isClassLoaded found no match. This is a general lifecycle bug,
not a Lyrical API correction. Keep the source fix separate from module rules;
UPSTREAM_AWF.md records its association status.

Verification:


```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_motion_velocity_planner//... \
  @autoware_motion_velocity_obstacle_stop_module//... \
  //bazel/tests:motion_velocity_planner_runtime_test
```

The final complete invocation builds 15 non-test targets and passes all four
test targets in 23.321 seconds. Planner node five cases pass in 1.0 seconds,
node utilities nine in 0.5 seconds, obstacle-stop all 39 in 0.6 seconds, and the
real executable lifecycle regression in 1.4 seconds. No failures/errors/skips/
disabled cases remain. The planner node suite sees the valid obstacle-stop
factory, while its intentional test_plugin requests remain unavailable; do not
mistake those expected error logs for a missing real runtime dependency.

Targeted buildifier, Python parsing, complete source/test/header inventories,
component boundary checks, changed-line source formatting and XML/log counts
also pass. Existing source changes are limited to the demonstrated duplicate-
load guard; original tests are unchanged. No Fast DDS, CMake, container or
Agnocast-enabled gate is claimed for this native node. The runtime regression
checks assembly and lifecycle; full scenario simulation remains a later phase.
Next inspect the core planning metapackage's launch/resource composition and
remaining Phase 5.2.5 completion criteria before moving to Phase 5.3.

## 2026-11-17: Phase 5.2.5 — mission-planner executable index

Purpose: make the existing executable(s) discoverable through the original core
planning XML. Inspect the package's CMake executable declarations, native Bazel
launcher targets and node_executables registration. Standalone rootpath checks
use Bazel filenames directly and therefore do not prove ROS libexec discovery.
The aggregate launch first fails to resolve its expected executable name.

Preserve Bazel target labels/binaries and add register_ament_files(renames=...)
under lib/autoware_mission_planner. Map mission_planner_exe to mission_planner, goal_pose_visualizer_exe to goal_pose_visualizer.
This preserves the upstream runtime contract without aliases in original XML or
convenience symlinks in the checkout. No original source or test changes.

Verification: [the complete composed gate](#2026-11-17-phase-525--core-planning-launch-composition) runs @autoware_mission_planner//...
and its existing mission planner and goal visualizer runtime check(s), then starts the original XML.
All 40 original C++ cases and those executable checks pass; the six-node
planning launch resolves the indexed names and shuts down cleanly. Exact shared
command, platform and aggregate results are recorded at that link; reuse results
for unchanged binaries. No Fast DDS, CMake or container run was performed.

## 2026-11-17: Phase 5.2.5 — path-generator executable index

Purpose: make the existing executable(s) discoverable through the original core
planning XML. Inspect the package's CMake executable declarations, native Bazel
launcher targets and node_executables registration. Standalone rootpath checks
use Bazel filenames directly and therefore do not prove ROS libexec discovery.
The aggregate launch first fails to resolve its expected executable name.

Preserve Bazel target labels/binaries and add register_ament_files(renames=...)
under lib/autoware_path_generator. Map path_generator_node_exe to path_generator_node.
This preserves the upstream runtime contract without aliases in original XML or
convenience symlinks in the checkout. No original source or test changes.

Verification: [the complete composed gate](#2026-11-17-phase-525--core-planning-launch-composition) runs @autoware_path_generator//...
and its existing path generator runtime check(s), then starts the original XML.
All 38 original C++ cases and those executable checks pass; the six-node
planning launch resolves the indexed names and shuts down cleanly. Exact shared
command, platform and aggregate results are recorded at that link; reuse results
for unchanged binaries. No Fast DDS, CMake or container run was performed.

## 2026-11-17: Phase 5.2.5 — planning-topic-converter executable index

Purpose: make the existing executable(s) discoverable through the original core
planning XML. Inspect the package's CMake executable declarations, native Bazel
launcher targets and node_executables registration. Standalone rootpath checks
use Bazel filenames directly and therefore do not prove ROS libexec discovery.
The aggregate launch first fails to resolve its expected executable name.

Preserve Bazel target labels/binaries and add register_ament_files(renames=...)
under lib/autoware_planning_topic_converter. Map path_to_trajectory_converter_exe to path_to_trajectory_converter.
This preserves the upstream runtime contract without aliases in original XML or
convenience symlinks in the checkout. No original source or test changes.

Verification: [the complete composed gate](#2026-11-17-phase-525--core-planning-launch-composition) runs @autoware_planning_topic_converter//...
and its existing path converter runtime check(s), then starts the original XML.
All 4 original C++ cases and those executable checks pass; the six-node
planning launch resolves the indexed names and shuts down cleanly. Exact shared
command, platform and aggregate results are recorded at that link; reuse results
for unchanged binaries. No Fast DDS, CMake or container run was performed.

## 2026-11-17: Phase 5.2.5 — autoware_behavior_velocity_planner shared-library identity

Purpose: let behavior and motion components coexist in one declared runtime
index. Inspect both cc_shared_library rules and native launcher template inputs.
Both targets used planner_component, producing libplanner_component.so. Each
package-local run passes, but the aggregate lib index selects one basename;
the other launcher cannot find its expected NodeFactory class and exits.

Keep :planner_component labels stable and set the physical shared_lib_name to
libautoware_behavior_velocity_planner_component.so. The existing launcher/ament macros derive their
load paths and component records from CcSharedLibraryInfo, so no handwritten
record or source change is required. Package-qualified physical names prevent
collisions when multiple resource indexes merge into a common lib directory.

Verification: [the complete composed gate](#2026-11-17-phase-525--core-planning-launch-composition) runs the complete 9-case original module suite,
both standard and experimental behavior executables and the 23-case stop-line suite, and original planning XML. All pass with clean exits. The composed
launch proves both distinct factory libraries load together; retain the full
consumer gates rather than treating the standalone result as sufficient.
The linked entry gives exact commands and results. No Fast DDS, CMake or
container check was run.

## 2026-11-17: Phase 5.2.5 — autoware_motion_velocity_planner shared-library identity

Purpose: let behavior and motion components coexist in one declared runtime
index. Inspect both cc_shared_library rules and native launcher template inputs.
Both targets used planner_component, producing libplanner_component.so. Each
package-local run passes, but the aggregate lib index selects one basename;
the other launcher cannot find its expected NodeFactory class and exits.

Keep :planner_component labels stable and set the physical shared_lib_name to
libautoware_motion_velocity_planner_component.so. The existing launcher/ament macros derive their
load paths and component records from CcSharedLibraryInfo, so no handwritten
record or source change is required. Package-qualified physical names prevent
collisions when multiple resource indexes merge into a common lib directory.

Verification: [the complete composed gate](#2026-11-17-phase-525--core-planning-launch-composition) runs the complete 14-case original module suite,
the motion executable and the 39-case obstacle-stop suite, and original planning XML. All pass with clean exits. The composed
launch proves both distinct factory libraries load together; retain the full
consumer gates rather than treating the standalone result as sufficient.
The linked entry gives exact commands and results. No Fast DDS, CMake or
container check was run.

## 2026-11-17: Phase 5.2.5 — core planning launch composition

Purpose: validate the actual planning launch/resource assembly after its
component modules passed independently. Inspect autoware_core_planning's
package.xml, source-less CMake resource install, all config files and original
XML. It has no existing runtime tests; the declared module gate is resource
resolution, all six original nodes reaching discovery, configured parameters,
connecting remaps, and clean process exits. Full map/route driving simulation
belongs to Phase 7. Prerequisites are the integrated eight runtime package
indexes, launch_ros/XML/testing, rclpy and explicit vehicle parameters.

Pattern: package-local MODULE depends on the original exec dependencies.
BUILD's ament_index exposes config/** and launch/** and composes each child's
:ament_index through deps. These are real runtime resources, not an artificial
source-less grouping target. Root MODULE selects the local package with a
0.0.0 dependency/override pair after its consumers. Test through a root-owned
launch_test in bazel/tests/core_launch/, including the original XML through
AnyLaunchDescriptionSource. Supply vehicle_param_file explicitly from the
existing test-helper vehicle YAML, so no vehicle_model description repository
or host ROS installation is needed. This is an explicit test input, not a
replacement vehicle default. Keep RCLCPP/Cyclone selection and a distinct ROS
domain for each launch variant.

Assembly failures and fixes are consolidated in the preceding five module
entries: preserve ROS libexec names through register_ament_files renames and
use distinct package-qualified physical component library names. The next
failure is original XML configuration: mission planner and path generator
abort on uninitialized wheel_radius, and smoother fails node construction with
an unset double. The existing vehicle_param_file argument is passed to behavior
and motion planners, but omitted for those other three consumers. Add exactly
one param-file entry to each missing node. Do not inject hidden global test
parameters or change defaults in VehicleInfoUtils. The fix is separate from
Bazel declarations. Upstream main still has the omissions on 2026-11-17, blob
381a878cc439d98a5af4aa53bf06ddf18950a875. Merged core PR #1327 documents the same
failure but its inspected diff changes endpoint adaptation/remaps, not vehicle
parameters. UPSTREAM_AWF.md distinguishes that related evidence from a fix.

The active test waits up to 30 seconds for six fully-qualified node names and
queries original values: path-generator 10 Hz, stop-line required duration 1 s,
obstacle-stop margin 5 m and JerkFiltered smoothing. Check final trajectory and
path/trajectory-chain pub/sub endpoints at the XML-remapped topics. A separate
post-shutdown test asserts every launched process exits successfully. No timing
sleep, source-test omission or relaxed parameter default satisfies the gate.
The same driver tests the control launch variants described below.

Verification, sequential clean-environment gate covering every changed module
and its affected consumers:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_behavior_velocity_planner//... \
  @autoware_behavior_velocity_stop_line_module//... \
  @autoware_motion_velocity_planner//... \
  @autoware_motion_velocity_obstacle_stop_module//... \
  @autoware_mission_planner//... @autoware_path_generator//... \
  @autoware_planning_topic_converter//... \
  @autoware_core_planning//... @autoware_core_control//... \
  //bazel/tests:planning_mission_planner_runtime_test \
  //bazel/tests:planning_goal_visualizer_runtime_test \
  //bazel/tests:planning_path_generator_runtime_test \
  //bazel/tests:planning_converter_runtime_test \
  //bazel/tests:behavior_velocity_planner_standard_runtime_test \
  //bazel/tests:behavior_velocity_planner_experimental_runtime_test \
  //bazel/tests:motion_velocity_planner_runtime_test \
  //bazel/tests:core_planning_launch_test \
  //bazel/tests:core_control_launch_test \
  //bazel/tests:core_control_gate_launch_test
```

Result: 72 non-test targets build; all 23 test targets pass in 61.267 seconds.
The selected original C++ suites contain 167 cases: mission 40, path generator
38, converter four, behavior nine, stop-line 23, motion 14 and obstacle-stop 39.
All pass without failures/errors/skips/disabled cases. All seven existing
standalone executable checks pass. The three new launch targets each pass an
active configuration/graph case and a post-shutdown case (six assertions total).
Planning starts all six original nodes together and passes in 2.5 seconds;
control default and gate-enabled pass in 0.8 and 1.0 seconds. Validate counts
against current BUILD-selected test targets, excluding stale XML files left by
older target names. Targeted buildifier, Python/XML parsing, resource/dependency
inventories, component boundaries, local links and git diff checks supplement
runtime evidence.

This closes the planning/control adoption gate. All packages under core planning
and control now have Bazel declarations, and original launch resources are
usable. Reuse the unchanged complete module/backend results recorded earlier
for the rest of Phase 5.2; this is not a rerun of every historical suite. No
Fast DDS, CMake, container, whole-system matrix or Agnocast-enabled aggregate
launch was run. The native nodes retain RCLCPP; earlier smoother backend
validation remains the applicable unchanged evidence. No route/map inputs are
injected here, so successful closed-loop driving is not established. Phase
5.2.5 and Phase 5.2 are complete; Phase 5.3 system/API composition follows.

## 2026-11-17: Phase 5.2.5 — core control launch composition

Purpose: expose and validate autoware_core_control's real launch/config resources.
Inspect its package manifest, source-less CMake install, pure-pursuit YAML and
XML's optional launch_command_gate branch. There are no existing runtime tests
in this package. Reuse the previously validated command-gate and pure-pursuit
module suites/behavior; no implementation changes are needed here.

Pattern: package-local MODULE declares both original exec dependencies and
rosdistro; ament_index(data=config/launch, deps=both child indexes) supplies the
complete runtime resource/executable closure without component-to-root labels.
The root-owned shared launch driver includes the original XML and supplies its
vehicle_info_param_file from the declared test-helper fixture. Test the default
branch and launch_command_gate=true separately in isolated ROS domains. Require
/control/simple_pure_pursuit, conditional /control/autoware_command_gate,
lookahead_gain=1.0, odometry/trajectory subscriptions and the remapped control
command publisher. Verify every child exits cleanly after launch shutdown.

Verification: the exact complete command and platform are in
[the complete composed gate](#2026-11-17-phase-525--core-planning-launch-composition). Both original XML variants pass their active and shutdown cases
(four assertions total, 0.8 seconds default and 1.0 seconds enabled). The root
resource targets build with their full closures, with no source/test changes,
failures or skips. This is a launch-assembly gate; it reuses earlier component
tracking/mode behavior rather than claiming full driving simulation. No Fast
DDS, CMake, container or enabled Agnocast gate was run.

## 2026-11-18: Phase 5.3.1 — AD API adaptors

Purpose: expose the two existing RViz-facing adaptors and their complete helper
suite as the first system/API group. Inspect package.xml, both implementation
sources, five private helper/node headers, initial-pose YAML, original XML and
all of test_helpers.cpp. Its own main runs 13 cases covering covariance vector
size validation, timer/request state decisions and route/waypoint construction.
Prerequisites are the already integrated ADAPI specs/local messages, component
interface utilities, wrapper and map-height fitter. No registry counterpart
replaces a local dependency.

Pattern: package-local MODULE and library over all original sources/headers,
with explicit compile providers and paired shared-runtime collectors. Compile
wrapper support unconditionally. Preserve the two original executable names,
classes and ROS executor choices: initial_pose_adaptor_node uses MultiThreadedExecutor,
routing_adaptor_node SingleThreadedExecutor; both use the existing Agnocast-only
callback-isolated launcher when enabled. The shared component's physical name
is package-qualified from the outset. Register node/plugin records, config,
launch and executables in ament_index; compose the wrapper's ament_package for
its launch extension and environment files. Keep components independent of root
labels. The original test owns main, so use gtest rather than gtest_main.

Verification: the complete native command and both assembled backend commands
are recorded in the core API entry below. @autoware_adapi_adaptors//... builds
its complete surface and all 13 original cases pass (0.6-second test action).
The shared transaction driver additionally exercises both real executables:
a fixed-goal pose crosses adaptor -> API -> internal waypoint service with goal
and modification fields preserved, and an initial pose crosses adaptor -> API
-> localization service with configured covariance. Both backends pass and every
child exits cleanly. Vector-map mode is selected explicitly; no map is supplied,
so the original position is preserved when height fitting reports no map. This
checks the adaptor fallback and forwarding, not a new map-height algorithm gate;
reuse the unchanged fitter's earlier complete tests. No source/original-test
changes, skips or Fast DDS/CMake/container rerun were needed.

## 2026-11-18: Phase 5.3.1 — default AD API

Purpose: expose the existing interface-version, localization and routing nodes,
including their complete original suite. Inspect all five implementation sources,
private headers, configuration/launch files, test_conversion.cpp and registered
main.test.py with its interface_version.py child. Preserve all 15 conversion
cases and the original launch test, including its existing three-second child
shutdown bound and expected 1.9.1 response. The local version-message provider
is already selected; the protocol response is the existing source constant,
not a new version inferred from package metadata.

Pattern: package-local module/library, package-qualified shared component and
three autoware_agnocast_node launchers with MultiThreadedExecutor fallback,
matching CMake. Register every original executable/component/plugin record and
config/launch/test resource. Compose wrapper launch support and the published
@rclcpp_components//:ament_package runtime provider. Its smaller :ament_index
contains test component metadata but not component_container_mt; initially using
only that index causes original launch discovery to fail before tests run.
Selecting the executable-bearing provider fixes the setup without changing the
original launch suite. The test's own main similarly requires gtest rather than
gtest_main; otherwise the first link reports duplicate main. No source patch or
test adapter is necessary: the original Python child runs in the declared Bazel
test environment and finds its generated messages/RCLPY dependencies.

The original launch uses its RCLCPP multi-threaded container. The enabled branch
of its existing Python launch instead starts all three nodes independently with
their Agnocast-only executors and declared heap hook. Add no source inheritance
or executor changes. Keep native C++ and original launch suites under RCLCPP;
validate the enabled public behavior separately through the assembled API gate.
The selected source/runtime path does not use Flask or global vehicle parameters.
The separate test_default_adapi.launch.xml/global-parameter-loader path remains
for the next system-context slice, rather than a claimed result of this gate.

Verification: the complete commands are in the core API entry below. All 15
original conversion cases pass (0.2 seconds), and the unchanged original
interface-version launch test passes in a 3.1-second action. Assembled tests
add exact version, localization-state forwarding, localization request AUTO/
pose/status conversion and routing state/request/status conversion on both
backends. Native container resources, not host ROS tools, supply the launch.
No tests are omitted or filtered; no production/original-test changes, new
registry versions or interface-selection changes are required. No Fast DDS,
CMake or container rebuild is part of this module gate.

## 2026-11-18: Phase 5.3.1 — core API composition

Purpose: validate the actual system boundary across the default API and both
RViz adaptors, after inspecting the source-less core API CMake/manifest and
original XML's default enabled branches. It has no existing runtime suite.
Declare a package-local resource module with both original exec dependencies;
ament_index exports launch/** and composes the two child indexes through deps.
The root selects all three local packages with adjacent 0.0.0 dependency/override
pairs after their prerequisites. There are no component-to-metarepository labels.

Put the added launch driver under bazel/tests/api/. Include the original core
API XML, with rviz_initial_pose_auto_fix_target=vector_map as an explicit test
input so startup does not require a pointcloud-loader parameter service. Keep
all default API/adaptor branches enabled. The ordinary rclpy peer supplies
internal localization-initialize, waypoint-route and clear-route service
fixtures and publishes internal localization/route state and RViz goal/pose
inputs. It verifies:

- exact interface version 1.9.1 through the public service;
- localization state stamp/value forwarding;
- public initialization converted to internal AUTO with the pose intact and
  the internal response status returned through the API;
- UNSET routing state conversion, waypoint goal/status forwarding, and a fixed
  RViz goal passing through the adaptor/API to the internal route service;
- initial-pose forwarding through both layers, preserving pose and applying the
  configured covariance diagonal [4, 4, 0.01, 0.01, 0.01, 1] and zero off-diagonals.

Use bounded 25-second readiness/response waits, with no fixed discovery sleeps.
The active test also asserts three native launch processes (container plus two
adaptors) without the heap hook, or five standalone processes all mapping the
hook under ENABLE_AGNOCAST=1. A separate post-shutdown case asserts clean child
exit codes. The parent Python peer is not preloaded; with_runtime exports the
built hook path and the launch passes it only to enabled native children.
Host validation confirms the matching loaded 2.4.1 module/device. Check device
visibility in the host TestRunner context; a restricted tool view need not expose
the host /dev entry. No new bridge/interface metadata was necessary: both real
backend transactions pass using the unchanged product interface selection.

Complete native module/original-launch gate:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_adapi_adaptors//... @autoware_default_adapi//... \
  @autoware_core_api//... \
  //bazel/tests:default_adapi_original_launch_test \
  //bazel/tests:core_api_rclcpp_launch_test
```

This builds 41 non-test targets and passes all four test targets in 44.416
seconds: all 28 original C++ cases, the unchanged original launch case, and the
assembled active/shutdown cases. After adding process-count/preload assertions
only to the new driver, rerun its affected native target, then enabled target:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests:core_api_rclcpp_launch_test

env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:core_api_agnocast_launch_test
```

The final native action passes in 2.5 seconds (22.826 seconds total); enabled
passes in 3.2 seconds (30.193 seconds total). Both execute the transaction and
shutdown cases. Reuse unchanged C++/original-launch evidence; final selected XML
and logs show no failures/errors/skips/disabled cases. Exclude stale external
XML artifacts from older target names when auditing counts. Targeted buildifier,
Python parsing, source/test/dependency inventories, component boundaries, local
links and diff checks supplement runtime verification.

Phase 5.3.1 is complete. No original package source/test changes were needed.
No Fast DDS, CMake, image rebuild or broad communication matrix was run. The
services below the API are controlled fixtures, not a complete vehicle/system
simulation; map fitting without a supplied map uses its existing fallback.
Next take global-parameter/model context and remaining real core launch packages
as Phase 5.3.2. The global-loader test XML, alternate disabled API branches,
full system state transitions and closed-loop driving are not claimed here.
Phase 5.3 and Phase 5 remain in progress.

## 2026-11-18: Record deferred cross-platform and kernel-runtime options

Purpose: preserve the design discussion for later execution-platform and image
work without changing the current integration scope. The
[deferred options](PLAN.md#deferred-cross-platform-and-kernel-runtime-options)
are authoritative; this entry records the documentation task rather than
repeating the proposal. Inputs are the current Agnocast runtime launcher/tag
convention and the linked official Bazel, Kleaf and Pigweed documentation.

The plan distinguishes host-kernel runtime prerequisites from target-owned
kernel artifacts, user-space cross-compilation and OS/firmware image builds.
It records the proposed runtime bundle/checker, cache and worker-capability
options, VM/device validation boundaries and their unvalidated status. No new
phase requirement, automatic module loading or portability claim is introduced.

Verification: git diff --check and local Markdown file/anchor checks for the
changed documents. Documentation only: no build, runtime test, kernel operation,
Fast DDS check or cross-platform experiment was performed. Revisit the options
when a concrete target requires them in the packaging/platform phases.

## 2026-11-18: Phase 5.3.2 — sample vehicle resources

Purpose: integrate autoware_sample_vehicle_description in its actual global/runtime context.

Inspect the original resource-only CMake install and vehicle YAML, URDF and
mesh files. Expose config/**, urdf/** and mesh/** with a package-local ament_index
and rosdistro dependency. Root source selection uses the local snapshot. The
current consumer reads vehicle parameters, so xacro compilation/rendering is
not introduced. No existing runtime suite is registered. Acceptance is real
package lookup and a launched consumer receiving wheel_base=2.79,
wheel_radius=0.383 and vehicle_height=2.5 through the global loader, in both
wall-time and simulated-time contexts. Rendering the vehicle/sensor URDF remains
an explicit later task.

Pattern: retain original sources/configurations and add only package-local
MODULE/BUILD resource definitions selected by adjacent root 0.0.0 dependency/
local_path_override pairs. Added tests/fixtures live in bazel/tests/system_context.
The shared original-launch gate uses Cyclone DDS, serialized tests and declared
resources, with separate ROS domains for the two clock variants.

Verification: the complete Phase 5.3.2 gate below passed with Cyclone DDS.
Both original combined contexts passed (wall and simulated clock selection,
3.3 seconds each), including this module's assertions and clean shutdown.
There are no registered existing runtime tests in this resource-only module.
Unchanged component algorithm/backend results remain applicable. No source fix
was needed. Simulated time is selected but not advanced; this does not establish
simulation progression, URDF rendering or localization convergence.

## 2026-11-18: Phase 5.3.2 — global parameter loader

Purpose: integrate autoware_global_parameter_loader in its actual global/runtime context.

Inspect global_params.launch.py and the existing vehicle-info launch helper.
Expose launch/** with ament_index, composing the vehicle-info resource index.
Keep model selection dynamic: the loader module does not depend on one chosen
vehicle description or metarepository labels. The loader derives
<vehicle_model>_description from a runtime string; Bazel cannot infer that edge.
The product/test explicitly declares
@autoware_sample_vehicle_description//:ament_index in its runtime data and
passes vehicle_model=autoware_sample_vehicle. Select the local description with
its root dependency/override pair too: source selection alone does not put its
resources in the launched process's runfiles. A bundle supporting multiple
runtime model choices must explicitly include each supported description.
A model-specific loader target could instead fix the model and declare its
resource dependency statically, at the cost of per-model boilerplate. Record
this as a future design option in PLAN.md, not an implemented or tested change.

Include the original loader in an unscoped GroupAction
so its SetParameter actions apply to following launches. A real path-generator
probe gets no explicit vehicle YAML: successful construction and queried vehicle
values prove global inheritance rather than duplicated per-node inputs. Check
use_sim_time false and true. The source package has no existing runtime tests.

Pattern: retain original sources/configurations and add only package-local
MODULE/BUILD resource definitions selected by adjacent root 0.0.0 dependency/
local_path_override pairs. Added tests/fixtures live in bazel/tests/system_context.
The shared original-launch gate uses Cyclone DDS, serialized tests and declared
resources, with separate ROS domains for the two clock variants.

Verification: the complete Phase 5.3.2 gate below passed with Cyclone DDS.
Both original combined contexts passed (wall and simulated clock selection,
3.3 seconds each), including this module's assertions and clean shutdown.
There are no registered existing runtime tests in this resource-only module.
Unchanged component algorithm/backend results remain applicable. No source fix
was needed. Simulated time is selected but not advanced; this does not establish
simulation progression, URDF rendering or localization convergence.

## 2026-11-18: Phase 5.3.2 — default API global-loader resource closure

Purpose: expose the existing default-API loader-based test XML with the real
local vehicle description, closing the deferred runtime-resource dependency.
Inspect launch/test_default_adapi.launch.xml: it includes the global loader.
Add that module to the default-API MODULE and its ament_index deps. The generic
loader remains model-independent; root test data explicitly selects the sample
description index and passes vehicle_model=autoware_sample_vehicle instead of
the XML's sample_vehicle default. No original launch or source is changed.

Add bazel/tests/system_context/test_api_loader.py and two root launch_test targets.
They invoke the original XML with use_sim_time=true, query all three API nodes'
clock selection, call the real interface-version service (1.9.1), and require
clean shutdown. Assert one native container with no heap-hook mapping versus
three enabled standalone processes with the heap hook mapped in every process.
The Python launch parent remains unpreloaded. The enabled test requires the
matching host Agnocast kernel module and /dev/agnocast, plus the built hook and
runtime index in data. Use the existing runtime launcher, preserving host PIDs.

Verification: the complete native module/original-consumer gate recorded with
the core-localization entry passed.
After adding the process/preload assertions, rerun each new API target:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests:default_adapi_global_context_launch_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --strategy=TestRunner=local --run_under=//bazel/agnocast:with_runtime \
  //bazel/tests:default_adapi_global_context_agnocast_launch_test
```

Both passed: native 5.396 seconds total (3.1-second action), enabled 26.126
seconds total (6.2-second action), two assertions/cases per launch including
post-shutdown checks. No dependency-related skips or deferred failures remain
in this module gate. No Fast DDS check, upstream CMake build, xacro rendering,
full vehicle simulation or cross-platform validation was performed. The possible
per-model static-loader design remains unimplemented; the current dynamic
model selection is bundled explicitly. Phase 5.3.2 is complete; inspect vehicle/
sensor description rendering and the top-level headless launch boundary next.
## 2026-11-18: Phase 5.3.2 — core sensing context

Purpose: integrate autoware_core_sensing in its actual global/runtime context.

Inspect the original XML and its vehicle-velocity-converter include. Expose
launch/** and compose the converter index plus the wrapper launch-support
ament_package required by that include. The resource module has no existing
runtime tests. Its gate publishes a real VelocityReport on the original vehicle
input and checks transformed numeric twist fields after the localization relay.
Use original parameters and remaps, under both global clock settings; reuse
unchanged converter module/backend tests.

Pattern: retain original sources/configurations and add only package-local
MODULE/BUILD resource definitions selected by adjacent root 0.0.0 dependency/
local_path_override pairs. Added tests/fixtures live in bazel/tests/system_context.
The shared original-launch gate uses Cyclone DDS, serialized tests and declared
resources, with separate ROS domains for the two clock variants.

Verification: the complete Phase 5.3.2 gate below passed with Cyclone DDS.
Both original combined contexts passed (wall and simulated clock selection,
3.3 seconds each), including this module's assertions and clean shutdown.
There are no registered existing runtime tests in this resource-only module.
Unchanged component algorithm/backend results remain applicable. No source fix
was needed. Simulated time is selected but not advanced; this does not establish
simulation progression, URDF rendering or localization convergence.

## 2026-11-18: Phase 5.3.2 — core perception context

Purpose: integrate autoware_core_perception in its actual global/runtime context.

Inspect all original configuration, XML includes and the cluster Python launch.
Expose config/** and launch/**, composing ground-filter, cluster and object-
converter indexes. The cluster launch starts a native component_container, so
compose the published rclcpp_components ament_package runtime provider too.
Select the original default no-cropbox/no-external-container branch. Gate the
real ground node's global clock parameter and discovery of cluster/converter
nodes with the original namespaces. This package has no existing runtime suite;
unchanged component algorithm/data tests remain their earlier evidence. This
context gate does not claim a new perception scenario or GPU path.

Pattern: retain original sources/configurations and add only package-local
MODULE/BUILD resource definitions selected by adjacent root 0.0.0 dependency/
local_path_override pairs. Added tests/fixtures live in bazel/tests/system_context.
The shared original-launch gate uses Cyclone DDS, serialized tests and declared
resources, with separate ROS domains for the two clock variants.

Verification: the complete Phase 5.3.2 gate below passed with Cyclone DDS.
Both original combined contexts passed (wall and simulated clock selection,
3.3 seconds each), including this module's assertions and clean shutdown.
There are no registered existing runtime tests in this resource-only module.
Unchanged component algorithm/backend results remain applicable. No source fix
was needed. Simulated time is selected but not advanced; this does not establish
simulation progression, URDF rendering or localization convergence.

## 2026-11-18: Phase 5.3.2 — core map context

Purpose: integrate autoware_core_map in its actual global/runtime context.

Inspect original config substitutions and all four map-node declarations.
Expose config/** and launch/** with the map-loader, projection-loader and
visualizer resource indexes. Root-owned fixtures supply a tiny valid three-point
PCD, MGRS 54SUE projector YAML and the existing local test-helper lanelet map.
A single PCD legitimately synthesizes its metadata when the explicit metadata
path is absent. Include the original XML and require latched projector, nonempty
vector map and a three-point cloud plus the global clock setting. Do not inject
map results from a fake publisher. There are no existing module runtime tests;
reuse earlier complete component tests when their definitions stay unchanged.

Pattern: retain original sources/configurations and add only package-local
MODULE/BUILD resource definitions selected by adjacent root 0.0.0 dependency/
local_path_override pairs. Added tests/fixtures live in bazel/tests/system_context.
The shared original-launch gate uses Cyclone DDS, serialized tests and declared
resources, with separate ROS domains for the two clock variants.

Verification: the complete Phase 5.3.2 gate below passed with Cyclone DDS.
Both original combined contexts passed (wall and simulated clock selection,
3.3 seconds each), including this module's assertions and clean shutdown.
There are no registered existing runtime tests in this resource-only module.
Unchanged component algorithm/backend results remain applicable. No source fix
was needed. Simulated time is selected but not advanced; this does not establish
simulation progression, URDF rendering or localization convergence.

## 2026-11-18: Phase 5.3.2 — core localization context

Purpose: integrate autoware_core_localization in its actual global/runtime context.

Inspect the original downsample/NDT, relay, EKF, stop-filter, twist2accel and
pose-initializer launch graph and parameters. Expose launch/** and compose all
local component indexes, wrapper environment files and the topic_tools runtime
bundle. topic_tools is absent locally; inspect the official RCR metadata and
select 1.5.0-1.rcr.2 with its pinned topic_tools_interfaces dependency. The
original relay launch and typed data gate validate this choice. Root registry declarations precede
local module pairs. The original XML's typed TwistWithCovarianceStamped relay
must carry the actual sensing output, not a replacement forwarding script.
Run alongside the real map loaders, providing the pointcloud parameter service
required during pose-initializer construction. Check NDT discovery and relay
clock/values in both contexts; no scan matching convergence is claimed here.
The source package has no registered runtime suite.

Pattern: retain original sources/configurations and add only package-local
MODULE/BUILD resource definitions selected by adjacent root 0.0.0 dependency/
local_path_override pairs. Added tests/fixtures live in bazel/tests/system_context.
The shared original-launch gate uses Cyclone DDS, serialized tests and declared
resources, with separate ROS domains for the two clock variants.

Verification: the complete Phase 5.3.2 gate below passed with Cyclone DDS.
Both original combined contexts passed (wall and simulated clock selection,
3.3 seconds each), including this module's assertions and clean shutdown.
There are no registered existing runtime tests in this resource-only module.
Unchanged component algorithm/backend results remain applicable. No source fix
was needed. Simulated time is selected but not advanced; this does not establish
simulation progression, URDF rendering or localization convergence.

### Phase 5.3.2 complete resource/consumer gate

Prerequisites: the preceding component integrations, local model and test map,
Cyclone DDS, and the root source overrides. Inspect each package's CMakeLists.txt
and package.xml before declaring the resource globs and transitive resource
indexes described above. The six resource-only packages register lint checks,
not additional runtime suites; pre-commit/lint aggregation is not run here.
The added launch driver and fixtures live in bazel/tests/system_context.
Run from the repository root in a clean ROS environment:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_sample_vehicle_description//... @autoware_global_parameter_loader//... \
  @autoware_core_sensing//... @autoware_core_perception//... \
  @autoware_core_map//... @autoware_core_localization//... \
  @autoware_default_adapi//... \
  //bazel/tests:default_adapi_original_launch_test \
  //bazel/tests:core_api_rclcpp_launch_test \
  //bazel/tests:core_context_wall_launch_test \
  //bazel/tests:core_context_sim_launch_test \
  //bazel/tests:default_adapi_global_context_launch_test
```

Result: 29 non-test targets and all six test targets passed in 53.540 seconds.
This retains all 15 original default-API C++ cases and its original version
launch suite, the existing core-API transaction gate and the new contexts.
The sensing relay preserved longitudinal/lateral velocity 2.0/0.125 and heading
rate 0.25; map outputs contained the real lanelet map and three-point cloud.
The model probe observed wheelbase 2.79, wheel radius 0.383 and height 2.5.

Registry input evidence: inspect
https://bazel.ros.org/modules/topic_tools/metadata.json and
https://bazel.ros.org/modules/topic_tools/1.5.0-1.rcr.2/MODULE.bazel.
No local topic_tools snapshot exists. Pin the exercised RCR release in the root,
keep registry declarations before local pairs, and retain its lockfile hashes.
The published ament_package supplies the original relay executable/launch files;
no replacement executable or dependency patch was needed.

## 2026-11-18: Phase 5.3.3 — Velodyne description resources

Purpose: integrate velodyne_description for the original vehicle-description launch.

Inspect the local sensor_component_description snapshot, package manifest and CMake resource install. Expose launch, meshes, rviz, urdf and world through a package-local ament_index; preserve the original files. The sensor macros are required by both local sensor kits, so select this local snapshot rather than a registry replacement. No original runtime suite is registered.

Integration pattern: add package-local MODULE/BUILD definitions, select local
sources with adjacent root dependency/override pairs, and put added tests under
bazel/tests/vehicle_description. The complete group gate renders both sensor
variants through the original XML, observes robot_description and tf_static,
checks base_link to sensor_kit_base_link translation (0.9, 0, 2.0), resolves
every rendered mesh URI in declared runfiles, and checks process shutdown.
Cyclone DDS only; the robot publisher is not a switchable Agnocast component.

Verification: the complete four-module gate recorded with the core-vehicle
entry passed using Cyclone DDS. Both original vehicle launches passed all two
cases each (behavior and shutdown), with zero failures, errors or skips.
No source change, dependency patch or compatibility fix was needed. This
checks descriptions and fixed transforms, not sensor drivers or simulation.

## 2026-11-18: Phase 5.3.3 — sample sensor-kit resources

Purpose: integrate autoware_sample_sensor_kit_description for the original vehicle-description launch.

Expose config, urdf and sensor assets, and compose the local Velodyne resource index. The original xacro uses its own camera/IMU/VLS macros and Velodyne VLP-16 macros, with calibration loaded from YAML. Its manifest registers no existing runtime suite. The root bundle supplies the sample vehicle independently; the description module does not depend on the root.

Integration pattern: add package-local MODULE/BUILD definitions, select local
sources with adjacent root dependency/override pairs, and put added tests under
bazel/tests/vehicle_description. The complete group gate renders both sensor
variants through the original XML, observes robot_description and tf_static,
checks base_link to sensor_kit_base_link translation (0.9, 0, 2.0), resolves
every rendered mesh URI in declared runfiles, and checks process shutdown.
Cyclone DDS only; the robot publisher is not a switchable Agnocast component.

Verification: the complete four-module gate recorded with the core-vehicle
entry passed using Cyclone DDS. Both original vehicle launches passed all two
cases each (behavior and shutdown), with zero failures, errors or skips.
No source change, dependency patch or compatibility fix was needed. This
checks descriptions and fixed transforms, not sensor drivers or simulation.

## 2026-11-18: Phase 5.3.3 — AWSIM sensor-kit resources

Purpose: integrate autoware_awsim_sensor_kit_description for the original vehicle-description launch.

Expose config and urdf and compose both local sample sensor-kit and Velodyne indexes. The AWSIM macros reuse camera/IMU files from the sample kit, so its declared index must preserve that transitive lookup. The original CMake has no runtime suite. This renders description resources, not an AWSIM simulator integration.

Integration pattern: add package-local MODULE/BUILD definitions, select local
sources with adjacent root dependency/override pairs, and put added tests under
bazel/tests/vehicle_description. The complete group gate renders both sensor
variants through the original XML, observes robot_description and tf_static,
checks base_link to sensor_kit_base_link translation (0.9, 0, 2.0), resolves
every rendered mesh URI in declared runfiles, and checks process shutdown.
Cyclone DDS only; the robot publisher is not a switchable Agnocast component.

Verification: the complete four-module gate recorded with the core-vehicle
entry passed using Cyclone DDS. Both original vehicle launches passed all two
cases each (behavior and shutdown), with zero failures, errors or skips.
No source change, dependency patch or compatibility fix was needed. This
checks descriptions and fixed transforms, not sensor drivers or simulation.

## 2026-11-18: Phase 5.3.3 — core vehicle description launch

Purpose: integrate autoware_core_vehicle for the original vehicle-description launch.

Inspect the original launch XML and vehicle.xacro. Expose launch and urdf, composing the published robot_state_publisher ament_package executable provider and xacro ament_index (its bin/xacro executable is needed on PATH). Neither provider exists locally. The original launch validates RCR versions 3.5.6-1.rcr.0 and 2.1.1-3.rcr.3
respectively; pin those root inputs and the generated lockfile hashes. Keep model-specific resources in caller-owned test data, with vehicle_model and sensor_model passed explicitly. There are no original runtime tests; preserve all original launch/source files.

Integration pattern: add package-local MODULE/BUILD definitions, select local
sources with adjacent root dependency/override pairs, and put added tests under
bazel/tests/vehicle_description. The complete group gate renders both sensor
variants through the original XML, observes robot_description and tf_static,
checks base_link to sensor_kit_base_link translation (0.9, 0, 2.0), resolves
every rendered mesh URI in declared runfiles, and checks process shutdown.
Cyclone DDS only; the robot publisher is not a switchable Agnocast component.

Verification: the complete four-module gate recorded with the core-vehicle
entry passed using Cyclone DDS. Both original vehicle launches passed all two
cases each (behavior and shutdown), with zero failures, errors or skips.
No source change, dependency patch or compatibility fix was needed. This
checks descriptions and fixed transforms, not sensor drivers or simulation.

### Phase 5.3.3 description gate

Prerequisites: the earlier local sample-vehicle resource index, the root's
Cyclone DDS configuration, and the four local module selections above. The
original launch uses a command substitution invoking xacro by name. The registry
xacro index publishes bin/xacro, so compose that index instead of relying on a
host apt/pip executable. The robot publisher's ament_package exposes its ROS
executable contract. Both dynamically selected descriptions must be in root test
data; the generic vehicle module has no dependency on either concrete model.
Resource-only descriptions need no C++/URDF parser at build time; rendering tools
belong to the actual launch consumer. Gazebo plugin references in rendered XML
are not executed by this gate and do not require simulator dependencies.

Inspect registry module/source inputs at
https://bazel.ros.org/modules/xacro/2.1.1-3.rcr.3/source.json and
https://bazel.ros.org/modules/robot_state_publisher/3.5.6-1.rcr.0/source.json.
The selected registry overlays already expose the needed runtime providers.
Run from the repository root, with no sourced ROS installation:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @velodyne_description//... \
  @autoware_sample_sensor_kit_description//... \
  @autoware_awsim_sensor_kit_description//... @autoware_core_vehicle//... \
  //bazel/tests:vehicle_description_sample_launch_test \
  //bazel/tests:vehicle_description_awsim_launch_test
```

Result: four module targets and both test targets passed in 57.014 seconds,
2.2 seconds per launch action. XML records two cases per sensor variant, zero
failures/errors/skips. The tests receive the actual latched rendered URDF and
static transforms, verify expected base/sensor/lidar links and calibration,
and resolve every mesh URI from declared package resources. There was no failed
trial requiring a source fix. Targeted buildifier, Python parsing and
`git diff --check` passed. No Fast DDS check, upstream CMake build, hardware,
sensor driver, Gazebo/AWSIM execution or driving simulation was performed.

The model-resource group is complete. Inspection of the original top-level
core launch found an unconditional RViz node; this separate headless-entry
boundary is next, and no original launch has been modified to bypass it.

## 2026-11-18: Phase 5.3.4 — optional RViz in the original core launch

Purpose: permit the existing top-level core launch to run in a headless product
without changing its default GUI behavior. Inspect the original XML: all eight
functional groups have switches, but RViz starts unconditionally. The native
aggregate gate failed before tests completed with `package 'rviz2' not found`,
even when the caller supplied launch_rviz=false. A headless bundle should not
need an unused GUI runtime. This is a launch-contract issue, not a ROS/toolchain
compatibility patch.

Add one launch_rviz argument defaulting to true and guard only the existing
RViz node with it. Preserve all original functional groups, arguments and paths.
Keep this source fix separate from the Bazel definitions. Do not replace the
full launch or suppress process failures in the test adapter.

Verification: the same //bazel/tests:core_system_headless_launch_test gate
failed without the option and passed with launch_rviz=false, all eight core
groups enabled and clean shutdown. The test also parses the indexed original
XML to assert the GUI default is still true. The final complete Phase 5.3.4 gate
records commands and results with the core composition entry. Default-on GUI
execution itself was not run; this is a statically preserved default, not a
claim of rendered GUI validation. No upstream CMake/GCC build was performed.

Upstream reviewed on 2026-11-18: autoware_core main
9799c6da7cdf4cb0bd9d9766e62acb406fd2f73c still has the unconditional node.
Searches for RViz/headless and launch_rviz found no matching issue/PR; the larger
autoware_launch package already offers an RViz switch, which is related precedent,
not an associated fix. Record the actionable finding in UPSTREAM_AWF.md.

## 2026-11-18: Phase 5.3.4 — component-interface admission

Purpose: close this remaining autoware_core package boundary. Inspect its
manifest, original build/install declarations and all registered tests.

Three original C++ suites; deploy CLI and public JSON/rule library. Use the existing pinned nlohmann_json header provider, register manifest_admit under its original ROS executable path, and keep source unchanged.

Add package-local MODULE/BUILD rules selected with adjacent root 0.0.0
dependency/local_path_override pairs. Added adapters/behavior tests belong under
bazel/tests; original sources and tests remain in place. The complete module
gate is declared in PLAN.md Phase 5.3.4. The final combined command recorded
with the top-level composition entry passed, with no failures, errors,
dependency-related skips or disabled tests. Reuse preceding unchanged
component/backend results; this group uses Cyclone DDS/native RCLCPP.

Result: all 74 original C++ cases passed across test_admission_rule (42),
test_manifest_json (19) and test_manifest_admit (13). The actual CLI also
preserved the operational-error contract: run
`env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" bazel run --config=no-agnocast @autoware_component_interface_admission//:manifest_admit -- /tmp/autoware-nonexistent-admission-input.json`
with that path absent; it reports `cannot open manifest` and exits 2. The
original suite covers accepted/rejected pairings and CLI verdict/output logic.
No production or original-test change was needed.

## 2026-11-18: Phase 5.3.4 — marker utilities

Purpose: close this remaining autoware_core package boundary. Inspect its
manifest, original build/install declarations and all registered tests.

Original marker-conversion suite and public library, with local geometry, vehicle and Lanelet/message dependencies. Preserve its custom test main instead of linking a second gtest_main. Compose shared runtime dependencies at the test executable.

Add package-local MODULE/BUILD rules selected with adjacent root 0.0.0
dependency/local_path_override pairs. Added adapters/behavior tests belong under
bazel/tests; original sources and tests remain in place. The complete module
gate is declared in PLAN.md Phase 5.3.4. The final combined command recorded
with the top-level composition entry passed, with no failures, errors,
dependency-related skips or disabled tests. Reuse preceding unchanged
component/backend results; this group uses Cyclone DDS/native RCLCPP.

Result: all 29 original MarkerConversionTest cases passed. Initial linking
reported duplicate main because this test supplies its own rclcpp-initializing
entry point; link gtest, not gtest_main. This is a Bazel declaration correction,
not a source patch. The public target preserves scoped include paths and uses
the existing local geometry/Lanelet/vehicle closure.

## 2026-11-18: Phase 5.3.4 — interface-spec lint

Purpose: close this remaining autoware_core package boundary. Inspect its
manifest, original build/install declarations and all registered tests.

All original pytest files, including copyright and committed-spec/manifest checks. Export source fixtures from the specs module and supply the real manifest generator. A root-owned adapter copies declared inputs into sibling package directories in a temporary workspace, preserving original relative-path expectations without depending on host source layout.

Add package-local MODULE/BUILD rules selected with adjacent root 0.0.0
dependency/local_path_override pairs. Added adapters/behavior tests belong under
bazel/tests; original sources and tests remain in place. The complete module
gate is declared in PLAN.md Phase 5.3.4. The final combined command recorded
with the top-level composition entry passed, with no failures, errors,
dependency-related skips or disabled tests. Reuse preceding unchanged
component/backend results; this group uses Cyclone DDS/native RCLCPP.

Result: all 65 original pytest cases passed, including the original copyright,
real-header acceptance and generator-backed freshness checks; zero skips.
The same gate reran all 30 component-interface-spec cases because its BUILD
exports the lint fixtures. Preserve source-package names when staging inputs:
canonical repository directory names end in `+`, which otherwise break the
suite's sibling lookups and cause seven dependency-related skips. Parse the
staged package.xml to choose each directory name. The RCR copyright adapter
redirects `.` using TEST_TARGET; remove that target hint inside the temporary
workspace so this original package check scans its intended declared sources.
No original test or lint implementation was changed.

The actual CLI also passed with the committed headers/config/manifest and the
real generator, reporting zero findings and zero error findings. Reproduce:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel run --config=no-agnocast \
  @autoware_interface_spec_lint//:ament_autoware_interface_spec_lint -- \
  --spec-dir "$PWD/src/core/autoware_core/common/autoware_component_interface_specs/include/autoware/component_interface_specs" \
  --manifest "$PWD/src/core/autoware_core/common/autoware_component_interface_specs/interface_manifest.json" \
  --generator "$PWD/bazel-bin/external/autoware_component_interface_specs+/generate_interface_manifest" \
  --config "$PWD/src/core/autoware_core/common/autoware_interface_spec_lint/config/interface_gates.yaml"
```

The committed config itself leaves four unimplemented gates off; preserve that
existing scope. This is a targeted module suite/CLI check, not pre-commit.

## 2026-11-18: Phase 5.3.4 — example test node

Purpose: close this remaining autoware_core package boundary. Inspect its
manifest, original build/install declarations and all registered tests.

Original standalone XML, native component plugin and executable. The package derives from autoware::node::Node, not the switchable wrapper; use the published native component registration macro and check real parameter services in standalone and container-loaded instances.

Add package-local MODULE/BUILD rules selected with adjacent root 0.0.0
dependency/local_path_override pairs. Added adapters/behavior tests belong under
bazel/tests; original sources and tests remain in place. The complete module
gate is declared in PLAN.md Phase 5.3.4. The final combined command recorded
with the top-level composition entry passed, with no failures, errors,
dependency-related skips or disabled tests. Reuse preceding unchanged
component/backend results; this group uses Cyclone DDS/native RCLCPP.

Result: both added launch cases passed: real standalone XML plus container-
loaded native component parameter services, followed by clean exits. Initial
startup failed because a component record alone does not install a libexec
binary. Register autoware_test_node_node under lib/autoware_test_node and include
that registration in the index. Keep the indexed executable free of a reverse
data edge to that same index, which would create an index/executable cycle.
The caller includes the index in runtime data. No source change was needed.

## 2026-11-18: Phase 5.3.4 — generic smoke-test support

Purpose: close this remaining autoware_core package boundary. Inspect its
manifest, original build/install declarations and all registered tests.

Expose the original Python helper and CMake/resource files. Run both original smoke test cases against autoware_test_node with empty optional parameter filename lists. The root adapter supplies launch configurations and reuses the original suite rather than replacing its assertions.

Add package-local MODULE/BUILD rules selected with adjacent root 0.0.0
dependency/local_path_override pairs. Added adapters/behavior tests belong under
bazel/tests; original sources and tests remain in place. The complete module
gate is declared in PLAN.md Phase 5.3.4. The final combined command recorded
with the top-level composition entry passed, with no failures, errors,
dependency-related skips or disabled tests. Reuse preceding unchanged
component/backend results; this group uses Cyclone DDS/native RCLCPP.

Result: both original smoke suite cases passed against the actual test-node
executable. The adapter only supplies arg_package, arg_package_exe and empty
optional parameter filename lists, then uses the helper's original launch
entities and original test classes. The resource-only CMake package registers
no additional runtime tests of its own; its smoke-test helper is the behavior
under test. CMake helper files remain available as indexed resources; no new
CMake build or compatibility claim is made.

## 2026-11-18: Phase 5.3.4 — top-level core composition

Purpose: close this remaining autoware_core package boundary. Inspect its
manifest, original build/install declarations and all registered tests.

Expose original launch/RViz resources and compose all eight local core-group indexes plus the global loader. Select the concrete model and sensor indexes in root test data. Use a declared tiny map bundle and the original full launch; do not replace any functional group with a test double.

Add package-local MODULE/BUILD rules selected with adjacent root 0.0.0
dependency/local_path_override pairs. Added adapters/behavior tests belong under
bazel/tests; original sources and tests remain in place. The complete module
gate is declared in PLAN.md Phase 5.3.4. The final combined command recorded
with the top-level composition entry passed, with no failures, errors,
dependency-related skips or disabled tests. Reuse preceding unchanged
component/backend results; this group uses Cyclone DDS/native RCLCPP.

Result: the original full launch passed both behavior and shutdown cases,
with all eight functional group switches left at their true defaults and
launch_rviz=false. Real map loaders and robot publisher produced indexed map
and rendered vehicle data; sensing-to-localization carried velocity 2.0, the
path generator and pure pursuit received wheelbase 2.79, the real version
service returned 1.9.1, and planning/control publisher/subscriber connections
were present. The test checks the new RViz argument remains default true.
All original launched processes exited cleanly. This proves headless resource,
configuration and communication assembly, not planning/driving scenario output.
The root package index composes the headless closure; GUI consumers must add
their RViz runtime provider separately. A default-on source flag is the only
original launch change; its evidence is in the separate source-fix entry.

### Phase 5.3.4 complete gate and Phase 5 closure

Prerequisites: completed preceding Phase 5 slices, current root local overrides,
RCR Cyclone DDS/toolchain providers and the local sample model/sensor description
indexes. No host ROS installation, extra apt/pip package, Agnocast kernel device
or allocator preload is needed for this native aggregate. Component repositories
export their own build/resources; only root test data selects concrete models,
fixtures and complete product composition. Run sequentially from the repository
root in a clean environment:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_component_interface_admission//... @autoware_marker_utils//... \
  @autoware_interface_spec_lint//... @autoware_test_node//... \
  @autoware_testing//... @autoware_core//... \
  @autoware_component_interface_specs//... \
  //bazel/tests:interface_spec_lint_tests \
  //bazel/tests:autoware_test_node_launch_test \
  //bazel/tests:autoware_testing_original_smoke_test \
  //bazel/tests:core_system_headless_launch_test
```

Final result: 28 non-test targets and all ten test targets passed in 13.162
seconds. XML records 204 executed cases: admission 74, markers 29, specs 30,
lint 65, example node 2, original smoke helper 2 and top-level core 2. Every
selected XML reports zero failures/errors/skips/disabled cases. The full core
launch action took 4.0 seconds. Earlier unaffected module and Agnocast results
remain their existing journal evidence, rather than rerunning the whole system
matrix. Targeted buildifier, Python parsing and git diff --check passed.

Audit every package.xml under src/core/autoware_core: all 74 package directories
now have package-local MODULE.bazel and BUILD.bazel and matching root local
source overrides; zero packages remain without declarations. This declaration
audit is not a substitute for runtime testing; the suites above and preceding
module entries provide the validation. Phase 5.1, 5.2, 5.3 and Phase 5 are closed.
No Fast DDS check, upstream CMake/GCC build, GUI display, sensor hardware,
Agnocast aggregate, localization convergence or full driving simulation was
run for this gate. Runtime images and full simulation remain Phase 7; introduce
GPU dependencies against an actual selected Phase 6 consumer next.

## 2026-11-18: Phase 6 — SDK choices and direct-provider experiment plan

Purpose: define the next GPU adoption experiments after Phase 5, separating
candidate SDK/toolchain choices from tested integration results. PLAN.md owns
the numbered steps and decisions; do not repeat them in the developer manual
until they describe supported targets.

Inspected inputs: current CUDA-utils/TensorRT/common/transport manifests, CMake
sources and original tests; CUDA dependency-meta CMake extras; existing LLVM/
RCR graph; NVIDIA redistributable and package indexes; current rules_cuda
registry metadata and source descriptions. The agreed first route keeps the
existing host toolchain, tries nvcc/CUDA 13.2.2 and evaluates current upstream
rules before introducing patches. TensorRT 10.16.1.11 for CUDA 13.2 is an x86
candidate; JetPack's 10.16.2 remains a distinct later target.

Metadata inspection is reproducible from these primary inputs:

- [CUDA manifest](https://developer.download.nvidia.com/compute/cuda/redist/redistrib_13.2.2.json).
- [Upstream rule versions](https://bcr.bazel.build/modules/rules_cuda/metadata.json)
  and [RCR rule versions](https://bazel.ros.org/modules/rules_cuda/metadata.json).
- NVIDIA Ubuntu 24.04 package indexes:
  [x86_64](https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/Packages.gz)
  and [SBSA](https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/sbsa/Packages.gz).
  Decompress each index and inspect Package: libnvinfer10 stanzas with Version:
  10.16*. On this review date, both list 10.16.1.11 for CUDA 13.2 and neither
  lists 10.16.2. This establishes availability only, not build compatibility.

New experiment: a package.xml dependency need not imply a separate Bazel module
when it exists only to carry CMake settings. autoware_cuda_dependency_meta
exports no code; its extras set CMAKE_C_FLAGS/CMAKE_CXX_FLAGS with -fno-lto to
avoid duplicate fatbinData symbols. Test CUDA-utils with direct rules_cuda/
CUDA-runtime/CCCL dependencies and inspect actual LTO/link actions. Preserve
required effects in the appropriate targets; omit an empty shim only after the
complete existing consumer suite passes. Do not remove the original package or
apply this exception to real implementation/interface dependencies. The working
rule and adoption checklist now require that inspection and evidence.

Verification: documentation-only change; git diff --check and added local
Markdown link/anchor checks. Read-only host inspection with
`nvidia-smi --query-gpu=name,compute_cap,driver_version --format=csv,noheader`
reported RTX 5070 Laptop GPU, compute capability 12.0 and driver 620.70.06.
This confirms device visibility, not successful CUDA execution. No SDK download/
extraction, compiler probe, Bazel build/test, runtime integration or Fast DDS
check was performed. All proposed technical inputs and shim omission remain
unvalidated; Phase 6.1 is the next implementation task.

## 2026-11-18: Phase 6.1 — CUDA utils and pinned toolkit

Purpose: build and execute the complete local autoware_cuda_utils suite using
Bazel-fetched SDK inputs, while testing whether its CMake-only dependency-meta
package needs a separate Bazel representation.

Inputs/prerequisites: inspect the package manifest/CMakeLists and all five
original .cu test files plus test_main.cpp. The header library depends on CUDA
runtime and CCCL (Thrust/CUB/libcudacxx). The meta package exports only -fno-lto
through CMake extras, no implementation or runtime resources. Use the existing
hermetic LLVM/libc++ 21.1.8, Bazel 9.3.2 and Ubuntu 24.04 x86_64. The host
RTX 5070 Laptop GPU (sm_120) and NVIDIA driver 620.70.06 provide execution;
there is no apt/pip toolkit prerequisite.

Rule selection: inspect upstream rules_cuda 0.4.0 and RCR 0.3.0.rcr.0 source
metadata and rule/templates. The RCR fork adds clang-host defines, static CUDA
archive-sharing tags and multiplatform redist changes. Upstream 0.4.0 already
handles this consumer's manifest/platform/architecture selection; its nvcc
route and shared cudart do not expose a need for those fork patches. Select
0.4.0 and retain the resulting lockfile, including its bazel_features 1.54.1
analysis-helper dependency. Do not claim every future shared-library, platform
or compiler route works merely because this one passes.

Minimum integration pattern:

1. Root MODULE declares rules_cuda, includes third_party/cuda/cuda.MODULE.bazel before
   local dependency pairs, and selects the local CUDA-utils source with a 0.0.0
   dependency/override pair. The included file pins CUDA 13.2.2 and manifest
   SHA-256 89d93e6de640d47eb43c63437fe1be426ed21dd58cc79933a29acbca33c6eeb8.
   Request only cccl/crt/cudart/nvcc/nvvm and linux-x86_64; archive hashes come
   from that pinned manifest. No host /usr/local/cuda discovery is used.
2. Set the product CUDA compiler to nvcc and architecture to sm_120 in .bazelrc;
   keep the host C++23 toolchain unchanged. The original CUDA test sources use
   a package-local C++17 flag, with the same hermetic Clang host compiler.
3. Add a package-local module using the rules_cuda extension's cuda repository.
   A cc_library exposes the scoped headers and direct CUDA runtime, Thrust,
   CUB and libcudacxx dependencies. Compile all test/*.cu with cuda_library,
   alwayslink=True; keep test_main.cpp in the cc_test and use gtest, not a
   second main. No original source/test/configuration is modified.
4. Omit autoware_cuda_dependency_meta from the Bazel graph. Validate the actual
   actions and complete multi-source consumer, rather than translating an
   empty CMake shim mechanically. This decision covers the current non-LTO
   configuration; revisit it before enabling LTO/device-link variations.

Observed failures and fixes, consolidated into the accepted result:

- nvcc's host_defines.h rejects libc++ on x86. Acknowledge this unsupported
  combination with the CUDA-only -D_ALLOW_UNSUPPORTED_LIBCPP product flag;
  do not apply it to all ordinary C++ compilation or claim vendor support.
- With that policy check overridden, math_functions.h uses removed
  std::__promote in pow. Enabling its existing modern-libc++ declaration also
  exposes an isnan(long double) noexcept mismatch and classification definitions
  conflicting with libc++ using declarations. Restrict the relevant existing
  modern-libc++ branches to Linux/libc++ >= 21; do not enable Android C-library
  branches or alter other standard libraries. Keep a documented two-file CRT
  patch; the native patch parser misattributes the second file's hunks when
  both are in one patch, so apply separate .h and .hpp patches. Use
  zero-context diffs to avoid preserving vendor tab/trailing-space context in
  the patch files; the resulting patched headers are unchanged.
- Upstream rules need no fork for component patching. Fetch the exact CRT
  13.2.86 archive through http_archive (SHA-256
  174ad29ab90c9301bd25f32f935345e65f19fbb0f39c15788daae25b0e924892),
  preserving the crt/ layout. Its small root-owned BUILD exposes headers,
  component files and license. Standard override_repo replaces only
  cuda_crt_linux_x86_64_13_2_2 in the extension. This keeps all other manifest
  components and rules_cuda source unchanged.

Verification commands, from the repository root in a clean ROS environment,
with access to the real host GPU and builds/tests serialized:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @autoware_cuda_utils//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel aquery --config=no-agnocast --output=jsonproto \
  'mnemonic("CudaCompile|CppCompile|CppLink", deps(@autoware_cuda_utils//:test_autoware_cuda_utils))'
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests:core_system_headless_launch_test
```

Result: the final CUDA gate passed in 4.056 seconds (2.9-second test action).
XML records 17 cases: error checks 2, unique-pointer utilities 5, clear utility
1, streams 5 and Thrust 4; zero failures/errors/disabled/skipped cases. All
five original CUDA source files compile, and original test registration is
retained. aquery contains ten configured CUDA compile actions (PIC/non-PIC),
covering five unique original sources, with nvcc -ccbin pointing at the hermetic
Clang and code generation compute_120/sm_120. Neither these nor the selected
plain-C++ test compile/link actions contain LTO-enable flags; the meta shim's
required current effect is satisfied without adding global -fno-lto. ELF NEEDED
entries include libcudart.so.13 and hermetic libc++/libc++abi/libunwind, with
runpaths to Bazel-provided libraries. The native full-core regression passed
both behavior/shutdown cases in 48.956 seconds (4.3-second test action).

Targeted buildifier, git diff --check and added local Markdown link/anchor
checks passed. The current consumer needs no static culibos, architecture,
rule-source or reproducibility patches. No TensorRT integration, SBSA/Thor
cross-build, additional GPU architecture, Fast DDS check, upstream CMake build,
LTO-enabled validation or bit-identical rebuild experiment was performed.
The CRT compatibility workaround is useful toolchain feedback, recorded in
UPSTREAM_RCR.md with its unsupported-configuration boundary. Phase 6.1 is
complete; TensorRT foundation (6.2) is next.

## 2026-11-18: Model subtree layout and portable launch-path requirements

Purpose: agree the model repository/build/runtime contract before integrating
the first real-model consumer. PLAN.md owns the detailed design and experiment
gates; AGENTS.md applies the ownership/path rules. No model targets are supported
yet, so this does not establish a steady-state DEVELOPING.md procedure.

Inspected inputs: ansible/roles/artifacts/tasks/main.yaml downloads versioned
Hugging Face bundles under the model data directory. Its variable is data_dir;
autoware_launch already calls the launch argument data_path. The current
planning-config diffusion_planner.param.yaml selects
$(var data_path)/diffusion_planner/v5.0, although Ansible lists several versions.
The launch source inventory found 26 files with HOME/autoware_data/ml_models
defaults. Confirm these inputs and the actual active consumer again at import.

Decisions: use squashed model subtrees under src/models/, generally matching
existing bundle subpaths. Import only the selected diffusion revision under
src/models/diffusion_planner, without a version subdirectory. Record immutable
upstream provenance. Each model repository owns one Bazel module, one ament
package index and its metadata/resources; add MODULE.bazel, BUILD.bazel,
package.xml and minimal ament_cmake CMakeLists.txt there. A root-owned aggregate
model index is not the selected design. This permits local build support and
model edits without upstream forks, while preserving independent repository
boundaries for future independent submodule or models.repos checkouts.

Keep weights and associated metadata in declared model bundle targets. Inspect
upstream LFS/Xet attributes and materialize actual bytes; validate retrievable
large-file objects and a fresh-checkout build before claiming reproducibility.
A subtree alone does not establish that materialization or object availability.

Launch paths must exclusively use the model repository's own indexed resources,
not a source/workspace path or HOME model directory. Remove the common model
data_path argument and its propagation; no external-directory override or legacy
versioned diffusion branch is retained. The source tree's data_dir/data_path
names and HOME paths above describe the inspected implementation, not the new
contract. Diffusion uses its single flat indexed bundle.

Consequence: colcon users must obtain the required model repositories,
materialize their weights, build the model packages and source the resulting
environment. Ansible hf downloads into HOME alone no longer satisfy launcher
dependencies. Missing model indexes mean required packages are absent; do not
silently resolve downloaded HOME files. Update launcher model-package runtime
dependencies with the indexed paths. External-directory-only launching is not
promised to remain compatible.

Keep repositories/ and ansible/ reference-only. There is no mandatory
per-migration models.repos update, Ansible entry removal or empty-role deletion.
Retain models.repos as a future collaborative/colcon checkout option with a
caveat: upstream HF revisions lack our added package.xml/CMake/build metadata.
Raw checkouts must receive that support and materialize the actual payloads
before they can be called colcon-compatible. A checkout manifest is not a build
support marker; MODULE.bazel marks Bazel adoption. Any future manifest workflow
needs its own validation rather than being assumed to work from a list of refs.

Colcon requires package.xml plus a supported build definition; Bazel-only ament
index construction does not intrinsically require package.xml. Shared metadata
is nevertheless the chosen per-repository contract. Verify colcon discovery,
resource installation/lookup and --symlink-install handling of large files;
no assertion that model payloads are never copied is accepted without testing.

Required first model gate: complete consumer suite/behavior with real indexed
model resources, relocation/materialization and fresh-checkout checks, flat
diffusion lookup, launcher runtime dependency declarations and colcon build/
symlink-install/environment sourcing. A future manifest workflow is separate.
Pin revisions and document exact
import/build/test commands and outcomes when that experiment begins.

Inspection commands:

```sh
rg -n 'diffusion_planner/(v|\$)|diffusion_planner.*v[0-9]' src/launcher/autoware_launch
rg -l 'autoware_data/ml_models' src/launcher/autoware_launch
rg -n 'data_path' src/launcher/autoware_launch/autoware_launch/launch/autoware.launch.xml
```

Verification: documentation/rule updates only; git diff --check and added local
Markdown links/anchors. No model imported, large-file object fetched, Ansible
role altered, launch parameter converted, package.xml/CMake support added or
Bazel/colcon/runtime test run. All proposed model support remains unvalidated;
Phase 6.2's existing local ONNX fixture is unchanged. The consolidated design
is recorded in a documentation-only commit; no runtime validation is claimed.

## 2026-11-18: Phase 6.2 — TensorRT common source compatibility

Purpose: fix two demonstrated standard-library compatibility blockers in the
local autoware_cuda snapshot, separately from build/SDK definitions. The first
library compile instantiates an incomplete std::basic_ostringstream in
src/profiler.cpp; add its direct `<sstream>` include. After that blocker is fixed,
compiling the real consumer fails on experimental/filesystem in public utils.hpp.
The existing compiler test uses nonexistent `__GNUC_MAJOR__` and also mistakes
Clang's compatibility version for library capability. Select `<filesystem>` with
__has_include, retaining the existing experimental fallback where unavailable.
Do not inject compiler-version macros, globally force headers or modify tests.

Inspect the original two-source library/CMake/test inventory and the current
upstream files before patching. Upstream autoware_cuda main
a5929dde0bacecae60c5f32dc34416f784a60cb4 still contains both patterns. Reviewed
2026-11-18: searches for filesystem/profiler PRs found no matching change.
AWF's Lyrical tracker #7404 is related platform work; its reported core build
explicitly excluded CUDA, so it is not matching fix/test evidence. Details and
review links belong in UPSTREAM_AWF.md.

Verification: the final native shared-library consumer gate below passes all
13 original TensorRT cases, including GPU engine builds, with no failures or
skips; the affected CUDA-utils suite passes all 17 cases. The public shared
library and original tests compile with hermetic LLVM/libc++ 21.1.8 and the
pinned TensorRT 10.16.1.11/CUDA 13.2.2 inputs. Source and definitions are separate
commits. No upstream CMake/GCC, Humble/Jazzy, ARM or GUI build was run. These are
header/feature-detection corrections, not TensorRT behavior or model changes.

## 2026-11-18: Phase 6.2 — TensorRT binary SDK

Purpose: make TensorRT's compile/link and engine-building runtime dependencies
explicit Bazel inputs, without apt installation or host GNU-library fallback
for the native inference process. There is no local TensorRT implementation or
RCR TensorRT SDK module. Inspect NVIDIA's Ubuntu 24.04 x86_64
[package index](https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/Packages.gz)
for version 10.16.1.11-1+cuda13.2, then retain exact URL/SHA-256 tuples in
third_party/tensorrt/pins.bzl. The six selected packages are libnvinfer10,
libnvinfer-plugin10, libnvonnxparsers10, libnvinfer-headers-dev,
libnvinfer-headers-plugin-dev and libnvonnxparsers-dev. The main runtime download
is about 1.85 GB; initial fetching/extraction is substantially slower than cached
build/test. No full static-library development payload is needed by this target.

Add an independent local tensorrt module under third_party/tensorrt and
select it with the root's adjacent dependency/override pair. Its extension
extracts each .deb as an ar archive into a separate temporary directory and then
extracts its single data.tar.* into the SDK repository. Keep license/document
payloads in that extraction. This is direct binary acquisition, not apt. Only
x86/Linux is declared; do not fetch unselected SBSA SDK payloads or claim ARM
compatibility. Expose scoped headers, three cc_import shared libraries and a
separate runtime file set including the builder resources. Use the version pin
to derive TENSORRT_VERSION_MAJOR instead of duplicating the selected major.

The imported vendor headers contain unused parameters. Plain includes in this
toolchain emitted -I, making these third-party warnings fail the original
Autoware library's -Werror policy. A small SDK-owned header rule exports CcInfo
system_includes, so NVIDIA headers use -isystem without weakening Autoware source
warnings or changing vendor headers. No global warning suppression is added.

Runtime closure: reuse the already pinned GNU 12.2 library archive inputs as
SDK-owned repositories. Inputs are conda-forge linux-64 archives
libstdcxx-ng-12.2.0-h46fd767_19.tar.bz2 (SHA-256
0289e6a7b9a5249161a3967909e12dcfb4ab4475cdede984635d3fb65c606f08)
and libgcc-ng-12.2.0-h65d4601_19.tar.bz2 (SHA-256
f3899c26824cee023f1e360bd0859b0e149e2b3e8b1668bc6dd04bfc70dcd659),
both under https://conda.anaconda.org/conda-forge/linux-64/.
Declare both libstdc++.so.6 and
its real versioned file, plus libgcc_s.so.1, as runtime data. A file containing
only the versioned bytes is insufficient for bare SONAME discovery. The SDK's
tensorrt_cc_test helper builds a native cc_binary, then a thin py_test launcher
resolves its executable and runtime anchors using Bazel rlocationpath/runfiles,
sets LD_LIBRARY_PATH to the declared SDK/GNU directories while retaining existing
paths, forwards native GTest XML through GTEST_OUTPUT, and execs the binary.
This keeps the normal bazel test command self-contained. Label-bind SDK-owned
helper dependencies to avoid caller-repository mappings; disable precompilation
of the shared Python entry source because its output cannot be declared in a
different consumer package. The timeout is 300 seconds, matching original CMake.
No metarepository label is needed inside the Autoware source module.

ABI/runtime audit: readelf -d and --version-info on the three imported libraries
show maximum requirements GLIBC_2.27, GLIBCXX_3.4.22 and CXXABI_1.3.11. The sm120
and PTX builder resources require at most GLIBC_2.14/GLIBCXX_3.4.18/CXXABI_1.3.9.
The selected GNU 12.2 providers cover those requirements. Builder ELF SONAMEs
are intentionally do_not_link_against_nvinfer_builder_resource_*; keep them as
data for TensorRT's bare-name dlopen, never link them as ordinary SDK libraries.
These inspected requirements do not raise the existing 2.34 ABI floor, but
execution was on Ubuntu 24.04, not a separate 2.34 runtime image.

While the full native suite runs, inspect `/proc/<native-test-pid>/maps` after
libnvinfer_builder_resource_sm120 appears. Compare paths with the resolved
SDK/GNU runfiles, not just apparent repository-name substrings: Bazel may resolve
them into its content-addressed repository cache. The engine-building snapshot
mapped the pinned libstdc++.so.6.0.30 and libgcc_s.so.1 and the extracted SDK's
sm120 builder library, without system GNU copies in that snapshot. Host NVIDIA
driver/device support remains an execution prerequisite. This is not runtime
image/driver provisioning validation.

Verification: the complete command and case counts in the following package
entry passed, including the public shared export and all SDK/runtime targets.
The candidate version is now validated for this x86 consumer. JetPack's 10.16.2
remains a distinct Phase 8 target; no patch-version equivalence is assumed.

## 2026-11-18: Phase 6.2 — TensorRT common

Purpose: adopt the current local autoware_tensorrt_common library and complete
original suite against declared SDK providers. Inspect CMakeLists.txt,
package.xml, public headers, both .cpp files and test/tensorrt_common_test.cpp.
Its 13 cases comprise three tensor-metadata cases and ten IO-reconciliation
cases using the real checked-in dummy ONNX and GPU engine building. Preserve
those original assertions, without metadata-only filtering or missing-model/
GPU skips. The ONNX fixture is 256 bytes and needs no model-repository download.

Minimum integration: add package-local MODULE/BUILD declarations and a root
local override. Export a cc_library with scoped headers, the SDK major define,
direct CUDA runtime/SDK/RCLCPP dependencies and the original library warning
policy. Export a shared library with the existing RCLCPP dynamic-dependency
provider. The native test links that actual shared export, not only a static
copy; use gtest_main, with no added source main. Declare the ONNX file in data
and supply DUMMY_ONNX_PATH with its rootpath. The SDK-owned test helper supplies
portable builder/GNU runtime loading and records all native cases in XML.

Inspect the current RCR tensorrt_cmake_module finder: it discovers headers,
three libraries and version values. Direct providers preserve these effects;
no separate Bazel finder/meta module, stdc++fs link or empty CUDA dependency-meta
module is required for this LLVM configuration. The two demonstrated source
fixes are documented separately above. Reusable SDK runtime/test support stays
with the third-party SDK; original tests and model fixture remain unchanged.

Run from the repository root with the Phase 6.1 SDK/toolchain configuration,
actual GPU/driver access, no sourced ROS environment and serialized tests:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @tensorrt//... @autoware_tensorrt_common//... @autoware_cuda_utils//...
```

Final result: eight non-test targets and both test targets pass in 21.959
seconds. TensorRT's action takes 21.1 seconds, CUDA-utils 0.4 seconds. XML
records all 13 TensorRT cases (3 metadata + 10 IO) and 17 CUDA cases, zero
failures/errors/skips/disabled cases. Actual model parsing and fresh per-case
GPU engine builds execute; native shared-library loading and the declared
runtime mappings are verified as above. No failures or dependencies are left
for a later session within this module gate. Targeted buildifier, Python syntax,
source include/guard review, added Markdown links and git diff --check pass.

No full-system DDS rerun is needed for this isolated library/SDK addition; the
previous unchanged core/backend evidence remains applicable. No Fast DDS,
upstream CMake/GCC, ARM/SBSA, JetPack 10.16.2, runtime image, real model import,
LTO or cross-machine engine-portability check was run. Phase 6.2 is complete;
local negotiated interfaces/library/examples and CUDA transport (6.3) are next.

## 2026-11-18: Reusable dependency layers under third_party

Purpose: separate product composition from independent dependency support while
keeping the contract that root owns version selection from third_party/.
The dependency direction is bazel/ -> src/ -> third_party/, with direct product
dependencies on third-party providers allowed and no upward dependencies.
Component sources and package-local definitions remain unchanged. Directory
placement does not require inventing a facade module for an included fragment.

Inspected inputs: root MODULE overrides/includes, the CUDA fragment and CRT
build/patch definitions, TensorRT's standalone module and generic runtime/test
helpers, and the remaining rclpy dependency patch. Move reusable support as
follows (earlier journal path references are normalized to the new paths so
readers can repeat those experiments using the current layout):

| Previous location | Current location |
| --- | --- |
| bazel/cuda.MODULE.bazel | third_party/cuda/cuda.MODULE.bazel |
| bazel/cuda/BUILD.crt.bazel | third_party/cuda/BUILD.crt.bazel |
| bazel/patches/cuda_crt_libcpp21_*.patch | third_party/cuda/patches/ |
| bazel/third_party/tensorrt/ | third_party/tensorrt/ |
| bazel/patches/rclpy_shared_dependencies.patch | third_party/rclpy/rclpy_shared_dependencies.patch |

Root MODULE now includes //third_party/cuda:cuda.MODULE.bazel, points the
TensorRT local override at third_party/tensorrt and selects the rclpy patch from
its dependency-owned package. CUDA's package exports its fragment/CRT BUILD and
patches. The fragment uses same-owner labels for the CRT override; no version,
archive hash, compiler flag, GPU architecture or patch content is changed.
TensorRT retains its independent module/API and same local module name, so
source consumers require no label changes. Generic dependencies/helpers contain
no Autoware-specific source or product-label dependencies. Architecture and
product selections remain in root configuration, not in component modules.

The agreed full relocation gate is the complete 13-case TensorRT-common and
17-case CUDA-utils suites plus the existing native full-core behavior/shutdown
test, which checks the relocated rclpy patch path in a real ROS Python consumer.
Run sequentially with the Phase 6 GPU/driver prerequisites and Cyclone DDS:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @tensorrt//... @autoware_tensorrt_common//... @autoware_cuda_utils//... \
  //bazel/tests:core_system_headless_launch_test
```

Result: all eight non-test targets and three test targets pass in 67.812
seconds. The uncached test actions take 22.3 seconds (TensorRT), 0.4 seconds
(CUDA-utils) and 4.6 seconds (core behavior/shutdown). XML records 13 + 17 + 2
cases, with no failures, errors or skips. Moved SDK files, CRT BUILD and patch
contents are byte-identical; source packages require no changes. Targeted
buildifier, Python syntax, Markdown links and whitespace checks pass.

No Fast DDS, ARM or cross-platform validation was run; the gate establishes the
existing x86_64/Cyclone DDS configuration only.

## 2026-11-19: Runtime packaging and deployment requirements

Purpose: make minimal host setup and container-optional execution explicit, and
prevent a second manual package inventory from becoming a deployment convention.
The authoritative requirements and planned gates are in
[PLAN.md](PLAN.md#runtime-packaging-and-deployment-requirements); this entry
records the inspected baseline and how to repeat the assessment.

Inspected inputs: images/dev.Dockerfile, bazel/tests/BUILD.bazel and
bazel/tests/core_system/test_launch.py, bazel/agnocast/with_runtime.sh,
third_party/tensorrt/test_runner.py, the existing Phase 7 scope, model-resource
convention and deferred kernel/platform discussion. Reproduce this inspection
by following the headless target's data/deps into its composed ament indexes,
reviewing both runtime launchers' environment setup, and comparing those inputs
with the proposed exported application closure.

Current evidence: the headless test launches the original autoware_core launch
with GUI disabled and checks behavior/shutdown through Bazel runfiles. Existing
runtime launchers extend inherited library paths; the TensorRT launcher is a
test entry point. The development Dockerfile provides bootstrap tooling rather
than runtime packaging. Existing passing tests establish supported target
execution, not standalone distribution, relocation or OTA. No export rule,
relocated bundle test, application OCI image or update agent is introduced here.

Decision: separate builder-host, build-environment and runtime responsibilities.
Reuse package-local runtime declarations in root-owned product packaging; verify
provider coverage before implementing automatic collection. Prefer a relocatable
native bundle, with thin OCI assembly from the same application closure and an
explicit OS/ABI baseline. Keep image paths/mounts product-owned. Kernel drivers
remain execution prerequisites; future OTA must coordinate application and host
compatibility, retain rollback and test failure/recovery. OTA implementation is
deferred and does not enlarge the current integration gate.

Milestone clarification: Phase 7 must demonstrate AWF's documented planning
simulator, beyond the current core launch test. Reviewed the
[versioned official tutorial](https://docs.autoware.org/1.4.0/tutorials/ad-hoc-simulation/planning-simulation/)
on 2026-11-19: its lane-driving flow supplies the initial user-facing reference.
Record source/tutorial alignment and exact scenario inputs when implementing;
retain RViz demonstration plus equivalent headless checks. Packaging can be
checked in a clean QEMU guest using only delivered artifacts and declared inputs.
[QEMU system-emulation documentation](https://www.qemu.org/docs/master/system/introduction.html)
confirms that this is a guest-OS environment; OCI execution needs a container
runtime there. Functional/resource/shutdown checks apply, while emulated
real-time performance does not gate success. Guest hardware coverage must be
stated separately; neither a VM runner nor simulator validation is implemented.

Verification: documentation-only change; review the source/target references,
check local Markdown links/anchors and run git diff --check. No build, runtime,
container, GPU or middleware rerun is required for unchanged code. No runtime
portability or OTA success is claimed. Next: continue Phase 6.3; execute the
specified export/relocation/OCI behavior gates when Phase 7 begins. DEVELOPING.md
remains unchanged because packaging is not yet a supported developer workflow.

## 2026-11-19: Phase 6.3.1 — Negotiated interfaces

Purpose: supply the local negotiation protocol without selecting a registry
implementation. Inspect src/core/cuda/negotiated/negotiated_interfaces/package.xml,
CMakeLists.txt and all four msg files. They declare no external message-package
dependencies: NegotiatedTopicsInfo references NegotiatedTopicInfo, and
SupportedTypes references SupportedType. No original runtime suite is registered.

Add package-local MODULE/BUILD files using rosidl_default_generators and four
explicit ros_message targets under msg/. Declare the two internal edges, then
pass all four roots and package.xml to default_generators. Root selects the
local module with version 0.0.0 and local_path_override; retain 0.1.0 inside
the source module. Existing sources/manifest/CMake remain unchanged. The pattern
is the existing message convention; no source-completeness validator is needed.

Verification command from the repository root, without a sourced ROS setup:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=no-agnocast @negotiated_interfaces//...
```

Result: all 15 generated targets build in 52.925 seconds (76 sandboxed actions,
10 internal actions, 3264 cache hits). Targeted buildifier and whitespace checks
pass. The next negotiated-library gate exercises these types in real
publisher/subscription behavior. Cyclone DDS is selected;
no Fast DDS compatibility run is scoped. Runtime GPU/kernel devices are not
needed for this package.

## 2026-11-19: Phase 6.3.2 — Negotiated publisher/subscription library

Purpose: validate the actual negotiation protocol and provide a shared library
for its examples and CUDA transport consumers. Inspect the local negotiated
package's CMakeLists.txt, package.xml, both src files, public include tree and
all three original test files. Inventory: 12 combinations, 32 publisher and 32
subscription cases; no disabled/skip cases. These native rclcpp tests exercise
negotiation, renegotiation, supported/compatible types and callbacks. They do
not require GPU access or the Agnocast kernel module.

Minimum pattern: package-local MODULE/BUILD declarations depend on the local
negotiated_interfaces module and RCLCPP; root chooses its local override. Export
a cc_library with scoped headers and the original warning flags, a shared
libnegotiated.so with ROS/message shared providers, and ament metadata. Expose
all original tests through cc_test/gtest_main, dynamically linking the actual
shared export and std_msgs support. Serialize tests because they use common
ROS node/topic names; select a dedicated ROS domain and Cyclone DDS. No source
or original test changes are made by these definitions.

Verification command:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @negotiated//...
```

Result: all four non-test and three test targets pass in 7.882 seconds; XML
records all 76 cases, zero failures/errors/skips or disabled cases (0.1 seconds
combinations, 3.8 publisher, 3.2 subscription). Targeted buildifier and whitespace
checks pass. No source changes were needed.

Diagnosed failure: the first test link reported undefined rclcpp symbols.
Linking only negotiated_shared does not place its transitive ROS shared exports
on the test link line. Add runtime_dynamic_deps explicitly to each test's
dynamic_deps, retaining shared ownership rather than linking duplicate static
ROS implementations. The complete suite above passes after that correction.

No GPU or Agnocast test is part of this native package gate. Their devices were
confirmed available outside the restricted tool sandbox; sandbox-local absence
must not be diagnosed as a host driver failure. No examples/component behavior
or CUDA transport completion is claimed. Next are the original example
executables and launches, then cuda_blackboard with its full prerequisites.

## 2026-11-19: Phase 6.3.3 — Negotiated example executables and launches

Purpose: expose the entire original negotiated_examples package, not just its
library dependency. Inspect its CMakeLists.txt, package.xml, eight component
sources, lifecycle publisher, common type-info header and four launch files.
No original runtime tests are registered. Acceptance: build all nine original
executables/shared components and run all four original composed/remapped
launches plus standalone, string-b, compatible publisher/subscriber, type-adapted
and lifecycle scenarios, asserting actual downstream delivery and clean exits.
Lifecycle coverage configures, activates, delivers and deactivates the publisher.
These are CPU/native-rclcpp examples; a GPU test would not exercise their code.

Integration pattern: package-local MODULE/BUILD definitions expose scoped source
libraries, uniquely named component shared libraries, native RCR component
executables/registration records and ament-indexed launch files. Keep lifecycle
as its original plain executable. Use the negotiated shared export plus its
transitive runtime provider, std_msgs, native components and lifecycle runtime
providers to preserve shared ownership. Register executable files under the
package's libexec directory and compose the component-container runtime provider.
No source changes or root-label dependencies are added to the source module.

Added behavior tests live under bazel/tests/negotiated_examples/. The adapter
includes the original launch files unchanged; standalone pairs use their indexed
executables. Select scenarios through declared test environment variables and
serialize the suite in one ROS domain. Assert receiver-specific callback logs,
including the downstream namespace in the unremapped pair. ROS console output
is stderr: checking stdout falsely reports timeouts despite successful delivery.
The lifecycle peer uses the actual change-state/get-state services.

Commands (repository root, no sourced ROS):

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=no-agnocast @negotiated_examples//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests:negotiated_examples_tests
```

Result: all 52 package targets build in 84.318 seconds. All ten scenarios
pass both delivery and shutdown checks (20 XML cases, no failures/errors/skips).
Their final run is included in the 54.958-second combined launch gate; the
separate image-pipeline failure from that run is diagnosed and fixed below.
Targeted buildifier, Python syntax and whitespace checks pass. No source changes
were needed. No Fast DDS or GPU behavior claim applies to this CPU package.

## 2026-11-19: Preserve CUDA example image metadata

Purpose: preserve image identity when the original cuda_blackboard example
subscriber converts a GPU image back to a ROS image. Before the fix, the real
component pipeline delivers pixels but output_image.header.frame_id is empty
instead of camera_frame, and the timestamp is zero. This is a source behavior
bug, independent of Bazel, ROS distribution or CUDA version.

In src/core/cuda/cuda_blackboard/src/cuda_blackboard_subscriber_node.cpp, copy
cuda_msg->header to ros_image->header alongside the existing encoding/dimension
copies. Do not change the launch, transport selection or GPU-copy behavior.
The integration test is kept outside the original package, under
bazel/tests/cuda_blackboard/test_launch.py. Reproduce with the component-test
command in the Phase 6.3.4 entry: before the fix it fails the frame assertion;
afterward both payload/metadata delivery and clean shutdown pass (3.5 seconds).
The native GPU round-trip tests separately confirm metadata survives the library
conversion; the loss occurs only in the demo subscriber's manual reconstruction.

Upstream main a4f330e5c228d713774450b0331b83b94c55a466 still omits the copy
(reviewed 2026-11-19). Inspected subscriber-file changes for PRs 14, 21, 22, 27
and 29; none adds this header assignment. These are related include/stream/
allocation/Agnocast changes, not a matching fix. The Lyrical tracker is likewise
not evidence of a metadata correction. UPSTREAM_AWF.md records the useful
finding; no upstream submission was made. Keep the source correction in a
separate fix commit from package build definitions.

## 2026-11-19: Phase 6.3.4 — CUDA blackboard transport

Purpose: integrate the full local cuda_blackboard package, including its eight
original cases and image publisher/subscriber components. Inspect CMakeLists.txt,
package.xml, all headers/sources, original tests/fixtures and example.launch.py.
Gate: real GPU execution without skips, original mailbox/publisher/async/worker
suites, enabled publisher/async variants, exact image/point-cloud device round
trips and cross-process compatible payload delivery in both backends, plus the
original composed image pipeline's payload and shutdown behavior. Revalidate
CUDA-utils/TensorRT execution on the restored NVIDIA 595.99.02 driver. Driver
and Agnocast devices must be checked outside sandbox restrictions.

Package-local definitions compile Agnocast support unconditionally through the
wrapper and configurator providers. The shared blackboard owns process-wide GPU
state; components/tests link its actual shared export. Original ROS nodes remain
native components; only the compatible publisher/async worker use backend
selection. Use pinned CUDA runtime directly rather than host CUDA discovery.
New payload and launch adapters/fixtures live under bazel/tests/cuda_blackboard/.
Keep the original mailbox test header-only and CUDA-free, as its CMake target
specifies; the subscriber component needs no direct cv_bridge/OpenCV edge.

The cv_bridge dependency is absent locally. Its registry 0.0.0 placeholder has
no BUILD file; select the published 4.1.0-3.rcr.2 release at root, not a source
implementation substitution. Reuse its ROS/OpenCV shared provider closure for
components. Runtime GPU access requires NVIDIA driver/device visibility; enabled
Agnocast transport requires the module/device and built heap hook. The worker
announcement original case needs an unused IPC listener endpoint. The initial full package/GPU gate builds 24 non-test and six test targets in
105.922 seconds. XML confirms all eight original blackboard cases, 17 CUDA-utils
cases and 13 TensorRT cases pass with no failures/errors/skips. The later
enabled variants use separate suite targets; reproduce the baseline tests
with the explicit original target list below. TensorRT actually
builds engines (21.6 seconds); CUDA-utils takes 0.6 seconds. Run:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @cuda_blackboard//:test_publish_mailbox \
  @cuda_blackboard//:test_cuda_blackboard_publisher \
  @cuda_blackboard//:test_cuda_blackboard_async_publisher \
  @cuda_blackboard//:test_start_worker_thread \
  @autoware_cuda_utils//... @autoware_tensorrt_common//...
```

The original worker target explicitly sets ENABLE_AGNOCAST=1, even with the
RCLCPP configuration selected for the other original cases. For direct enabled
transport, test the two added variants with the built heap hook; the exact
payload binary is exercised in RCLCPP mode in the same serialized gate:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  --run_under=//bazel/agnocast:with_heaphook \
  --test_env=AGNOCAST_BRIDGE_MODE=off \
  @cuda_blackboard//:test_cuda_blackboard_publisher_agnocast \
  @cuda_blackboard//:test_cuda_blackboard_async_publisher_agnocast \
  //bazel/tests:cuda_blackboard_payload_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --run_under=//bazel/agnocast:with_heaphook \
  --test_env=AGNOCAST_BRIDGE_MODE=off //bazel/tests:cuda_blackboard_payload_test
```

These pass in 10.401 and 3.139 seconds respectively: three original cases repeat
with ENABLE_AGNOCAST=1, and three added payload cases pass in each backend, zero
failures/errors/skips. The payload test verifies actual device allocations,
full image/point-cloud data and metadata round trips, and an async publisher
sending an exact GPU-derived image to a polling peer in a separate exec'd
process. posix_spawn avoids inheriting a live CUDA context through fork. Use
polling depth 1 for Latest; SensorDataQoS's default depth 5 correctly throws.
The bridge is deliberately off for this direct transport test; no new bridge
coverage is claimed. Only targets declaring heap-hook data can use this runner;
applying it to baseline targets without that data fails before test startup.
Run those baseline targets normally as above.

Original composed image example gate:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests:cuda_blackboard_component_test
```

The adapter creates a temporary PPM fixture, invokes the original example.launch.py unchanged, and checks indexed
component loading, exact BGR pixels/dimensions/encoding, frame/timestamp and clean
shutdown. It exposed the subscriber dropping the original image header; the
separate source-fix entry records the one-line correction. Both final
cases pass in 3.5 seconds (11.641-second build/test invocation). Other package
results remain applicable: only the demo subscriber source changed, not the
library, original unit tests, payload binary or negotiated examples.

Final supported workflow uses package test suites to avoid applying the heap
hook to targets without its data. The following complete RCLCPP gate passes all
six targets in 39.622 seconds, including the final demo fix and minimal
dependency declarations (component 4.5 seconds, payload 0.7 seconds). The enabled suite also passes all three targets in
11.566 seconds (1.5-second payload, 1.5-second async and 0.5-second publisher),
with zero failures/errors/skips. The paired commands are:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 --nocache_test_results \
  --test_output=errors @cuda_blackboard//:unit_tests \
  //bazel/tests:cuda_blackboard_payload_test //bazel/tests:cuda_blackboard_component_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --local_test_jobs=1 --nocache_test_results --test_output=errors \
  --run_under=//bazel/agnocast:with_heaphook --test_env=AGNOCAST_BRIDGE_MODE=off \
  @cuda_blackboard//:agnocast_tests //bazel/tests:cuda_blackboard_payload_test
```

Targeted buildifier, clang-format, Python syntax, Markdown links and whitespace
checks pass. No source changes beyond that demonstrated metadata bug are needed.
Phase 6.3 is complete. No Fast DDS, bridge matrix, performance, ARM/Thor or
runtime-bundle/OCI test was run. Phase 6.4's representative GPU consumer is next.

## 2026-11-19: Phase 6.4.1 — Sparse-convolution SDK compatibility

Purpose: prepare the actual external dependency required by the sparse plugins
before declaring the full autoware_tensorrt_plugins package integrated. Inspected
its complete CMake source list, eleven creators and reference tests; sparse
creators include generated spconvlib headers and call APIs accepting TensorView,
std::vector, std::string and unordered_map values. Kernel tests alone do not
exercise this boundary. The related Phase 6.4 consumer group also includes the
local classifier and YOLOX packages, with separate model/resource adoption gates.

Inspected reference provisioning: ansible/roles/spconv selects spconv 2.3.8 and
cumm 0.5.3, cu130-rev1 on Ubuntu 24.04. Download the immutable release assets
without installing them:
https://github.com/autowarefoundation/spconv_cpp/releases/tag/spconv_v2.3.8%2Bcumm_v0.5.3%2Bcu130-rev1
The amd64 cumm payload SHA256 is
30c776917c8082a5c54264293b9dbdfae15c858c390b99b4d55e61d82ade1833;
spconv is 24e08b4af087059dc5758627303356b441bd5e9296a2d25876f2a77c20acb0ff.
Both downloaded files match the release asset digests. Inspect with dpkg-deb -x
and readelf -d/--version-info, without apt installation or pip imports.

Observed SDK closure: libspconv.so needs libcudart.so.13, libcublasLt.so.13,
libstdc++.so.6, libgcc_s.so.1 and baseline OS libraries. Its maximum versioned
requirements include GLIBC 2.34, GLIBCXX 3.4.32 and CXXABI 1.3.9. Public API
symbols use GNU std::__cxx11 rather than libc++ std::__1; the existing pinned
GNU 12 runtime is insufficient for this SDK. Treat binary C++ ABI compatibility
as a separate question from whether TensorRT itself loads GNU runtime libraries.

Integration: third_party/spconv owns a repository rule extracting each Debian
ar/data archive, applying the documented TensorView header correction and
exporting system headers plus libspconv.so. Root selects this module and adds
cuBLAS, cuRAND and NVRTC to the checksum-pinned CUDA 13.2.2 manifest. These are
actual consumer requirements: libspconv needs cuBLASLt; TensorView includes
curand.h and cuda/nvrtc.h even in the plugin host compilation. No apt/pip or
host CUDA/GCC installation supplies build inputs.

Clang 21 first fails compiling Tensor::astype: the callback-based conditional
instantiates invalid std::copy conversions for forbidden integer/half and
half/bfloat pairs. Replace only that conditional with C++17 if constexpr,
preserving its allowed-pair predicate and rejection path. The dependency patch
records the complete diff and upstream status. The probe checks float/half
round trips and rejection of int64-to-half as well as the real SDK API.

After headers compile, the default libc++ consumer cannot link StaticAllocator:
its std::__1 container symbols differ from the vendor's std::__cxx11 symbols.
Use the pinned LLVM module's GNU 13.3.0 platform/runtime, built from source,
only for the SDK's C++ consumer closure. third_party/spconv/defs.bzl exports
configured shared-library/test rules and a GNU runtime filegroup. Transition at
the public shared-library or test boundary; ordinary underlying cc_library
rules remain visible to cc_shared_library's link graph. Select dynamic C++
runtimes explicitly on those shared targets: this toolchain does not provide
static libstdc++ linking. The rest of Autoware remains on libc++.

The acceptance probe constructs and updates StaticAllocator with real
unordered_map/string/Tensor values, transfers integers to the GPU, invokes
SpconvOps::sort_1d_by_key and checks sorted values and the returned permutation
after copying back. This validates container ABI, ownership and SDK GPU code,
rather than relying on a successful dlopen. Run with a working NVIDIA driver
and device access, using the existing root/user resource settings:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @spconv//:abi_test
```

Final probe passes in 0.5 seconds (6.419-second invocation), including the
conversion-policy checks. Earlier combined plugin validation also passes the
same allocator/GPU boundary. GNU runtime construction initially took about four
minutes; subsequent runs reuse declared build outputs. Module inputs are pinned;
no host compiler/runtime search fallback was added. Validated on x86_64,
CUDA 13.2.2, sm_120 and NVIDIA 595.99.02. ARM/JetPack, other vendor SDK releases
and numerical/performance coverage of every sparse operation remain untested.

## 2026-11-19: Phase 6.4.2 — Complete TensorRT plugin library

Purpose: expose the whole local autoware_tensorrt_plugins package, not just its
reference kernels. Inventory the original CMake lists, plugin_registration.cpp,
eleven creator implementations and all three original test sources. The gate
is seven CUDA sources, twenty-four C++ sources, eighteen original parameterized
cases and an added native consumer loading/serialization/GPU check. Reuse the
accepted sparse SDK from Phase 6.4.1; no model download is required.

Add package-local MODULE/BUILD definitions and a root local override. Build
cuda_ops with nvcc/C++17, relaxed constexpr, extended lambdas and the existing
1675 diagnostic suppression. Build all plugin C++ implementations/creators with
the spconv-owned GNU platform and alwayslink to preserve static registration.
Export libcuda_ops.so and libautoware_tensorrt_plugins.so through configured
cc_shared_library targets; use dynamic_deps to avoid duplicate kernel linkage.
Keep their underlying implementation libraries manual: public targets build
them in the matching GNU configuration. Register the plugin under the package's
share/.../plugins directory and cuda_ops in its ament index. Do not change the
original CMake, implementation or tests.

Add the interoperability test under bazel/tests/autoware_tensorrt_plugins.
It deliberately compiles with libc++ and loads the opaque DLL with dlopen,
matching autoware_tensorrt_common. Enumerate all eleven creators, build an Argsort
engine through TensorRT, serialize/deserialize it, execute on the GPU and assert
exact output indices. Do not pass STL values or use RTTI across the GNU/libc++
boundary. Query TensorRT's versioned interface metadata before its ABI-defined
cast. The SDK/plugin contract here is the TensorRT interface, not the concrete
spconv C++ API.

Two test-adapter failures clarify this contract: dynamic_cast across these
standard-library runtimes returns null despite a valid versioned creator;
TensorRT's loadLibrary API rejects duplicate creator names because these sources
already self-register during dlopen. Use the existing Autoware dlopen path and
getInterfaceInfo checks. Neither requires changing the original implementation.
The SDK-owned tensorrt_cc_test helper gains an optional libstdcxx_runtime anchor
so this test can select the declared GNU 13 runtime; existing consumers keep
their default runtime and receive a regression run.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @spconv//:abi_test @autoware_tensorrt_plugins//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @autoware_tensorrt_common//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests:tensorrt_plugins_test
```

Results: the full SDK/plugin invocation completes in 77.220 seconds, builds
five public non-test targets and passes the SDK probe plus all eighteen original
reference cases. The unchanged TensorRT-common suite passes all thirteen cases
in 21.0 seconds during the subsequent combined gate. After correcting only the
interop adapter, its final invocation completes in 4.643 seconds and passes
both cases. Native XML reports zero failures, errors and skips. Targeted
buildifier, clang-format and whitespace checks pass. No Fast DDS, ARM/Thor,
performance or deployment-bundle claim is made. Sparse creators are compiled
and loaded; individual sparse-convolution numerical inference is not covered
by the package's original suite. Phase 6.4 remains in progress: classifier and
YOLOX are separate package gates.

## 2026-11-19: Classifier filesystem compatibility

The complete classifier C++ build fails under Clang/libc++ 21.1.8 because
fileExists hard-codes std::experimental::filesystem. The selected standard
library provides C++17 std::filesystem. Add its direct header and use that
namespace; no calibration, inference or original test logic changes. This
source compatibility correction is committed separately from Bazel definitions.

Reproduce the failure with the uncorrected source and the package-local rules
from Phase 6.4.3: bazel build --config=no-agnocast
@autoware_tensorrt_classifier//.... The compiler reports no member experimental
in namespace std at both references on the file-existence condition. With the
correction, all seven public package targets build in 5.455 seconds. The five
added inference/preprocessing cases then pass in 21.2 seconds (27.215-second
invocation), including fresh engine builds and cached loads. See the following
entry for exact commands/fixtures. No original runtime tests exist in this
package. Upstream source/related tracker association is recorded in
UPSTREAM_AWF.md; upstream CMake/GCC and other platforms are not validated.

## 2026-11-19: Phase 6.4.3 — TensorRT classifier

Purpose: integrate the complete local classifier and its GPU preprocessing
library. Inspect CMakeLists.txt, public headers, constructor/inference dispatch
and preprocess.cu. There are no original runtime tests; CMake registers only
lint. Define the behavior gate before implementation: fixed-batch numerical
inference, CPU resizing/letterboxing/normalization, cached engine reload,
preallocation/reallocation and direct numeric CUDA preprocessing. Reuse the
accepted CUDA/TensorRT/common providers; no pretrained model is necessary for
this library contract.

Add package-local MODULE/BUILD definitions, root local override and ament index.
Build preprocess.cu as a CUDA/C++17 library, and the classifier as a separate
shared library with dynamic dependencies on it and TensorRT-common/RCLCPP.
Direct CUDA runtime, cuBLAS and cuRAND headers replace the CMake-only finders;
no empty dependency-meta module or stdc++fs link is needed with libc++.
Preserve OpenMP using the existing NDT pattern: pinned LLVM libomp headers plus
a native cc_import of its declared archive, with -fopenmp on the classifier.
This preserves shared-link graph visibility. The hard-coded experimental
filesystem compile blocker is corrected in the separate source-fix commit.

Put new tests under bazel/tests/autoware_tensorrt_classifier. The checked-in
166-byte ONNX fixture computes spatial channel means on [2,3,2,2] inputs;
make_fixture.py regenerates its explicit ONNX IR8/opset13 protobuf with Python's
standard library. TensorRT parsing/building supplies actual format validation.
Copy it into TEST_TMPDIR before engine creation; never write engine caches into
source/runfiles. Fixed colored images with custom normalization yield classes
[2,0] and scores [12,45], or [6,22.5] after half-image zero letterboxing. Check
both constructor cuda flags, repeated output-vector clearing, buffer resizing
and fresh/cached engines. A separate direct CUDA helper check verifies both
batches, RGB channel reversal, default mean/std and zero padding numerically.

Important existing behavior: doInference always calls preprocess_opt (CPU), even
with cuda=true. The private GPU path also has a different channel/normalization
contract from CPU preprocessing. Preserve that implementation; do not claim the
constructor flag selects CUDA preprocessing or assert equivalence between the
two paths. The GPU helper is tested directly and inference itself runs on GPU.
No INT8/calibration accuracy or pretrained traffic-light accuracy is claimed.

```sh
python3 bazel/tests/autoware_tensorrt_classifier/make_fixture.py
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=no-agnocast @autoware_tensorrt_classifier//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests:tensorrt_classifier_test
```

All seven public package targets build (5.455 seconds); all five native cases
pass (21.2-second test, 27.215-second invocation), zero failures/errors/skips.
Logs show fresh engine creation and subsequent Loading engine paths. Targeted
buildifier, clang-format, Python syntax and whitespace checks pass. Same pinned
x86_64 CUDA13.2.2/TensorRT10.16.1/driver595 environment as the plugin gate. No
Fast DDS, ARM, model repository or packaging test was needed for this boundary.
Phase 6.4.4's YOLOX and its real model/resource closure remain to integrate.

## 2026-11-19: Phase 6.4.4 — Legacy utility header facade

Purpose: satisfy YOLOX's actual autoware_utils/ros/debug_publisher.hpp and
system/stop_watch.hpp includes from the local snapshot. Inspect the facade's
CMake/package/header inventory: it forwards compatibility namespaces to the
eleven already supported split utility packages, has no implementation sources
and no original runtime suite. Do not replace its local headers with registry
sources or edit consumers merely to rename includes.

Add one package-local MODULE/BUILD and root override. Export all existing
headers with include/ stripped, depending on all eleven split packages. Their
previously accepted suites remain applicable because their code/configuration
is unchanged. Add a root standalone compile/runtime consumer of the original
aggregate header; assert StopWatch's namespace alias denotes the same split
utility type and construct/read the timer. This checks the full public include
closure and the compatibility namespace required by YOLOX.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --nocache_test_results --test_output=errors \
  //bazel/tests:autoware_utils_facade_test
```

Passes as part of the sequential utility gate (4.195-second invocation), zero
failures/skips; no implementation or original test changes. Targeted buildifier,
clang-format and whitespace checks pass. This facade introduces no new utility
algorithm or middleware behavior, so no transport matrix or Fast DDS check is
required. Perception utilities, messages and the model/YOLOX boundaries follow.

## 2026-11-19: Phase 6.4.4 — Perception utilities

Purpose: prepare YOLOX's local run-length mask encoder without omitting the
rest of perception_utils. Inspect its CMake/package, all three implementations
and the complete existing suite: IoU BEV suppression (five cases), class
remapping (two) and run-length encode/decode (one). The package also exports the
existing prime_synchronizer.hpp header. Reuse the accepted local object-
recognition, message and geometry providers and registry OpenCV/RCLCPP inputs.

Add local MODULE/BUILD/root override, a header/source library and shared export.
Collect the complete transitive dynamic providers, including OpenCV itself.
Expose each original test unchanged. test_utils.cpp supplies main and therefore
links gtest, while the other two link gtest_main. Initial setup failures were a
duplicate main and then missing cv::Mat::zeros: allowing the utility shared
library to own OpenCV statically pruned methods not referenced by that library.
Declaring @rosdistro//cc:opencv as a separate dynamic provider preserves its
complete export for both library and test consumers.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @perception_utils//...
```

Final combined utility invocation completes in 4.195 seconds; all eight original
cases pass across three targets (0.3/0.4/0.3 seconds), no failures/errors/skips.
No original source/test changes are required. Targeted buildifier and whitespace
checks pass. This adds no changed DDS behavior or Agnocast backend; no Fast DDS
check or broad communication matrix was run. Next: local Tier 4 interfaces and
real model resources before the whole YOLOX gate.

YOLOX consumer follow-up: linking the entire utility library with the legacy
utility facade exposes cc_shared_library's linked-but-not-exported ownership
checks for unrelated geometry/planning dependencies. YOLOX only uses the
run-length mask codec. Split that existing source/header into
run_length_encoder_library and run_length_encoder shared targets with only
OpenCV dependencies. The complete perception_utils library depends on that
same canonical codec target and composes its shared provider, avoiding duplicate
implementations. Preserve all other public headers and original suites.
The same full command passes all eight cases again (4.277-second invocation,
0.3/0.4/0.3-second targets); no source changes. This target split addresses the
actual consumer graph instead of exporting unrelated implementation symbols.

## 2026-11-19: Phase 6.4.4 — Tier 4 perception interfaces

Purpose: provide YOLOX's feature-bearing detection messages from the local
Tier 4 snapshot. Inventory package.xml and the fourteen interface sources
explicitly registered in CMake (nine object-recognition and five traffic-light
messages). Preserve those declarations; two other undeclared source files do
not enter the Bazel graph merely because they exist.

Add package-local MODULE/BUILD files and ros_message declarations beside the
original sources, with exact referenced-type dependencies. Root default_generators
composes those fourteen targets and existing local Autoware/standard ROS message
providers. Register the new package once in bazel/interfaces:all_interfaces;
consumer-specific lists remain unnecessary. Add a standalone root serialization
test for DetectedObjectsWithFeature, including nested ROI, object classification,
probability and header fields. No original runtime suite is registered.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=no-agnocast @tier4_perception_msgs//:c \
  @tier4_perception_msgs//:cc @tier4_perception_msgs//:py \
  @tier4_perception_msgs//:ament_index //bazel/interfaces:all_interfaces
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests:tier4_perception_roundtrip_test
```

All selected language/index/collection providers build in 197.131 seconds;
the serialization case passes in 0.1 seconds (4.373-second invocation), zero
failures/errors/skips. RCR's standard interface exports generate both FastRTPS
and introspection typesupport even with Cyclone selected. This is dependency
code generation, not a Fast DDS middleware/configuration acceptance run. An
initial broad wildcard invocation was interrupted; use explicit supported
providers for this gate. No --config=fastdds test or runtime check was run.
Targeted buildifier, clang-format and whitespace checks pass. Agnocast plugin
assembly can consume the expanded collection, but no new bridge/runtime matrix
is claimed by this message-only task. Model and full YOLOX gates follow.

## 2026-11-19: Decompressor Lyrical Buffer compatibility

The actual decompressor build fails at cv::Mat(compressed_image.data): Lyrical's
message byte sequence is rosidl Buffer, not std::vector, so OpenCV's vector-only
constructor is inapplicable. Replace that construction with a one-row CV_8UC1
view over data()/size(). cv::imdecode reads the view while the original message
remains alive; the const_cast satisfies OpenCV's non-const Mat view API without
changing input ownership or adding a payload copy. Add the direct cstdint
include. Preserve all decode, encoding and exception behavior.

Validate with the entire @autoware_image_transport_decompressor//... test/build
gate described below; both original suites pass on Lyrical/libc++ after the
correction. Upstream current source still has the vector-specific construction;
UPSTREAM_AWF.md records the inspection and related tracker. This source fix is
separate from Bazel definitions. Older ROS/CMake builds are not tested; the
contiguous view supports both the old vector and new Buffer API.

## 2026-11-19: Phase 6.4.4 — Image transport decompressor

Purpose: satisfy the existing YOLOX launcher's optional compressed-image input
chain with the local package. Inventory its two implementation libraries,
component/executable, launch/config files and both complete existing suites:
forty-one decoder parameter/edge cases and three ROS-node delivery cases.
The malformed-encoding cases characterize existing behavior, including known
inconsistent metadata; preserving their passing expectations is not a claim
that those upstream behaviors are corrected.

Add package-local MODULE/BUILD and root override. Build the decoder, its ROS
component and generated standalone executable. Export RCLCPP/CV-bridge/OpenCV/
sensor-message dynamic providers, the component record and original launch/
configuration/executable resources through the package-owned index. Existing
node tests include the original src/ header: declare it in the node library,
without moving or editing those tests. Register the component record with
ament_index.components; rclcpp_components_register_node does not create a
separate *_ament_index target. The byte-buffer compile blocker is fixed in the
preceding independent source commit.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @autoware_image_transport_decompressor//...
```

All twelve non-test targets build; all forty-four original cases pass across
two targets (decoder 0.1 seconds, node 1.1 seconds; 22.562-second invocation),
zero failures/errors/skips. The node tests exercise actual compressed-to-raw
ROS delivery, requested encoding and rejection of undecodable data. Targeted
buildifier, clang-format and whitespace checks pass. These are native rclcpp
nodes with Cyclone DDS; no Agnocast rewrite, Fast DDS or separate component
runtime matrix is claimed. Continue with repository-owned model resources and
YOLOX's complete GPU suite.

## 2026-11-19: Phase 6.4.4 — Repository-owned YOLOX models

Purpose: supply the real ONNX detection/segmentation inputs required by all
existing YOLOX tests, while exercising editable repository-owned model modules.
Inspect the reference artifact task, actual tests and launch defaults. All
required detection and segmentation weights/labels/color maps are in the single
AutowareFoundation/tensorrt_yolox repository; importing the separate fine-
detector repository is unnecessary for this consumer. Use tag v1.0, immutable
commit 424d6b9cb94c939ae9ce6a22380e46c13f76b1da, from
https://huggingface.co/AutowareFoundation/tensorrt_yolox. Its README declares
Apache-2.0; retain that model documentation/license metadata.

Clone into an isolated temporary directory with GIT_LFS_SKIP_SMUDGE=1, run git
lfs pull there, and compare all five payload SHA256/size pairs against the HF
revision API's LFS metadata. Keep full immutable inputs in MODEL_PROVENANCE.md
and SHA256SUMS inside the repository boundary. Import as a squashed subtree at
src/models/tensorrt_yolox. Direct HF fetch negotiation with the large parent
repository failed with expected acknowledgments; fetching/unshallowing the
isolated model clone and importing that local Git revision succeeds. The
recorded upstream URL/revision remain unchanged.

Preserve the original nested .gitattributes and LFS pointers, materializing
actual bytes in the working tree. Run git lfs install --local in the parent;
normal pushes must mirror those objects to the parent repository's own LFS
remote. Do not configure one HF endpoint for the whole metarepository: it would
break independently edited weights and other model repositories. SHA256SUMS
represents current checked-in payloads; model edits update it alongside related
metadata, while the immutable import provenance remains recorded separately.

Add one repository-owned autoware_tensorrt_yolox_models MODULE/BUILD/package.xml
and minimal ament_cmake CMakeLists. :models exposes weights/metadata; register
original filenames into share/autoware_tensorrt_yolox_models with its own ament
index. CMake installs that same set; remove CMakeLists.txt from the *.txt glob.
No aggregate model package or Ansible/repositories reference change is needed.
Root selects the local module; consumers own model/runtime choices outside it.

```sh
# In an isolated model checkout at the recorded revision:
git lfs pull
sha256sum -c SHA256SUMS
# From the Bazel product root:
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests:yolox_model_resources_test
# Separately, in an existing sourced colcon/ROS development environment:
colcon build --base-paths src/models/tensorrt_yolox --symlink-install
. install/setup.bash
python3 -c 'from ament_index_python.packages import get_package_share_directory; print(get_package_share_directory("autoware_tensorrt_yolox_models"))'
```

Initial results: all five HF hashes/sizes match; both added model checks pass
(0.7-second test, 2.388-second invocation), including a materialized standalone
relocated prefix with no source-path lookup. A separate existing Jazzy/colcon
environment discovers/builds this model-only package in 0.77 seconds; after
sourcing its install, all five indexed ONNX paths are symlinks to matching real
payloads. This host ROS installation is used only for the requested colcon
compatibility experiment, never as a Bazel dependency provider.

Storage gate: git lfs push paulsohn bazel uploads all five objects (238 MB) to
the project remote. Create a fresh clone with --no-local --depth=1 --no-checkout,
sparse-check out src/models/tensorrt_yolox with GIT_LFS_SKIP_SMUDGE=1, select the
project remote as origin, then git lfs install --local and git lfs pull. Verify
its independent .git/lfs/objects is initially empty: a --shared clone reuses
local LFS objects and does not establish remote retrieval. All five fresh
payloads match SHA256SUMS after downloading from the project remote. These are
newly stored project objects, not an HF-only fallback; normal Git LFS storage
also accepts subsequent edited weights. No synthetic weight change is committed.

The fresh checkout's colcon build passes in 0.85 seconds, and indexed lookup
again confirms all five weight symlinks and hashes. Repeat the Bazel model test
against the fresh module:

```sh
bazel test --config=no-agnocast \
  --override_module=autoware_tensorrt_yolox_models="$model_fresh/src/models/tensorrt_yolox" \
  --sandbox_add_mount_pair="$model_fresh" --nocache_test_results \
  --test_output=errors //bazel/tests:yolox_model_resources_test
```

Both cases pass in 0.7 seconds (1.946-second invocation). The temporary checkout
is inside /tmp; Bazel's private test /tmp initially hides the ultimate targets
of its external-repository runfile symlinks. Mounting exactly that selected
checkout into the test namespace fixes the missing-file failure. This is a
local experiment invocation, not a committed absolute model path or a host
model-directory fallback. The test also materializes an independent relocated
runtime prefix and resolves its own package index.

Model storage/index/build gates are complete. YOLOX inference and affected
launcher path/dependency changes belong to the following consumer task. Other model repos,
the global model data_path removal and flat diffusion validation remain their
own consumer migrations; no HOME fallback is allowed for this adopted model.

## 2026-11-19: YOLOX dependency-header and filesystem compatibility

The complete local YOLOX build exposes three compiler/dependency blockers:
experimental/filesystem is absent from libc++21; the single-image node selects
that header through nonexistent __GNUC_MAJOR__; and label.cpp instantiates
stringstream without including sstream. Use the C++17 filesystem header/namespace,
select the single-image node's existing fallback with __has_include, and add the
direct sstream include. No inference, decoder or original test behavior changes.

A further detector-header failure is a dependency-layout mismatch: the selected
RCR provider exposes TartanLlama expected under tl/expected.hpp, while the local
snapshot expects the ROS tl_expected wrapper. Inspect the current AWF header
history and PR #13622's actual patch. Match its already merged correction:
include tl/expected.hpp and replace package.xml's tl_expected dependency with
libexpected-dev. Bazel consumes the pinned @rosdistro//cc:tl_expected alias to
BCR tl-expected 1.3.1, never an apt installation. Record matching and unmatched
upstream work separately in UPSTREAM_AWF.md.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=no-agnocast @autoware_tensorrt_yolox//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @autoware_tensorrt_yolox//... \
  //bazel/tests:yolox_detector_test //bazel/tests:yolox_node_test
```

All twenty-two initial package targets build in 24.168 seconds. With the complete
runtime declarations from the following integration entry, all fourteen original
cases pass: ten labels (0.2 seconds), three real GPU detector cases (38.8 seconds)
and one GPU/ROS-node case (22.1 seconds), zero failures/errors/skips. Both ONNX
models build actual FP16 engines; real-image detection, segmentation and ROS
outputs are checked. Source corrections remain separate from Bazel definitions.
Targeted clang-format/whitespace checks pass. Older ROS/CMake builds, INT8
calibration, other SDK versions and ARM are not tested.

## 2026-11-19: Phase 6.4.4 — Complete YOLOX GPU consumer

Purpose: expose the full local autoware_tensorrt_yolox package with all existing
behavior tests. Inspect its five C++ sources, one CUDA preprocessing source,
two component/executable contracts, original resources and three original test
files. Gate: all ten label cases, three real-model detector cases and the
original ROS-node case, with no GPU/model skips. Reuse the preceding local
utility, Tier 4 message, decompressor and model integrations. The separately
committed source correction is 1b2f9a66; it does not alter inference behavior.

Add local MODULE/BUILD/root override. Use registry image_transport 6.4.10-1.rcr.0
because no local implementation exists. Compile preprocessing with nvcc/C++17;
compile the full library and both original node sources with the existing
libc++ toolchain and pinned OpenMP support. Reuse the source-built LLVM libomp
header/archive pattern. Export the processing library, preprocessing shared
provider, both components, generated executables, original launch/config files
and test_image.jpg through the package-owned ament index. Preserve test mains:
all three original files supply main, so link gtest rather than gtest_main.

Use perception_utils' dedicated run_length_encoder_library/shared target,
not its full geometry/remapping closure. Keep shared providers explicit. The
ROS-node test exposed a runtime-only failure after successful linking: its
DebugPublisher could not load autoware_internal_debug_msgs' introspection
support because the generator-C shared library was absent. Declare the actual
debug-message module and :cc_shared provider in the component's collected
runtime closure. Merely embedding generated code statically does not satisfy
ROS's runtime typesupport loading contract.

The label suite initially aborted before executing because the linked
image_transport library initializes a plugin loader and needs its package
index. Put the component's own :ament_index in that test's data; it composes
the image_transport provider. The detector/node adapters already declare this
index. Neither failure requires changing an original test or deferring cases.

Keep added adapters under bazel/tests/autoware_tensorrt_yolox. The fixture runner
resolves the model's indexed share, copies exactly the two required ONNX files
and labels/color map into TEST_TMPDIR, supplies the original tests'
YOLOX_TEST_DATA_DIR override and an explicit runtime index prefix, then preserves
native exit/XML results. Engine files remain writable temporary outputs.
Reject skipped or empty native suites; two added negative adapter cases verify
that those zero-exit native results still fail the integration gate.

Extend the generic third_party/tensorrt test helper with optional run_under and
timeout inputs. Its existing SDK/GNU runtime resolution happens before invoking
the caller-owned fixture command, so the Autoware adapter need not duplicate SDK
paths or depend on host libraries. The helper has no Autoware labels. Native
TensorRT-common's full thirteen-case suite passes with the default path after
this change (22.2 seconds), and both YOLOX suites exercise the fixture path.
Use a long timeout for these model-building suites, matching their original
multi-minute allowance instead of treating engine construction as a unit test. The
remaining shared-launcher consumers are also rerun after the helper change:
all five classifier and both GNU-runtime plugin-interop cases pass in a
27.245-second invocation (22.1 and 1.4 seconds respectively), zero skips.
Reproduce with bazel test --config=no-agnocast --local_test_jobs=1
--nocache_test_results --test_output=errors //bazel/tests:tensorrt_classifier_test
//bazel/tests:tensorrt_plugins_test.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @autoware_tensorrt_yolox//... \
  //bazel/tests:yolox_detector_test //bazel/tests:yolox_node_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @autoware_tensorrt_common//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests:yolox_fixture_adapter_test
```

Results: all twenty-one package non-test targets build; all fourteen original
cases pass, zero failures/errors/skips/disabled. Final native target times are
0.2 seconds (labels), 38.8 (detector), 22.1 (ROS node); the combined invocation
also included a separately corrected launcher-adapter check and took 101.536
seconds. The detector verifies blank-image rejection, traffic-light detection
in the real original image, bounded/remapped ROIs and semantic/color masks.
The ROS test publishes real images through Cyclone DDS and verifies object and
mask outputs after actual FP16 GPU inference. The fixture rejection target
passes both added checks in 0.7 seconds. Targeted buildifier, clang-format,
Python syntax and whitespace checks pass.

No original test is edited or omitted. INT8 calibration accuracy, GUI/single-
image interactive behavior, performance, ARM/Thor and Fast DDS are not claimed.
The separate launcher boundary finishes indexed defaults/dependencies; full
perception launch composition and deployable runtime bundles remain later work.

## 2026-11-19: Phase 6.4.4 — Indexed YOLOX launcher defaults

Purpose: complete the model provider's actual launcher adoption alongside the
validated GPU consumer. Inspect every current autoware_launch reference to
$(var data_path)/tensorrt_yolox in active XML files. There are twelve model/label/
color-map paths across component_traffic_light, both camera/lidar/radar launch
variants and the two whole-image traffic-light entry points. Changelog prose
is historical and is not a runtime parameter. The files belong to
autoware_launch and autoware_perception_launch.

Replace those twelve prefixes with
$(find-pkg-share autoware_tensorrt_yolox_models), preserving original filenames
and argument override behavior. Declare the model package as exec_depend in
both launcher manifests. No workspace absolute path, HOME fallback, aggregate
model package or Ansible/reference-manifest edit is introduced. The remaining
data_path arguments still serve other, unadopted model families; this task
removes their use for YOLOX, not the outstanding global conversion.

Root BUILD exports only the five affected XML files and two manifests as a
standalone-test input group. The root-owned Python test parses those original
files, registers ROS frontend substitutions, evaluates their actual indexed
path expressions with LaunchContext, checks real model/metadata files and
verifies the runtime dependencies. Direct substitution parsing without loading
ROS substitutions reports Unknown substitution: find-pkg-share; a normal XML
parser loads those extensions, while this focused adapter imports them
explicitly. This checks resource resolution, not full perception launch startup.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests:yolox_launch_paths_test
```

Both launcher checks pass in 1.1 seconds (3.757-second invocation). All twelve
selected expressions resolve to existing files in the model's own indexed share;
both manifests declare that package. Earlier passing real-model detector/node
and fresh LFS/Bazel/colcon gates remain applicable: these edits change only
launcher defaults/dependencies and the focused test. Targeted XML/Python syntax,
buildifier, Markdown links and whitespace checks pass.

Phase 6.4 and Phase 6 are complete for the selected x86 scope: full plugins,
classifier and YOLOX, with actual GPU execution and complete module suites.
No new Fast DDS, ARM/JetPack, INT8 accuracy, performance or full perception/GUI
launch check was run. Other model families and the common data_path removal
remain later consumer work; the next numbered phase is Phase 7's native runtime
packaging and planning-simulator milestone. The x86 TensorRT/JetPack version
gap and the need for actual host GPU/kernel drivers remain documented limits.

## 2026-11-20: Phase 7.1 — Native graphics runtime provider

Purpose: supply real GL dispatch implementations for a native headless product
whose PCL/VTK/OpenCV closure links GL/EGL/GLX/OpenGL. Inspect the pinned registry
libglvnd BUILD and generated stubs before selecting a provider: version
1.7.0.rcr.0 exports link-time stubs with production SONAMEs, not usable runtime
implementations. Host process maps showed installed graphics/X11 libraries and
libbsd/libmd concealing this runtime edge. A static SONAME-presence audit alone
cannot distinguish a stub from an implementation.

Pattern: root-select a generic repository under third_party/graphics_runtime,
without adding an Autoware-dependent facade module. Record eleven Ubuntu noble
amd64 package versions, immutable payload paths and SHA-256 values in
archives.json, obtained from official metadata. Bazel natively extracts each
.deb and its nested data archive; no apt/dpkg resolver, maintainer script, host
installation or copied host library is used. The payloads supply GLVND 1.7.0
and its X11/XCB/BSD load closure. Keep copyright files as declared resources.
Constrain this provider to Linux x86_64. The
[provider README](../../third_party/graphics_runtime/README.md) owns input-source,
API-gate and unsupported-graphics details.

The reusable third_party/runtime helper maps declared library basenames and
explicit aliases into the runfiles lib prefix, rejecting conflicting mappings.
It is independent of Autoware labels. Root composition subsequently combines
this provider with its existing GNU and source-built zlib providers; do not add
those product choices to the dependency helper. Introduce the optional root
--config=runtime Bash/Python bootstrap selection for distributions, preserving
normal developer build configuration.

The complete binary-provider suite is its display-independent dispatch_test:
dlopen all five real dispatch DSOs and query EGL client extensions with no
display, vendor backend or GPU context. Reproduce its standalone gate:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //third_party/graphics_runtime:dispatch_test
```

Pass: 0.3 seconds (4.311-second invocation), no omitted cases. The stricter
[product relocation gate](#2026-11-20-phase-71--native-headless-core-distribution)
additionally proves actual application use of these files without host graphics
libraries. This binary-provider gate does not establish rendering, complete
upstream graphics conformance, a display/vendor backend, or ARM support. No
existing source package is edited; buildifier, clang-format, JSON and whitespace
checks pass. The provider task is complete for the selected headless load scope.

## 2026-11-20: Phase 7.1 — Wrapper standalone logging runtime

Purpose: preserve the generated standalone mains' logging dependency in deployed
package indexes. The core build-tree launch passed, while an extracted
interface_node resolved libglog.so from /lib with LD_LIBRARY_PATH set only to
the extracted lib prefix, also loading host gflags and libunwind.so.8. Inspect
autoware_agnocast_wrapper/defs.bzl: its generated mains explicitly link
@rosdistro//cc:glog, while its ament_package exported only its component library.
That main-only dependency therefore lacked an indexed runtime provider.

Add the existing @rosdistro//cc:glog shared provider to the wrapper's package-local
ament_package libraries list. No C++ implementation, original test, root-owned
file inventory or runtime fallback is needed. Transferable rule: include a
standalone main's helper-owned shared providers even when the component library
itself does not depend on them. The
[developer convention](DEVELOPING.md#native-core-distribution) records that rule;
the product's own strict runtime audit verifies the exported provider in use.

Complete wrapper gate for this metadata change: all existing C++ and Python
cases with ENABLE_AGNOCAST=0 and Cyclone DDS. The binary/algorithm implementation
is unchanged; no new enabled-backend or Fast DDS claim is made.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_agnocast_wrapper//:launch_tests \
  @autoware_agnocast_wrapper//:unit_tests
```

All 37 C++ and 66 Python cases pass, zero skips (0.4 and 1.6 seconds respectively;
27.316-second invocation). Targeted buildifier and
whitespace checks pass. The metadata correction is complete; the subsequent
native-product gate demonstrates that no host glog/gflags/libunwind.so.8 is
loaded. Existing enabled-backend implementation evidence is unchanged.

## 2026-11-20: Phase 7.1 — Native headless core distribution

Purpose: distribute the already validated original core launch without Bazel,
a checkout, build-cache symlinks, host ROS or host Python. Work began November 19;
the final gates passed November 20. The boundary is root product composition,
its native runtime providers and the existing core behavior suite. Scope is
Ubuntu 24.04 x86_64, CPU/Cyclone DDS, ENABLE_AGNOCAST=0, sample vehicle/sensor
indexes and declared small map fixtures. This is not closed-loop driving.

Inspected inputs: the core ament_index closure and original XML launch,
launch_ros_binary/launch_test entry points, rules_python 2.4.5 bootstraps,
rules_pkg 1.3.0's include_runfiles implementation, generated wrapper mains and
RCR libglvnd 1.7.0.rcr.0. The complete module gate consists of the original two
core cases, archive structure, the real graphics-dispatch check and the complete
affected wrapper suites (37 C++ and 66 Python cases, disabled backend). Repeat
the same core cases against a relocated application, both natively and inside
a minimal isolated filesystem, with library and process-lifecycle assertions.

Minimum integration pattern:

- Root bazel/runtime selects launch_ros_binary with the original exported core
  XML, its index and concrete sample vehicle/sensor indexes. The source package
  only gains a BUILD export; it acquires no metarepository dependency. Keep
  map_path caller-supplied. Package a separate verification companion from the
  existing launch_test, its peer dependencies and declared map fixtures.
- Use pkg_tar(include_runfiles=True, preserve_mode=True, stamp=0,
  allow_duplicates_with_different_content=False). It consumes DefaultInfo files,
  runfiles files/symlinks/root symlinks and repository mappings. Preserve the
  executable-adjacent runfiles layout and materialize its declared closure;
  do not add a second per-package install inventory or scan source directories.
- Build with --config=runtime: bootstrap_impl=script starts through Bash and
  reaches declared Python before importing application code. The ordinary
  /usr/bin/env python3 bootstrap otherwise requires host Python first. Initial
  generator reruns made this first configuration build take 3178.127 seconds;
  subsequent archive work reused the unchanged native build outputs.
- The bundle-relative run launcher establishes its own index/library/runfiles
  paths, clears inherited ROS/Python/library discovery variables, disables user
  Python packages and selects headless sample models, Cyclone DDS and no
  Agnocast. Supply writable state separately from the read-only application.
  A standalone Python binary's Bazel args are not embedded into its raw
  executable; the wrapper supplies the actual launch file and arguments.
- Root composition selects native_runtime. A generic third_party/runtime helper
  publishes declared library basenames/explicit SONAME aliases into lib/ and
  rejects conflicting mappings. It composes the existing pinned GNU 12.2
  runtimes, RCR's source-built zlib 1.3.2 (including libz.so.1) and real graphics
  dispatch. Keep reusable providers below src/, and product choices above it.

Diagnosed omissions and their fixes are recorded at the owning boundaries:
[real graphics dispatch](#2026-11-20-phase-71--native-graphics-runtime-provider)
and [standalone logging](#2026-11-20-phase-71--wrapper-standalone-logging-runtime).
A full process-map inspection also exposed Python's host GNU/zlib libraries;
root composition supplies the existing pinned GNU archives and source-built zlib
through its own lib prefix. Keep only loader/glibc in the runtime OS allowlist.
Audit the Python launcher itself as well as every native child. Check nested
startup/clean-exit pairs and surviving PIDs after shutdown; an outer launcher
returning zero alone cannot establish its children's exit status.

Build the two archives, then run the complete root suite sequentially from an
unsourced shell. The separate provider/wrapper entries give their full gates;
reuse those passing results for unchanged code and configuration.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=runtime --config=no-agnocast \
  //bazel/runtime:core_bundle //bazel/runtime:core_bundle_tests
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests:core_bundle_structure_test \
  //bazel/tests:core_system_headless_launch_test
```

Both root targets pass with the final parent-process audit (27.9 seconds
structure, 4.9 seconds core; 69.113-second invocation). Analysis-only build of
both archives and dispatch_test also accepts the final platform guard. No
original implementation or original test is modified.

Reproduce extraction and the final native gate as follows. Staging directory
names are arbitrary; the application has no build-time install prefix. The
map is assembled from companion-owned fixtures by the test itself.

```sh
runtime_dir=$(mktemp -d)
mkdir "$runtime_dir/app" "$runtime_dir/checks_final" "$runtime_dir/state" "$runtime_dir/results"
tar -xf bazel-bin/bazel/runtime/core_bundle.tar.gz -C "$runtime_dir/app"
tar -xf bazel-bin/bazel/runtime/core_bundle_tests.tar.gz -C "$runtime_dir/checks_final"
(
  cd /
  env -i HOME="$runtime_dir/state" PATH=/usr/bin:/bin \
    TEST_TMPDIR="$runtime_dir/state" XML_OUTPUT_FILE="$runtime_dir/state/native-final.xml" \
    "$runtime_dir/checks_final/runtime/verify" "$runtime_dir/app/run"
)
```

Both cases pass: nodes, wheelbase/use_sim_time parameters, nonempty latched map,
sensor-bearing robot description, velocity relay, version API, graph endpoints
and clean shutdown. The final native functional case takes 3.416 seconds. Archive
structure checks both distributions for duplicate/escaping paths, closed links,
executable Bash bootstraps, repository mappings and the core index. All indexed
resources come from the extracted application; the test peer has its separate
index. No inherited AMENT_PREFIX_PATH, PYTHONPATH, LD_LIBRARY_PATH or LD_PRELOAD
is used. DDS execution needs network-interface access, including loopback.

The isolated OS baseline is Ubuntu 24.04, glibc 2.39, image digest below. Docker
is an isolation harness for this native artifact, not a native runtime requirement
or the Phase 7.2 OCI product. No build workspace/cache, ROS installation, Python
installation or GPU/Agnocast device is mounted. Only the two extracted trees and
an initially empty results directory enter the container; /tmp is fresh writable
state. Run unprivileged, without network access, from / and with read-only roots:

```sh
docker run --rm --network=none --read-only --cap-drop=ALL \
  --security-opt=no-new-privileges --user "$(id -u):$(id -g)" \
  --tmpfs /tmp:rw,exec,nosuid,nodev \
  --mount "type=bind,src=$runtime_dir/app,dst=/validation/app,readonly" \
  --mount "type=bind,src=$runtime_dir/checks_final,dst=/validation/checks,readonly" \
  --mount "type=bind,src=$runtime_dir/results,dst=/results" \
  ubuntu@sha256:73b999393d07f2bf65a40373778a44dc9a6d54ad7718d29247d8ff9413c4d6a8 \
  /bin/sh -ec 'if command -v python3; then exit 1; fi; test ! -d /opt/ros; \
    mkdir /tmp/home /tmp/state; cd /; exec env -i HOME=/tmp/home LOGNAME=runtime \
    PATH=/usr/bin:/bin TEST_TMPDIR=/tmp/state XML_OUTPUT_FILE=/results/isolated-final.xml \
    /validation/checks/runtime/verify /validation/app/run'
```

The final isolated gate also passes both cases, zero failures/errors/skips.
Both native and isolated logs record matching startup/clean-exit reports for
all 27 processes including the outer launcher, with no surviving recorded PIDs.
The only loaded application libraries outside its tree are the Linux loader and
glibc's libc, libm, libdl, libpthread, librt and libutil; parent Python and all
native children use the declared runtime providers. This establishes Ubuntu
24.04 x86_64 support, not a whole-bundle glibc-2.34 compatibility claim.
Both the indexed interface_node and the bundled Python interpreter request
/lib64/ld-linux-x86-64.so.2. These static ELF checks and dependency inspection
complement the actual loaded-library audit:

```sh
readelf -l "$runtime_dir/app/core_headless.runfiles/_main/lib/autoware_default_adapi/interface_node"
readelf -l "$runtime_dir/app/core_headless.runfiles/_main/bazel/runtime/_core_headless.venv/bin/python3"
LD_LIBRARY_PATH="$runtime_dir/app/core_headless.runfiles/_main/lib" \
  ldd "$runtime_dir/app/core_headless.runfiles/_main/lib/autoware_default_adapi/interface_node"
```

ldd reports no unresolved dependency; libglog.so resolves inside the bundle.
Targeted buildifier, clang-format, shell/Python/JSON syntax, Markdown link and
whitespace checks pass. No pre-commit or Fast DDS run is performed.

Phase 7.1 is complete. Next: 7.2 packages this same native closure into a thin
OCI artifact. A vendor graphics backend/display server, RViz, GPU/Agnocast
execution, ARM, QEMU, OTA and a driving scenario are not established here.
Native distributions still contain duplicated runfiles paths; size optimization
must preserve resource/library identity and the existing behavior gate.

Additional runtime-environment verification on 2026-11-20: the same unchanged
archives pass both existing cases on native Docker Engine 29.10.0, with Desktop
stopped and its autostart disabled. Use the isolated command above with
docker --context default run --pull=never. Both functional and post-shutdown
cases pass, zero failures/errors/skips; the parent/child library audit again
allows only the loader and glibc outside the application. A small FROM scratch /
COPY marker build with the Engine's default Buildx driver also exports matching
file contents. Engine-installed Buildx 0.39.0 and Compose 5.8.0 are available.
No Autoware rebuild, additional middleware run or new integration phase is needed.

For an existing Desktop installation, switching context alone leaves separate
image stores and local client preferences. While the source daemon is running,
transfer the existing reference images without changing their digests:

```sh
set -o pipefail
docker --context desktop-linux image save ubuntu:24.04 ros:lyrical-ros-base | \
  sg docker -c 'docker --context default image load'
sg docker -c 'docker --context default version --format "{{.Server.Version}}"'
```

The Ubuntu digest used above and the ROS reference image digest are preserved.
Other Desktop images, stopped containers, volumes and builder cache remain in
their original store. Back up local client settings before replacing Desktop-
owned Buildx/Compose links with Engine-installed plugins. Clear the Desktop
credential-store preference only after verifying its store contains no
credentials; preserve registry-specific helpers. Check for active
builds before stopping/disabling the Desktop service. Native Engine and
containerd were already running and enabled at boot. The account's Docker group
membership was present but absent from the existing process groups; sg docker
resolved access without changing socket permissions or requiring sudo. A new
login session makes that membership available to ordinary CLI commands. These
host-local settings require no Bazel rule or source-package changes.

For 7.3, preserve the agreed sequence: reconcile the exact AWF tutorial and
integrate its selected Universe planning/launch closure before demonstrating
planning_simulator.launch.xml. A core-only simulator experiment remains plausible
from the inspected standalone simulator manifests but has no runtime proof and
does not satisfy that original workflow's milestone. See the
[phase plan](PLAN.md#phase-7--runtime-packaging-and-headless-planning-simulation-in-progress)
for source references and the separate acceptance boundary.

## 2026-11-20: Phase 7.2 — Thin OCI core distribution

Purpose: package the validated native core closure into a deployable OCI image
without introducing a second application dependency list or installing target
dependencies in a Dockerfile. The complete task gate comprises payload parity,
image structure/configuration, both existing headless behavior/shutdown cases,
and clean termination through the normal image entrypoint.

Prerequisites and inspected inputs: Phase 7.1's native application and separate
verification archives; Ubuntu 24.04 x86_64 at the same immutable image digest;
local Docker Engine access for execution only. Inspect rules_img 0.3.22's
pull, layer_from_tar, image_manifest, image_load and image_structure_test rules.
This version already participates in RCR's module graph; make its selection
explicit in the root rather than introduce another image assembly library.
Keep --config=runtime --config=no-agnocast throughout, preserving the cached
Bash/Python bootstrap and selecting CPU/Cyclone DDS without Agnocast.

Integration pattern: images/images.MODULE.bazel pulls the digest-pinned base
with eager layer materialization so exported archives contain every base blob.
In images/BUILD.bazel, pkg_tar consumes the native archive through deps and adds
only package_dir="opt/autoware". Feed that archive to layer_from_tar, then append
it to the base with image_manifest. Select /opt/autoware/run as entrypoint,
empty command, / as workdir, user 1000:1000, HOME=/tmp and SIGINT for clean ROS
launch shutdown. Export an OCI tarball and an eager, canonically tagged Docker
load archive from the same image provider. Image assembly needs no Docker
daemon, apt, pip or rosdep. The verification companion remains outside the
product; no component source or runtime dependency provider changes are needed.

The root tests compare every native payload file/link's
bytes, mode and target after prefixing, inspect image metadata and filesystem,
then load the built archive into the local engine. The runtime test executes
the existing two-case suite against the baked application with only the separate
verification companion and writable results mounted; a second run uses the
normal image entrypoint and only map inputs, then checks docker stop and every
child's clean-exit report. Both runs use read-only roots, an unprivileged image
user, fresh /tmp, no network, dropped capabilities and no workspace/cache mount.
The existing loaded-library audit remains active.

Verification, using Bazel 9.3.2 and native Docker Engine 29.10.0:

```sh
bazel build --config=runtime --config=no-agnocast \
  //images:core_oci //images:core_docker_archive
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests:core_image_structure_test \
  //bazel/tests:core_image_payload_test //bazel/tests:core_image_runtime_test
bazel run --config=runtime --config=no-agnocast //images:core_load
docker image inspect docker.io/library/autoware-core:bazel \
  --format 'Descriptor={{json .Descriptor}} RepoDigests={{json .RepoDigests}}'
docker info --format '{{json .DriverStatus}}'
```

Run Bazel from an unsourced shell; the recorded invocations use env -i with
HOME and PATH containing only the Bazel launcher and /usr/bin:/bin. If the
current login has stale Docker group membership, wrap these commands in
sg docker -c rather than alter socket permissions. Builds reuse the unchanged
native outputs; image export does not require C++ recompilation.

Results: all three image tests pass. Payload parity covers 23,665 native
files/links, preserving bytes, modes and targets. All 13 image structure and
configuration assertions pass. The runtime test passes both original headless
cases with zero failures/errors/skips: expected nodes, wheelbase/use_sim_time,
nonempty latched map, sensor-bearing URDF, velocity relay, version API, graph
endpoints and clean nested shutdown. The parent and all native children execute
from the baked /opt/autoware payload; the loaded-library audit permits only
the documented OS loader/glibc outside it. The functional-suite launch has 27
processes including its outer launcher. The separate default-entrypoint run
initializes the stop-line plugin, then docker stop honors SIGINT, returns exit
code 0 and produces matching clean-exit reports for all 26 child processes.
No child reports a crash or forced termination. The final runtime test takes
26.3 seconds, including image loading, companion extraction and both launches.

The OCI archive is 811,123,712 bytes, containing one Ubuntu layer and one native
application layer. Verify the standalone export without a daemon or registry
by checking its index, manifest, config and every layer blob:

```sh
python3 - <<'PY'
import hashlib
import json
import tarfile
from pathlib import Path
with tarfile.open('bazel-bin/images/core_oci_layout.tar') as archive:
    assert json.load(archive.extractfile('oci-layout'))['imageLayoutVersion'] == '1.0.0'
    index = json.load(archive.extractfile('index.json'))
    assert len(index['manifests']) == 1
    def verify(descriptor):
        algorithm, digest = descriptor['digest'].split(':')
        member = archive.getmember('blobs/' + algorithm + '/' + digest)
        assert member.size == descriptor['size']
        assert hashlib.file_digest(archive.extractfile(member), algorithm).hexdigest() == digest
        return member
    manifest = json.load(archive.extractfile(verify(index['manifests'][0])))
    verify(manifest['config'])
    for layer in manifest['layers']:
        verify(layer)
    assert len(manifest['layers']) == 2
    assert index['manifests'][0]['digest'] == Path('bazel-bin/images/core_digest').read_text().strip()
    print(index['manifests'][0]['digest'])
PY
```

All blob sizes and SHA-256 digests pass. The exported manifest digest is
sha256:6217f630e7abc7096327f01976124c1df129c1176b6a3b7d868f28e512cb0117.
The normal core_load target also succeeds. Its direct containerd-socket probe
is denied for the unprivileged account and it falls back to docker load. The
loader's generic warning predicts a changed digest, but actual inspection on
this Engine's io.containerd.snapshotter.v1 store confirms the loaded OCI
descriptor and RepoDigests match the exported manifest. No socket permission
change or root invocation is needed; check actual descriptors when using a
different engine/storage backend. The local convenience tag is not an
immutable publication mechanism.

Test setup findings: an empty OCI command may be omitted from Docker's inspected
Config rather than appear as Cmd: []; treat a missing/empty value alike, while
still asserting the image entrypoint. The mtree structure matcher checks
canonical archive paths and does not follow Ubuntu's /bin directory symlink;
assert /usr/bin/bash there. Without these corrections the test harness fails
before exercising runtime behavior, although the image itself is valid. Capture
both Docker log streams when checking initialization and shutdown. These fixes
require no product/source patch.

Targeted buildifier, Python syntax/flake8, YAML, documentation links and
whitespace checks pass. No pre-commit or Fast DDS run is performed. Unchanged
Phase 7.1 native/module suites retain their previous evidence.

The single-entrypoint variant, now Phase 7.2.1, is complete for x86_64
CPU/Cyclone DDS with Agnocast disabled.
The image does not establish GUI, GPU, enabled Agnocast, ARM, QEMU, OTA or a
driving scenario. QEMU remains an optional additional packaging check.

Additional completion scope agreed on 2026-11-20: Phase 7.2 remains open for
s6 and systemd OCI service demonstrations and the OSTree task below.
Preserve the existing core artifact and give each manager a
small dummy prerequisite daemon with explicit readiness. Add product definitions
under images/, pinned generic dependency support under third_party/ and
root-owned tests under bazel/tests/. Inspect each manager's actual runtime
requirements before choosing versions or asserting container settings work.
No image-side apt/pip/rosdep dependency resolution is planned.

To-dos: test delayed readiness, the existing functional core behavior against
the managed instance, dependency-loss policy and clean ordered shutdown. Keep
the core source unchanged and avoid substituting an independently launched core
for the managed process under test. Record each variant's exact inputs,
commands, diagnosed failures and complete results when implemented. The
[subphase plan](PLAN.md#phase-7--runtime-packaging-and-headless-planning-simulation-in-progress)
defines the full acceptance boundary. This addition is planning only: no s6 or
systemd implementation, build or behavior check has been performed.

Deployment decision: retain native artifacts plus optional single-host OCI
packaging and ROS launch composition. Multi-container, multi-host profiles and
mixed-backend generation are not being pursued; see the authoritative
[scope decision](PLAN.md#deployment-scope-decision). The supervisor variants
are demonstrations within one image/host, not a general orchestration layer.

Additional plan requested on 2026-11-20: add Phase 7.2.4 for OSTree native
distribution of the same core and systemd/dummy composition. Before
implementation, explicitly choose application-tree delivery versus bootable OS
deployment; the latter also requires pinned OS/kernel/boot inputs and actual
boot/update/rollback checks in a disposable guest. The upstream
[overview](https://ostreedev.github.io/ostree/introduction/) and
[deployment contract](https://ostreedev.github.io/ostree/deployment/) distinguish
the repository/checkout mechanism from full OS deployments. The plan requires
two immutable revisions, integrity/provenance checks, install/update/rollback,
the complete core and prerequisite-service behavior after each transition,
preserved compatible state and a usable previous revision after interrupted
preparation. No developer-host OS conversion or production OTA/fleet machinery
is authorized by this experiment. Keep the scope and gates in PLAN.md rather
than repeat a prospective developer workflow in DEVELOPING.md.

The s6, systemd and OSTree additions are planning-only documentation changes;
reuse the passing 7.2.1 runtime evidence above. Documentation consistency,
local links/anchors and whitespace checks pass; no runtime rerun is needed for
these plans. The validated OCI boundary is committed separately as 3ec963e7;
publish the remaining-variant plan and pause before implementation. After all
four 7.2 variants pass, proceed to 7.3's AWF tutorial and actual selected Universe
planning/launch closure.

## 2026-11-21: Prioritize Universe coverage before vehicle redeployment

Purpose: keep upcoming work focused on Autoware adoption after defining the
ownership of software, domain resources, per-vehicle records and runtime state.
The current distribution variants remain required; after them, the original
planning-simulator workflow drives its actual Universe closure, followed by
broader related-package integration. Keep existing phase identifiers.

Decision: defer vehicle-specific provisioning and model/map/domain switching
until substantial Universe coverage is available. No new Ansible roles, fleet
service or mixed-deployment abstraction is scheduled. A later domain deployment
experiment should use identified software/model/map/parameter revisions and
synthetic vehicle records, prove resource selection during A → B → A, reject
incompatible selections, and preserve identity/calibrations through recovery.
Phase 7.2.4 only gains an explicit distinction in its existing state-preservation
fixture; a full domain-switching experiment remains outside its gate. bootc is
an OS-deployment candidate whose actual base compatibility and boot behavior
would need validation if selected.

Validation: documentation consistency, local links and whitespace only; no
runtime behavior changes and no build/test rerun is required for this decision.
Next: resume Phase 7.2.2's complete s6/core-plus-prerequisite image gate, then
systemd and OSTree, before beginning 7.3.

## 2026-11-21: Phase 7.2.2 — s6 core service supervision

Purpose: demonstrate a full core application and a prerequisite daemon in one
OCI image, with actual readiness ordering, dependency-loss handling, restart
recovery and ordered shutdown. The complete module gate includes the existing
core behavior/shutdown cases against the supervisor-started instance, image
structure, delayed readiness and pre/post-readiness failure injection. Routine
middleware remains Cyclone DDS, Agnocast disabled.

Inspected inputs: s6-overlay 3.2.3.2's immutable noarch and static x86_64 release
archives, s6-rc service definitions, readiness descriptors, finish exit status
125 and the read-only-root/non-root execution contract. SHA-256 pins and
provenance belong to third_party/s6/. The product owns the service definitions
in images/s6/, the original core payload and a tiny root-owned demonstration
daemon. The daemon announces readiness through the manager's notification
channel and remains alive. The core waits for that notification; startup order
alone is insufficient. Unexpected prerequisite failure terminates the product,
with nonzero status and explicit restart recovery.

Integration pattern: extend the existing image with the upstream runtime layer,
the demo binary's executable-adjacent runfiles and product configuration.
Preserve its complete declared runtime closure: the LLVM/rules_cc runtime
toolchain adds shared libc++/ABI/unwind dependencies even for this C-only
binary. Copying only the ELF leaves unresolved libraries. Ordinary trailing
--as-needed flags and disabling static_link_cpp_runtimes do not remove the
separately injected runtime providers. image_layer(include_runfiles=True)
preserves their existing relative RPATH layout instead of listing libraries by
hand. Keep the installed executable name consistent in service commands.

The root test's lifecycle adapter attaches to the existing managed core and
requests its manager to stop the dependency group on test shutdown. An explicit
service PID file makes the existing executable/library audit inspect the core
and its descendants instead of the adapter. The verification companion stays
outside the application image. No component source is modified and no second
core graph is launched to stand in for the managed process.

Validation commands (run sequentially, unsourced shell, native Docker Engine
29.10.0 and Bazel 9.3.2):

```sh
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests:core_s6_structure_test //bazel/tests:core_s6_runtime_test
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests:core_image_runtime_test //bazel/tests:core_bundle_structure_test \
  //bazel/tests:core_system_headless_launch_test
```

Both s6 targets pass: nine structure assertions and the full managed runtime
gate. Both original core cases pass against the supervised process, including
the parent/child library/executable audit, expected nodes, parameters, map/URDF,
velocity relay, version API and graph endpoints. All 26 inner processes report
clean exits and none of the recorded application PIDs survive group shutdown.
The gate proves no early core startup while readiness is withheld, crash and
clean unexpected prerequisite loss both terminating the product with nonzero
status, explicit container restart recovery, clean reverse shutdown and failure
before readiness never starting the core. The final s6 runtime gate takes 60.8
seconds, including archive loading/extraction and the repeated lifecycle cases.
The three affected native/plain-image targets also pass without failures/skips;
unchanged image payload-parity evidence is reused. Builds reuse the native core
closure; no original source package is modified.

Runtime contract: --read-only --network=none --cap-drop=ALL
--security-opt=no-new-privileges, writable /tmp, and writable executable /run
owned by UID/GID 1000 (mode 0755). Test-only mounts provide map inputs, the
separate verification companion and observable /run/autoware state. Manual
execution only needs the map mount; the image defaults to /maps. The supervisor
and services all run as 1000:1000; no cgroup/device/privileged-container access is
required. S6_READ_ONLY_ROOT=1 directs mutable supervisor state to /run.

Consequential failure-policy finding: service dependencies govern transitions,
not continuous health. A finish script that only examines exit status misses
unexpected clean daemon exits. Inspect s6-svstat's wantedup state as well as the
exit status; unexpected exit while still wanted up halts the product, while a
requested reverse shutdown stays successful. Finish exit 125 prevents automatic
restart from hiding a failed prerequisite. The gate injects both SIGKILL and
caught SIGTERM, and verifies normal shutdown separately. The lifecycle adapter
stops the prerequisite through s6-rc so its reverse dependencies stop first.

Targeted buildifier, clang-format, Python/flake8, shell syntax, YAML, local
documentation links and whitespace checks pass. No pre-commit, Fast DDS or GPU
check is run. Phase 7.2.2 is complete for the existing x86_64 CPU/Cyclone product,
with Agnocast disabled. This does not establish native systemd or OSTree support.
Next: Phase 7.2.3's pinned manager closure and equivalent systemd image gate.

## 2026-11-21: Phase 7.2.3 — systemd core service supervision

Purpose: implement the same prerequisite/core readiness, loss, restart and
shutdown contract using native systemd units in a separate OCI image. The
complete existing headless core behavior and lifecycle suite is retained, with
Cyclone DDS and Agnocast disabled. No original source package is modified.

Inputs/prerequisites: Ubuntu noble/noble-updates package metadata, inspected
2026-11-21, selects systemd 255.4-1ubuntu8.19, matching shared libraries and the
explicit 32-payload closure in third_party/systemd/archives.json. Preserve each
package version, path and SHA-256; the pinned Ubuntu base supplies glibc. The
main archive returned HTTP 403 for selected payloads; kernel.org and Ubuntu's
security mirror supplied matching hashes. Package-installation tooling such as
debconf and systemd-dev is excluded. No package manager/maintainer scripts run.
Bazel extraction preserves original data archives; declared Python 3.14
compression.zstd conversion produces tar inputs accepted by rules_pkg, keeping
links, modes and copyright files. This avoids a host dpkg/zstd build dependency.

Minimum pattern: compose the generic SDK layer with unchanged native core,
the demo executable's full runfiles, and product-owned units under images/.
Type=notify delays prerequisite activation until READY=1. Core uses
Requires/BindsTo/After on the prerequisite; PartOf connects both services to
autoware.target. Use RequiresMountsFor on service runtime/input mounts so
shutdown does not unmount them before application termination. PID 1 runs as
root; services run as 1000:1000. ExecStopPost records shutdown, while the
instanced OnFailure handler records failure and requests manager exit status 1.

Failure policy and diagnosed issues: OnFailure alone misses unexpected clean
exits. ExecStopPost inspection of transient job/target state is unreliable during
dependency propagation. Instead, Restart=always with StartLimitIntervalSec=infinity
and StartLimitBurst=1 permits initial startup but rejects replacement starts,
triggering OnFailure. The test proves only one daemon start actually occurs.
Use Wants on the product target: Requires propagated target teardown back into
the prerequisite and cancelled its pending restart/failure policy after a clean
exit. The core's strict requirement stays intact. Normal explicit stop does not
restart; explicit container restart supplies a fresh manager and start budget.
The launch-test adapter must wait for all three systemctl stop jobs, because a
target's own stop can complete before member processes. Otherwise log inspection
races application shutdown even though runtime ordering is correct.

Supported runtime, experimentally established on native Docker Engine 29.10.0,
cgroup v2/systemd driver: --read-only --network=none --cgroupns=private --tty,
--cap-add=SYS_ADMIN --security-opt=apparmor=unconfined,
--security-opt=no-new-privileges, /tmp:rw,exec,nosuid,nodev and /run:rw,exec.
The entrypoint verifies 0::/ before bind-remounting its private cgroup view rw.
Default Docker cannot remount it; SYS_ADMIN alone still fails under AppArmor.
The container-local profile override is required for this invocation. No writable
host-cgroup bind mount, privileged mode, device or host manager installation is
used. Operator acceptance of these broader permissions is a runtime prerequisite.
Maps mount read-only at /maps; tests additionally mount a separate verification
companion and observable state. The stop signal is SIGRTMIN+3. See
[developer invocation](DEVELOPING.md#systemd-core-image).

Exact gate (run in a clean unsourced environment with local Docker access):

```sh
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests:core_systemd_structure_test //bazel/tests:core_systemd_runtime_test
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests:core_s6_structure_test //bazel/tests:core_s6_runtime_test
```

The systemd gate passes: ten image structure assertions and the complete runtime
gate (42.3 seconds). Both original core cases check actual managed processes,
including executable/library closure, nodes, parameters, map/URDF, relay, API,
graph endpoints and shutdown. All 26 inner processes finish cleanly with no
surviving recorded application PIDs. Delayed readiness prevents early core
startup. SIGKILL and caught SIGTERM prerequisite loss both stop the product with
nonzero status and no independent daemon restart; explicit container restart
recovers, and normal reverse shutdown exits zero. Failure before readiness does
not start the core. The gate also verifies systemd is PID 1 and core has its own
service cgroup. Both affected s6 targets also pass, including the strengthened no-restart
assertion; its full runtime gate takes 60.7 seconds.
Previously passing plain/native core gates and payload parity are reused because
their implementation/configuration is unchanged.

Targeted buildifier, Python/flake8, shell syntax, YAML and whitespace checks pass.
No pre-commit, Fast DDS or GPU check is run. Scope is Linux x86_64 container
supervision on the supported base, not a host OS or kernel/bootloader update.
Next: application-only OSTree distribution on a fixed OS.

## 2026-11-21: Phase 7.2.4 — OSTree SDK

Purpose: provide pinned local OSTree tools without installing host packages,
using Ubuntu noble inputs compatible with the established runtime baseline.
Select OSTree 2024.5-1build2 and 73 ELF/runtime payloads, including libc/loader,
by version/path/SHA-256 in third_party/ostree/archives.json. The source indexes
are noble/noble-updates main and universe, inspected 2026-11-21; the selected
Ubuntu mirrors and manifest hashes establish the same input contract as systemd.

Minimum pattern: move Debian payload handling from the systemd SDK into
third_party/deb_payloads only when this second consumer requires it. Owners
retain their separate manifests and root-selected versions. The shared rule
preserves original data archives, emits declared Python 3.14 Zstandard conversion
and rules_pkg assembly, and executes no apt/dpkg/maintainer scripts. No generic
facade module or Autoware dependency is introduced. Both SDK runtime archives
build; the relocated systemd provider passes its complete consumer gate in the
following application-distribution experiment.

The OSTree Python helper extracts its declared SDK, runs its own ELF loader and
library directory in a clean environment, and selects local GIO behavior. Before
execution, a loader audit resolves symlinks and rejects DSOs outside the SDK.
The public //third_party/ostree:ostree CLI forwards command arguments/output and
status, using an ephemeral SDK extraction. This avoids a host OSTree/zstd
installation. Only the exercised local command scope is established; advertised
optional features, remote transport, signing/key management and the absent gnupg
command suite are not validated runtime capabilities.

Verification: in a clean unsourced shell with Bazel and ordinary shell/file
utilities, first run the CLI with --version; it reports libostree 2024.5. Then
exercise the declared tool independently of Autoware using this small fixture:

```sh
ostree=(bazel run --config=runtime --config=no-agnocast //third_party/ostree:ostree --)
task_tmp=$(mktemp -d)
mkdir "$task_tmp/fixture"
printf 'SDK fixture\n' > "$task_tmp/fixture/payload"
chmod 666 "$task_tmp/fixture/payload"
ln -s payload "$task_tmp/fixture/link"
"${ostree[@]}" --repo="$task_tmp/source" init --mode=archive
revision=$("${ostree[@]}" --repo="$task_tmp/source" commit --branch=fixture \
  --no-xattrs --canonical-permissions --owner-uid=0 --owner-gid=0 \
  --timestamp=2026-11-21T00:00:00Z --add-metadata-string=fixture=verified \
  "$task_tmp/fixture")
test "$revision" = "$("${ostree[@]}" --repo="$task_tmp/source" rev-parse fixture)"
"${ostree[@]}" --repo="$task_tmp/source" show --print-metadata-key=fixture "$revision"
"${ostree[@]}" --repo="$task_tmp/source" fsck
"${ostree[@]}" --repo="$task_tmp/client" init --mode=archive
"${ostree[@]}" --repo="$task_tmp/client" pull-local --untrusted "$task_tmp/source" fixture
"${ostree[@]}" --repo="$task_tmp/client" fsck
"${ostree[@]}" --repo="$task_tmp/client" checkout --user-mode --force-copy \
  "$revision" "$task_tmp/checkout"
cmp "$task_tmp/fixture/payload" "$task_tmp/checkout/payload"
test "$(readlink "$task_tmp/checkout/link")" = payload
```

All commands succeed: metadata is 'verified', both repositories pass fsck for
one commit/five objects, and imported bytes/link targets match. The observed
fixture revision is
488a6bee20035f88be56898ee6587ad1d20f7808807fc0700c56e971c8699b9b.
Targeted buildifier, Python/flake8, links and whitespace checks pass. This closes
the dependency-tool boundary; it does not by itself establish Autoware delivery.
Next: the native application repository and complete managed core transition
gate below. No source package, host installation or reference Ansible role is
modified; no pre-commit or middleware/GPU check is needed for this SDK fixture.

## 2026-11-21: Phase 7.2.4 — OSTree application-tree distribution

Purpose: demonstrate immutable native application delivery/update/rollback while
keeping the supported OS, systemd units and demo prerequisite fixed. Whole-OS,
kernel/bootloader updates, bootc and vehicle provisioning remain deferred. The
OCI systemd variant is the disposable fixed-OS harness; the delivery artifact
is a native application OSTree repository. Broader Universe integration is next.
The deferred OTA section in PLAN.md records tool/state ownership for later:
Bazel builds identified artifacts, an OS pipeline owns its base, optional
Ansible provisions machine-specific inputs, local supervisors own services,
and an update agent would own authenticated activation/recovery decisions.
The existing ansible/ dependency-installer reference is not extended.

Inputs/prerequisites: use the [validated SDK](#2026-11-21-phase-724--ostree-sdk),
the existing native core_bundle, and the supported systemd/Docker runtime
contract. Version selection, the 73-payload manifest, declared conversion,
loader audit and local command scope are documented in the dependency gate
above. Components remain independent of product assembly and SDK definitions.

Minimum integration pattern: a root py_binary/genrule imports core_bundle into
an archive repository through --tree=tar, applies --no-xattrs, owner UID/GID 0,
--canonical-permissions, and a fixed commit timestamp. Add release.json with
release/domain metadata as an overlay. The second commit reuses the first tree
and changes only that metadata. The two revisions deliberately have identical
application binaries. Emit the repository and releases.json in core_ostree.tar.gz,
plus a separate identical core_ostree.json receipt. Manifest/commit metadata bind
the trees to the native bundle SHA-256; the receipt also pins the SDK archive
hash. Normalize archive ownership/time headers; preserve file/link payloads.
No Git/workspace/cache paths enter the artifact. The builder runs fsck before
publishing its outputs. The resulting archive is approximately 258 MiB; this is
an observed artifact size, not a performance acceptance gate.

Consequential import findings: 45 Lark wheel files in the native tar have mode
0666; a bare-user-only client rejects them. Canonical commit permissions remove
unsupported writable bits while preserving executable status. A subsequent
bare-user-only fsck still fails for the three logical 0555 symlinks in the
native tar, because that mode stores physical symlinks without their original
metadata. Inspection of failing object d8e663ac… confirms its logical 0120555
mode and Python bootstrap symlink target in the archive header. Use an archive-mode local client instead. It retains logical metadata
without requiring filesystem xattrs; force-copy, user-mode checkout produces
a separate application tree. Keep both fsck and --untrusted import verification;
do not suppress the checksum failure or rewrite source package contents.

Activation/state pattern: create complete deployments/<commit> trees, keep
vehicle records outside them, and use an atomically replaced relative active
symlink (with directory fsync) to select a prepared tree. Stop the whole product
before switching, then start a fresh manager/product instance so late resource
loads cannot span revisions. Rollback selects a retained compatible tree and
uses the same stop/start procedure. The test kills a real force-copy checkout
after observing files, verifies SIGKILL termination and unchanged active A,
then demonstrates A remains usable through its full gate. This proves interrupted
preparation behavior, not arbitrary power-loss or kernel/bootloader recovery.
The fixture preserves synthetic identity.json and calibration.yaml outside
revision content, exposing them read-only to the test runtime; it does not add
calibration-consuming application behavior. Only synthetic release/domain
metadata changes. Real model/map/domain switching and state migrations remain
deferred; current compatibility means identical binaries and state schemas.

Exact verification, in a clean unsourced environment with the documented local
Docker/cgroup access and user.bazelrc limits, sequentially:

```sh
bazel build --config=runtime --config=no-agnocast \
  //bazel/runtime/ostree:core_repository
bazel run --config=runtime --config=no-agnocast //third_party/ostree:ostree -- --version
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests:core_ostree_runtime_test
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests:core_s6_structure_test //bazel/tests:core_s6_runtime_test \
  //bazel/tests:core_systemd_structure_test
```

The CLI reports libostree 2024.5. The complete OSTree gate passes in 180.7
seconds: repository integrity/provenance, intentional object corruption rejected,
restored source verified, untrusted local import and local fsck, exact comparison
of all 23,665 native files/links (bytes, link targets and executable status),
interrupted preparation, and A→B→A activation. Each selected tree mounts read-only
over /opt/autoware in the systemd harness; its embedded original application is
hidden. Each selection passes the entire supervisor gate: both original core
functional/lifecycle cases, executable/library audits, delayed readiness,
SIGKILL and clean SIGTERM prerequisite loss, no independent daemon restart,
explicit product recovery, startup failure, reverse shutdown and all 26 inner
processes terminating cleanly. Six original core cases pass across three
selections, with no failures or skips. Synthetic vehicle records remain identical
throughout; active release/domain metadata matches each selected revision.

Observed revisions: A is
3f083ff81ed9c1bc5fea73f3884ee4567f5637b6953624df14293b42f6b0d6eb,
B is e6843e8708364d9681df4f53392e8641d0281657ed4beb6b3eb15972f717cd3c.
Input bundle SHA-256 is
4be16386f59a90da9e9be8de92b398954f867af1a036ee6400c35e2d998a8844;
SDK archive SHA-256 is
767a0a8fa6b17774108f571611faa06e706ab8ad21c68f6e7f641abcd36336ef.
Other integrations should compare their own receipt against their declared
inputs, not expect these commits after changing application content.

All three affected supervisor regressions pass: s6 runtime (58.8 seconds),
s6 image structure and systemd image structure. The unchanged native/plain-image
implementation and prior payload-parity evidence are reused. Targeted buildifier,
Python/flake8, documentation links/anchors and whitespace checks pass.
No original source package, host OS/service installation or reference Ansible
role is modified. No pre-commit, Fast DDS, GPU or QEMU check is run; this scope
is application distribution on Linux x86_64, not bootable OS/production OTA.
Phase 7.2 is complete. Next is 7.3's selected AWF planning-simulator/Universe
closure.

## 2026-11-21 — Scope an opt-in RMW reproduction milestone

Added a planned Bazel reproduction of the separate Lyrical colcon CPU crop
experiment. In six laptop runs per case, scoped publisher loans achieved
1.19× native median and 1.09× native p95 pooled. Median ratios by run order
were 1.30× and 1.17×. All 1,080 timed-run frames passed and a separate cold
Cyclone observer received 60 diagnostics. Use the pinned source, exact colcon
commands and raw results linked in the plan to establish the reference on a
provisioned worker before comparing Bazel-built binaries. This commit updates
planning documents only. Verified the reference commit, document anchor and
whitespace. All runtime evidence here comes from colcon.

## Clarify the scoped-loan performance milestone

The 1.20× native latency ratio guides engineering work. The same colcon
implementation produced median ratios of 1.30× and 1.17× in different orders,
so a few microseconds of headroom in the pooled result cannot serve as a
bridge overhead budget. Use paired, rotated measurements and evaluate drops,
copies and resource use alongside latency. Correctness checks remain strict.
The pilot preference is a single supervised bridge with explicit routes at
startup. A native-only deployment can supply an empty route list. Bridge
qualification and the native QoS investigation continue on the colcon branch.
Documentation-only update, validated with git diff --check.

## 2026-11-21: Phase 7.3.1 — Embedded model interpreter lifetime

Purpose: correct a demonstrated teardown crash in the required local
learning-based vehicle-model dependency. The new root embedding fixture first
checks real Python model calls, then destroys InterconnectedModel normally.
The model itself owns py::scoped_interpreter; a second caller-owned interpreter
is invalid and is not part of the final fixture.

Failure: on Ubuntu 24.04, LLVM/libc++ 21.1.8 and declared Python 3.14.4, the model
executes but exits with SIGSEGV (the run-under wrapper reports exit 245). GDB
reaches _Py_Dealloc → pybind11::handle::dec_ref → SimplePyModel::~SimplePyModel
→ InterconnectedModel::~InterconnectedModel. Its guard is declared after the
submodels vector, so reverse member destruction finalizes Python before releasing
Python-owned objects. This source lifetime defect is independent of Bazel.

Preserve move construction explicitly when adding the destructor: a user-declared
destructor suppresses the original implicit move constructor. The extended
fixture rejects the missing move at compile time, then moves a live model,
updates it and verifies normal teardown. Defaulting that constructor restores
the existing ownership transfer; both cases pass, including the moved object.

Fix: explicitly clear submodels in the destructor body while the interpreter
is alive. Keep member layout, public API and model computations unchanged;
do not suppress teardown, leak submodels or skip the failing regression.
Upstream main 087ff2c2867121a4807bdf7360a964aef40004f2 has the same lifetime
ordering. The inspected issue/PR list and clang-tidy PR #25 supply no matching
fix; the Lyrical tracker is broader related work. UPSTREAM_AWF.md records the
review scope and proposed correction; no submission is made.

Verification with the package-local Bazel declarations and root fixture prepared
for the following integration boundary:

```sh
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors --run_under=//bazel/tests:with_python \
  @autoware_learning_based_vehicle_model//... \
  //bazel/tests/autoware_learning_based_vehicle_model:embedding_test
```

The complete four-source library builds, and both mapping/embedding cases pass,
including repeated updates, external reset and normal interpreter teardown.
The pre-fix fixture fails; the same fixture passes after the destructor change.
Use the existing run-under tool for the declared Python home/import paths.
For native debugging, preserve those values for the inferior; run host GDB
with its own libraries/Python settings, rather than forcing the pinned GNU
wheel runtime onto GDB's libsource-highlight dependency. This changes no target
runtime dependency selection. Targeted clang-format and whitespace checks pass.
No older Python/ROS release, upstream CMake, GPU or middleware matrix is tested.
The other simulator modules and full lane-driving milestone remain incomplete.

## 2026-11-21: Phase 7.3.1 — Learning-based vehicle model

Purpose: supply the local simulator's required C++/Python connector without
host python3-dev/pybind11 packages. Inspect the standalone simulator repository's
package.xml/CMake/source and use its local snapshot; this library has no existing
behavior suite. Preserve all four C++ sources and the upstream hidden-visibility
compile setting. Expose its public headers with package-local MODULE/BUILD
rules and root local_path_override. Reuse the selected RCR pybind11 and declared
rules_python Python 3.14 headers/libraries, as the established pyplot provider
does; no empty pybind11_vendor/CMake shim is needed.

The root-owned acceptance fixture checks textual signal-name mapping/unconnected
signals and actual embedded-Python model loading, parameter/reset/dt dispatch,
state/action conversion, repeated updates and external state reset. InterconnectedModel
owns interpreter lifetime; the fixture must not create a second scoped interpreter.
No real learned vehicle/model weights are selected
by this fixture. Define this gate before introducing any compatibility fixes.

Validation: the complete four-source module builds and both root cases pass
with normal teardown, zero failures/skips, using the command in the immediately
preceding source-fix entry. //bazel/tests:with_python supplies the declared
interpreter home/import paths; it is a root test adapter, not a dependency of
this component. The experiment exposed interpreter/member lifetime ordering;
source correction b0be9d2d is documented separately above and in UPSTREAM_AWF.md.
The fixture follows the model-owned interpreter contract and never starts a
second interpreter. Targeted buildifier, clang-format and whitespace checks
pass. The library task is complete; the three simulation nodes and original AWF
driving milestone remain incomplete. No pre-commit or Fast DDS run is performed.

## 2026-11-21: Phase 7.3.1 — Simple planning simulator

Purpose: expose the local standalone vehicle simulator required by the original
AWF planning-simulation entrypoint. Inspect its package.xml/CMake, fourteen model/
core/utility sources, Python launch files, parameter/CSV resources and original
parameterized GTest before implementation. The acceptance scope is all ten
existing Ackermann/actuation model cases plus original-launch resource/motion
and clean-shutdown checks; learned connector lifetime has its separate gate.

Integration: package-local MODULE/BUILD declarations use supported local
Autoware utilities/messages, selected Lanelet2 providers and the local learning
library. Export the original component/plugin and generated standalone main;
register launch/param/data and original test CSV files in its own ament index.
The original Python launch also requires the vehicle-info provider's indexed
parameter file, so declare that index transitively. Root launch tests supply
initial_engage_state and motion_publish_mode=full_motion exactly as the parent
simulator launch does. Their defaults are inherited launch configurations, not
new parameters to insert into source files. The root peer sends initial pose,
gear and control commands, checks declared vehicle/model parameters, forward
motion and braking, then requires clean process exit. This is a simulator gate,
not a replacement for the original Universe planning stack.

The primitive launch fixture uses the already-adopted core sample vehicle
index as a caller-owned vehicle_info_param_file and vehicle_model_pkg. The
original parent supplies the latter; allow_substs also expands its occurrence
inside a YAML comment. Without that inherited configuration, launch fails
before starting a node. Supplying the actual indexed resource fixes the fixture
without editing source launch files. The full AWF sample_vehicle provider is
separate and enters the original-launch resource task in 7.3.3.

The three related packages build together with:

```sh
bazel build --config=runtime --config=no-agnocast \
  @autoware_simple_planning_simulator//... \
  @autoware_dummy_perception_publisher//... @autoware_vehicle_door_simulator//...
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_simple_planning_simulator//... \
  //bazel/tests/autoware_simple_planning_simulator:launch_test
```

The complete ten-case original suite passes in 71.5 seconds, covering forward,
reverse and steering behavior across its declared Ackermann/actuation presets.
The unchanged original fixture is neither filtered nor edited. Both root
launch cases pass in 4.2 seconds: wheel_base=2.79, DELAY_STEER_ACC_GEARED,
map/base_link odometry, displacement greater than 1 m, braking below 0.05 m/s
and clean node exit. The final complete module gate runs after the learning
library's lifetime/move correction, source commits b0be9d2d and 8e0efa8d; no
other simulator implementation change is needed. The library's independent
mapping/embedding/move/teardown cases also pass. Buildifier, Python/flake8 and
whitespace checks pass; Cyclone DDS/ENABLE_AGNOCAST=0 only. The module task is
complete; original Universe planning and full AWF lane driving remain later
7.3 tasks. No Fast DDS, GPU, GUI or pre-commit check is run.

## 2026-11-21: Phase 7.3.1 — Dummy perception publisher

Purpose: support the local simulated-perception producer used by planning
simulation. Inspect all native sources, original launch/config/schema resources
and its two existing SDF cases. Preserve both SDF cases; add an original-launch
check of object add/modify/delete, detected/ground-truth frames and IDs,
nonempty pointcloud production, the separate empty-object executable and clean
shutdown. The declared acceptance profile uses straight-line objects with zero
velocity and an explicit map/base_link transform.

Integration: declare its SDF and full node libraries, both executables and
component-owned resources, using local perception/simulation interfaces,
trajectory/point-type/utilities and RCR PCL/tf2 providers. Native executables
must consume flattened ROS dynamic dependencies: directly naming only rclcpp
and perception cc_shared leaves unresolved inline rcl/rmw/rcutils symbols.
Use collect_transitive_dynamic_deps, including a small separate closure for
the empty publisher. The root test publishes a transient-local TF fixture,
waits for empty outputs to prove the node's TF listener is ready, then sends
one command per lifecycle transition; repeated ADD would append duplicates.
The index/launch remains component-owned and has no root label dependency.

Reproduce its complete gate after the grouped build recorded above:

```sh
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_dummy_perception_publisher//... \
  //bazel/tests/autoware_dummy_perception_publisher:launch_test
```

The actual grouped invocation also included the simulator/door targets and
ran tests sequentially. Both original SDF cases pass; both root launch cases
pass in 2.9 seconds, with no failures/skips. They verify the original XML/config,
empty producer output, straight-line object ID/classification and map/base_link
frames, pointcloud production, exact stationary positions at x=10 then x=12,
object deletion and clean exit of both executables. This is the selected local
simulated-perception profile; predicted-path motion and full perception tracking/
prediction stacks are not established by this fixture. No source implementation
or original test is changed. Buildifier, Python/flake8 and whitespace checks
pass. The module task is complete for CPU/Cyclone DDS with Agnocast disabled;
Fast DDS, GPU and pre-commit are not run.

## 2026-11-21: Phase 7.3.1 — Vehicle door simulator

Purpose: support the Universe door simulator selected by the original planning
simulator's default launch_dummy_doors=true. There is no existing behavior suite
in its CMake/test declarations. Inspect the original node and launch first;
the root gate requires four-door layout/roles, transient-local status delivery,
opening/opened/closing/closed transitions, invalid index/command rejection and
normal shutdown through the original XML launch.

Integration: add a package-local module, full standalone binary and ament index
for the original XML. Reuse the already-supported local ADAPI door interfaces;
no new interface declarations or source implementation changes are needed.
Flatten the ADAPI/rclcpp shared closure for inline ROS API calls, matching the
standalone dependency pattern exposed by the related perception executable.
The root-only peer checks real services and status updates rather than calling
private C++ methods. Reproduce its complete behavior gate after the grouped
package build above:

```sh
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_vehicle_door_simulator//... \
  //bazel/tests/autoware_vehicle_door_simulator:launch_test
```

Both root cases pass in 12.0 seconds, zero failures/skips: four doors and their
roles, initial closed state, opening/opened/closing/closed reports, explicit
failure messages for invalid index/command, and clean exit through the original
XML. No source implementation is changed. Buildifier, Python/flake8 and whitespace
checks pass. This module task is complete for CPU/Cyclone DDS with Agnocast
disabled; Fast DDS, GPU and pre-commit are not run.

Phase 7.3.1 closes with four local modules and 20 passing cases in six targets:
two connector cases, ten original simulator cases, two original SDF cases and
six root launch/lifecycle cases. Passing results for unchanged perception/door
code are reused after the simulator fixture correction; the complete simulator
suite and connector are revalidated after their affected changes. Next is
7.3.2's selected Universe implementation closure. This does not yet establish
an autonomous planning-simulator session or the original RViz workflow.

## 2026-11-21: Phase 7.3.2 — Universe arrival-checker tf2 compatibility

Purpose: compile the complete existing Universe mission-planner suite against
Lyrical before accepting its Bazel module. Inspect its CMake test declarations,
the original test sources and the pinned tf2 include tree. There are 15 planner/
utility cases and 21 arrival-checker cases in two executables.

The initial build fails in test/test_arrival_checker.cpp because
`tf2/LinearMath/Quaternion.h` is absent from the selected tf2. Replace that include
with `Quaternion.hpp`, retaining every assertion and implementation. Apply the
existing clang-format include grouping. This is a source compatibility change;
keep it separate from the Bazel definitions. The arrival executable supplies its
own main, so its Bazel rule links gtest rather than gtest_main.

With the module declarations in the following mission-planner entry, reproduce:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_mission_planner_universe//:test_autoware_mission_planner_universe_arrival_checker
```

All 21 original arrival cases pass in 0.2 seconds; all 15 planner/utility cases
also pass. The final include grouping passes targeted clang-format. No assertion,
algorithm or original CMake build is changed. The upstream main file still uses
the .h spelling when checked on 2026-11-21; the Lyrical tracker and scoped PR
searches identify no matching fix. Association/status are recorded in
UPSTREAM_AWF.md. Local verification is CPU/Cyclone DDS with Agnocast disabled;
Humble/Jazzy and colcon are not tested.

## 2026-11-21: Phase 7.3.2 — Nested interface source namespaces

Purpose: expand the product-level interface consumer for the current mission
interfaces and compile its complete selected closure. The first plugin analysis
fails on tier4_perception_msgs/object_recognition/DetectedObjectWithFeature,
before compiling a plugin. That package's already-registered message source lives
under msg/object_recognition/, but ROS generates tier4_perception_msgs::msg and
headers under tier4_perception_msgs/msg/. Its traffic_light subdirectory has the
same distinction.

Cause/fix: the root collector inferred interface kind from the source's immediate
parent directory. Infer msg/srv/action from its interface source extension instead;
keep basename and provider-owned package identity. This also preserves existing
flat and category/msg layouts. Do not move source files or add package-specific
path exceptions. Add one root regression test against the actual collected nested
Tier IV message descriptors, without checking source-directory completeness.
The bridge implementation/templates and registered interface surface are unchanged.

Reproduce with the final interface selection and then test sequentially:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=runtime --config=no-agnocast \
  //bazel/interfaces:all_interfaces //bazel/agnocast:plugins
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/interfaces:descriptor_test //bazel/agnocast:template_tests
```

The full plugin library links successfully with 252 collected identities and
289 bridge instantiations (1161 seconds for this build). The descriptor and
template gate passes. The merged package CcInfo makes an added provider capable of
changing compiler inputs across the plugin library; this run recompiles all
289 bridge units. Keep the full plugin build for interface-selection changes
and reuse it for unchanged implementation-only integrations. This verifies
canonical metadata/header/type instantiation,
including the already-selected nested perception messages; no enabled bridge
transport or full communication matrix is claimed. No kernel module or Fast DDS
check is required for this metadata/template gate. Targeted buildifier,
Python/flake8 and whitespace checks pass. Preserve source-layout independence in
future all-interface consumers; package/kind/Name is a ROS identity rather than
a relative source path.

## 2026-11-21: Phase 7.3.2 — Tier IV reroute interface

Purpose: supply the local RerouteAvailability message required by Universe
mission planning and manual lane selection. Inspect tier4_planning_msgs 0.1.0,
its manifest/CMake interface list, the message fields and both consuming nodes.
There is no existing behavior suite, only lint declarations. Register the one
currently required message; the other unregistered sources remain outside the
Bazel graph by design.

Integration: add a package-local MODULE.bazel retaining manifest-level interface
dependencies, msg/BUILD.bazel with ros_message(RerouteAvailability) depending on
builtin_interfaces/Time, and default_generators at the package root. Its generator
dependency list contains only builtin_interfaces. Select the local package with
an adjacent root bazel_dep/local_path_override pair and register its cc/cc_shared
providers once in bazel/interfaces/BUILD.bazel. The existing generic collector
and bridge assembly consume this registration without another interface list.

Build and test this provider sequentially in the existing pinned Lyrical/LLVM
environment (the actual grouped build also selected the preferred-lane provider):

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=runtime --config=no-agnocast \
  @tier4_planning_msgs//... \
  //bazel/interfaces:all_interfaces //bazel/agnocast:plugins
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests/tier4_planning_msgs:serialization_test
```

The serialization case passes, preserving timestamp and availability across
Cyclone DDS CDR serialization/deserialization. Both actual native consumers
also compile and exchange the availability message in the manual-selection
launch gate below. All declared generated targets and the expanded plugin library build successfully. No original interface or node source
is changed. Targeted buildifier, Python/flake8 and whitespace checks pass. This
accepts the declared interface surface; additional types require explicit BUILD
registration when their consumers are integrated. No Fast DDS check is run.

## 2026-11-21: Phase 7.3.2 — Tier IV preferred-lane service

Purpose: supply the local SetPreferredLane service used by the default manual
lane handler. Inspect tier4_external_api_msgs 0.0.0, its manifest/CMake, the
service and its native consumer. There is no existing behavior suite. Register
this service only, preserving its LEFT=0, RIGHT=1, AUTO=2 contract and its
external autoware_common_msgs/ResponseStatus response; the package's similarly
named legacy ResponseStatus is not that response type.

Integration follows the reroute interface entry: a local module/root override,
srv/BUILD.bazel with ros_service and the referenced common status target,
package-root default_generators with autoware_common_msgs as its generator
dependency, and one central collector provider pair. Retain all manifest-level
interface dependencies in MODULE.bazel, including builtin_interfaces from its
build/exec declarations. Lyrical generates request, response and service-event
messages alongside the service.

Build this provider and the selected plugin closure, then test sequentially:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=runtime --config=no-agnocast \
  @tier4_external_api_msgs//... \
  //bazel/interfaces:all_interfaces //bazel/agnocast:plugins
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests/tier4_external_api_msgs:serialization_test
```

The case passes for request, response and event wire round trips, retaining the
three constants, direction, success/code/message and nested payloads. Without
the test's LD_LIBRARY_PATH=lib, Cyclone reports missing common-message C
introspection type support; generated Python imports alone do not establish
that runtime search path. Set it in the native py_test environment, following
the existing native ROS convention. All declared generated targets and the expanded plugin library build successfully. The real service
also passes rejection, preference-update and reset behavior in the manual
handler gate below. Original sources are unchanged; buildifier, Python/flake8
and whitespace checks pass. CPU/Cyclone DDS only, no Fast DDS check.

## 2026-11-21: Phase 7.3.2 — Universe mission planner

Purpose: integrate the default Universe mission-planning implementation required
by the local planning-simulator launch. Inspect its package.xml, CMake, all
sources, original config/XML launches and both complete original GTest binaries:
15 planner/utility and 21 arrival cases. The acceptance gate also requires the
three original node executables, a valid six-segment route, public selector
forwarding, UUID preservation, echoed goal, invalid-preference rejection,
arrival, clearing and normal shutdown through the original mission/visualizer
XML. It does not establish vehicle motion or full lane-driving acceptance.

Integration: declare the local module and native library/shared component with
explicit local utility/map/planning dependencies. Expose service_utils as a
small public library so the manual handler consumes that API without linking
node factories. Register mission_planner and route_selector with their original
MultiThreadedExecutor ROS fallback, and goal_pose_visualizer with its original
SingleThreadedExecutor; use the existing Agnocast wrapper's generated executables
and resource records. Use a package-specific shared-library identity and index
original config/launch resources and executable names. Component definitions
have no root labels. Preserve both original test binaries, linking gtest for
the arrival executable's own main and gtest_main for planner/utilities.

The root-only fixture composes the already-integrated core map launch, the
original test-utils map and vehicle YAML, and the original Universe mission and
goal-visualizer launches. Its MGRS grid must be 54SVE, matching the map's own
metadata and the original tests' coordinates. Publish stationary map-frame
odometry and STOP operation mode; drive real SetWaypointRoute/SetPreferredPrimitive/
ClearRoute services. Keep the selector's internal planner route topic distinct
from its public /planning/route output: remapping both to the same topic creates
a forwarding loop. Assert the same UUID on public and internal deliveries.
The external inputs are fixtures; they are not a substituted planning stack.

Two consequential failures guide the build pattern. The original arrival test's
removed tf2 .h include needs the separate compatibility fix above. The node then
links but exits during startup because BasicLoggerLevelConfigure dynamically
loads logging_demo service type support. Declare logging_demo//:cc_shared in the
flattened runtime dependencies and its module dependency; it must be reachable
from the node, not supplied incidentally by another test's index. Declare the
wrapper's ament_package in fixture data for its XML include. These are build/
runtime closure declarations, with no planner implementation change.

Reproduce the native and original-launch gates sequentially:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_mission_planner_universe//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests/autoware_mission_planner_universe:launch_test
```

The actual native invocation grouped both node modules, both interface peers and
both root launch targets. All 36 original cases pass (0.3/0.2 seconds). The final
fixture-and-metadata invocation passes both mission launch cases in 2.8 seconds, checking
route IDs 9102/9540/9546/9178/52/124, public/internal UUID, goal frame/position,
SET/ARRIVED/UNSET states, explicit malformed-preference rejection and clean exit
of all seven processes. Unchanged native results are reused after fixture-only
corrections. Targeted buildifier, Python/flake8, clang-format and whitespace
checks pass. The module gate is complete for CPU/Cyclone DDS, ENABLE_AGNOCAST=0;
no Fast DDS, enabled-Agnocast runtime, GPU, RViz or full simulator gate is claimed.

## 2026-11-21: Phase 7.3.2 — Manual lane selection

Purpose: integrate the manual lane handler included by the default Universe
planning launch. Inspect all source/CMake/manifest/XML inputs. It has no original
behavior test suite. The declared gate requires the original component/executable
and launch, no-route and unavailable-reroute rejection, one actual preferred-
primitive update, AUTO restoration and clean shutdown against the real Universe
mission planner. This checks preference metadata, not physical lane-change safety
or vehicle execution.

Integration: add a local module/root override, native library and package-specific
shared component; use the ordinary RCR rclcpp component executable because this
source derives directly from rclcpp::Node. Preserve its executable/plugin names
and index original XML. Depend on the mission package's service_utils API,
existing local route/map/utilities and the two new Tier IV interface providers.
Flatten the native ROS/type-support shared closure. The root test reuses the
mission fixture; the component has no dependency on root test labels.

Fixture know-how: lane 9102 has no adjacent road lane. The test map has lane
10310 to the left of 9178, separated by a solid boundary; the waypoint planner
correctly omits it from its alternatives. First exercise normal waypoint routing,
then clear and supply that explicit alternative using SetLaneletRoute. Position
the stationary peer inside 9178 at (3753.36, 73739.80). This deliberately tests
the handler's explicit-route preference API, without asserting that a crossing
of that solid boundary is a feasible driving maneuver. Do not modify the
original map or planner to make the fixture pass. Publish availability repeatedly;
service discovery alone does not mean its polling subscription has consumed it.
Retry only the explicit 'Not in lane driving state' readiness response, then
submit exactly one successful shift. Wait for both the debug shift and the
mission planner's changed route, because the handler acknowledges before its
asynchronous planner request completes. AUTO must restore the full original
preference vector and preserve the UUID.

Reproduce after the mission provider is built:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=runtime --config=no-agnocast \
  @autoware_manual_lane_change_handler//...
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests/autoware_manual_lane_change_handler:launch_test
```

Both final launch cases pass in 2.5 seconds: correct rejection messages, shift
9178 -> 10310 with debug count -1, AUTO restoration/count 0, retained UUID,
clearing and clean exit of all eight processes. No source implementation or
original test is changed. Buildifier, Python/flake8 and whitespace checks pass.
The module gate is complete for CPU/Cyclone DDS, ENABLE_AGNOCAST=0; this ordinary
rclcpp node has no Agnocast backend switch. No Fast DDS check is run.

This first 7.3.2 group adds four local modules and passes 42 cases across six
targets: 36 original mission cases, two wire-serialization cases and four root
launch/lifecycle cases. The collector regression and bridge-template checks also pass. Passing unchanged
native/interface results are reused while finalizing the launch fixtures. Phase 7.3.2 remains in progress: behavior/
motion planning, control and the remaining default launch-selected dependencies
must enter the graph before the original planning-simulator composition gate.

## 2026-11-21: Phase 7.3.3 — Tutorial vehicle description

Purpose: prepare the concrete sample_vehicle input named by AWF's planning
simulation tutorial. The existing core autoware_sample_vehicle_description has
a different package identity; do not alias it over this locally available source.
Inspect sample_vehicle_description's package.xml, CMake install list, Xacro,
vehicle/simulator YAML and COLLADA/image resources in the local autoware_launch
snapshot (upstream subtree revision 45bd52eb298a9700c51eef556734c6cb0e4af364).
CMake declares resource installation and no existing behavior test suite.

Integration: add a package-local module, original resource globs under an ament
index and the existing pinned Xacro provider. Select this local module with the
root's adjacent bazel_dep/local_path_override pair. Keep original names and all
source/config/resource bytes unchanged. Add a root-only resource check that
materializes the installed share tree into a fresh temporary prefix, creates its
package marker and replaces AMENT_PREFIX_PATH with that prefix. Render the
original vehicle Xacro there; require base_link, its wheel-base-derived visual
offset, its mesh and every COLLADA image reference to resolve in that prefix.
The simulator YAML must also remain indexed.

Transferable detail: COLLADA library_images/image/init_from contains image file
references; effect/surface init_from may instead name an image ID such as
texture0. Checking every init_from as a filesystem path incorrectly rejects a
valid unchanged asset. Restrict the resource check to image definitions.

Reproduce the module/resource gate:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @sample_vehicle_description//... \
  //bazel/tests/sample_vehicle_description:resources_test
```

The corrected resource case passes in 0.5 seconds, with no failure or skip.
Buildifier, Python/flake8 and whitespace checks pass. The combined tutorial
vehicle/sensor robot-state-publisher gate is recorded in the next entry.
This accepts the description resource module, not a vehicle interface driver
or a complete planning simulation. PLAN.md's detailed 7.2/7.3 sections are also
reordered numerically; that editorial move needs local-link/whitespace checks
and has no separate runtime validation requirement.

## 2026-11-21: Phase 7.3.3 — Tutorial sensor description

Purpose: prepare the tutorial's concrete sample_sensor_kit input and demonstrate
that its description composes with sample_vehicle. Inspect the manifest, CMake,
original sensors/sensor-kit Xacro, calibration YAML, camera/IMU/VLS templates and
meshes in the same autoware_launch snapshot as the vehicle entry. No existing
behavior suite is declared. The required velodyne_description is already local
and Bazel-supported; retain that provider instead of fetching another version.

Integration: add a local resource module/index covering original config, urdf
and sensor trees, with the velodyne/Xacro indexes in its runtime dependencies.
Add the root source override. The root-only resource test copies this package
and velodyne into an isolated prefix, uses that prefix alone for package lookup,
expands the original sensors.xacro and verifies the expected top/left/right/rear
lidars, IMU, calibration transform and every mesh URI. No source implementation,
Xacro, original calibration or original test is modified.

Extend the existing root description-launch peer with an explicit vehicle-model
selection, preserving its existing default and core sample/AWSIM rows. Add the
sample_vehicle/sample_sensor_kit row. The already-supported generic core vehicle
launch composes these real description providers with the native robot state
publisher; its dynamic Xacro contract matches the selected model inputs. This
is a resource/TF test, not a replacement for the pending full Universe launcher.
Assert real robot_description and transient-local static transforms, calibrated
base_link -> sensor_kit_base_link translation (0.9, 0, 2), the rear lidar and
resolvable meshes; require normal process shutdown.

Reproduce sequentially:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @sample_sensor_kit_description//... \
  //bazel/tests/sample_sensor_kit_description:resources_test \
  //bazel/tests:vehicle_description_sample_launch_test \
  //bazel/tests:vehicle_description_awsim_launch_test \
  //bazel/tests:vehicle_description_universe_sample_launch_test
```

The grouped invocation also included the vehicle module/resource target.
The sensor relocation case passes in 0.6 seconds; all six launch/shutdown cases
pass (core sample 1.5 seconds, AWSIM 1.4 seconds, tutorial sample 2.0 seconds),
without failures/skips. Passing sensor and launch results are reused after the
vehicle-only COLLADA check correction. Targeted buildifier, Python/flake8 and
whitespace checks pass. The module gate is complete for CPU/Cyclone DDS with
Agnocast disabled. Gazebo plugin execution, sensor drivers and full Universe
startup are outside this description-resource contract; no Fast DDS check runs.

## 2026-11-21: Phase 7.3.3 — Official tutorial map and scenario resources

Purpose: make the documented planning-simulator map an explicit build input and
pin scenario geometry before adding autonomous-driving assertions. Review both
AWF's version 1.4.0 planning tutorial and current planning-simulation preparation/
lane-driving pages on 2026-11-21, then inspect the local demo_artifacts role's
map URL read-only. Both tutorials name sample-map-planning, sample_vehicle and
sample_sensor_kit; the current role supplies the official S3 archive. Do not
extend or invoke Ansible to provide a Bazel dependency.

Accepted input: sample-map-planning.zip from the official URL, SHA-256
5536fce7bb8db7688fdf94ec004118b898637ad0d5b6175108b10989dd6e93b9. The hash,
not the mutable URL, identifies the accepted data. Inspect its four payloads,
archive member timestamps, OSM structure, projector YAML and PCD header. Exact
URLs, per-file hashes/sizes, attribution and reference links are maintained in
[assets/map/README.md](../../assets/map/README.md). No payload is changed or
committed into the source tree.

Integration under the current asset convention: select the local module
at assets/map with a root bazel_dep/local_path_override pair. Its MODULE.bazel
owns http_archive, pins SHA-256, strips sample-map-planning/ and applies its
exports-only BUILD overlay. The provider registers those four files at
share/autoware_planning_simulator_map and exports its ament index. Consumers
use @autoware_planning_simulator_map//:ament_index and resolve the share directory
through the index. There is no HOME lookup, absolute workspace path or runtime
fetch. This product map archive is separate from per-repository model indexes.

Pin the root scenario YAML: frame map, MGRS/54SVE, sample_vehicle and
sample_sensor_kit, initial (3717.239501953125, 73720.84375, 0, yaw 0.48724),
goal (3732, 73728.6, 0, yaw 0.48724). The existing Universe DefaultPlanner,
using the real indexed vehicle YAML dimensions and its default footprint/goal-
angle settings, accepts the route through preferred lanelets [9102, 9540].
The approximately 16.7 m route is a lane-driving candidate; this gate does not
prove controller behavior, engagement, goal arrival or screenshot-derived pose
coordinates. The tutorial's GUI lets the user choose the poses; these exact
coordinates make the corresponding future headless choice reproducible.

Add root format/relocation checks and a native consumer gate. The OSM contains
4499 nodes and 241 relations; its legacy format omits MetaInfo, so route_handler
warns about the absent version while accepting the loaded map. Preserve the
archive rather than inventing metadata. Use map_projector_info.yaml's MGRS,
54SVE and WGS84 values for the current loader contract. The PCD declares binary
XYZ/RGB data with 1757841 points and retains 186 trailing zero padding bytes;
strict file-size equality incorrectly rejects this original artifact. Verify
the declared payload/padding and load it through the real pinned PCL reader,
which returns the expected dimensions/data size. Copy the indexed resources to
a fresh prefix and verify package lookup there, independently of HOME/source
paths. No new completeness validator or original-package edit is introduced.

Reproduce after the two description modules:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests/planning_simulator_resources:resources_test \
  //bazel/tests/planning_simulator_resources:route_test
```

Both format/relocation cases pass in the Python target (0.5 seconds), and both
native cases pass (0.6 seconds): nonempty routable geometry with exact preferred
lanelets/goal and PCL load of the complete pointcloud. The vehicle-only resource
correction also passes; unchanged sensor/launch results are reused. The first
resource group totals 12 passing cases across seven targets, no failures/skips.
Targeted buildifier, Python/flake8, clang-format, local Markdown links/anchors
and whitespace checks pass. CPU/Cyclone DDS with ENABLE_AGNOCAST=0 only; no
Fast DDS, GUI, autonomous vehicle motion or packaging rerun is claimed.

The independent resource portion of 7.3.3 has started while 7.3.2 remains open.
Full original planning_simulator.launch.xml composition still requires its
remaining selected behavior/motion-planning, control and system/API dependency
closure. Neither 7.3.2 nor 7.3.3 is declared complete by these resource gates;
7.3.4 retains full original workflow/RViz and autonomous-driving acceptance.

## 2026-11-21: Phase 7.3.2 — Boundary-checker expected compatibility

Purpose: compile the shared boundary-departure library required by default
Universe path-planning and control modules. The first native build fails on
its removed tl_expected/expected.hpp wrapper include. Inspect all four include
sites, the manifest, the pinned RCR provider and AWF's actual PR #13622 files;
check the merged PR status on 2026-11-21. The native tl-expected provider is
1.3.1 and exports tl/expected.hpp.

Apply only this package's matching upstream correction: replace the four
includes with tl/expected.hpp and change the manifest dependency from the ROS
wrapper to libexpected-dev. The original implementation and assertions are
unchanged. Keep these five source/manifest edits in their own source-fix commit;
Bazel still builds the pinned native provider, without installing a host package.
The broader PR's controller/perception changes are not pre-applied.

With the module declarations in the following library entry, reproduce:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_deprecated_boundary_departure_checker//...
```

All 66 original cases pass, including parameterized resampling cases, without
failure or skip. The grouped invocation also passes the two traffic-light and
one RTC case. Targeted clang-format/whitespace checks pass. This is Lyrical/
dependency-header compatibility, not an algorithm change; no colcon, Humble,
Jazzy or enabled-Agnocast runtime validation is claimed. The matching upstream
association and scope are recorded in UPSTREAM_AWF.md.

## 2026-11-21: Phase 7.3.2 — Traffic-light utility library

Purpose: provide the shared signal/geometry helpers used by default path and
velocity-planning modules. Inspect the local package manifest, public header,
implementation, CMake and its one existing test source. Preserve both original
cases rather than replacing the library with another message/API generation.

Integration: add a package-local module and header-owning native cc_library,
using the existing local Autoware/Tier IV perception interface providers and
RCR lanelet2/tf2. Expose the flattened message/tf2 runtime dependencies for native
consumers. Add the local root override and an ament index for package identity;
there are no original runtime resource directories or standalone nodes to add.
Expose the unchanged existing test source with gtest_main.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @autoware_traffic_light_utils//...
```

Both original cases pass in 0.1 seconds: unknown-signal conversion retains its
expected confidence and signal-center geometry yields the mean coordinates.
The actual grouped invocation also includes RTC and boundary checking. No
source or original test is changed. Buildifier and whitespace checks pass.
The module is complete for the selected CPU/Cyclone DDS profile; this library
check does not establish traffic-light detection or a complete driving stack.
No Fast DDS run is needed or performed.

## 2026-11-21: Phase 7.3.2 — Shared boundary-departure library

Purpose: supply the complete local boundary/footprint implementation needed by
the default Universe path-planning and control dependency closure. Inspect its
three CMake library definitions, public headers, source files, all nine original
test sources and plotting option. The existing default suite has 66 cases once
parameterized tests are instantiated; visualization export defaults to OFF.

Integration: expose the original production sources/public headers through a
native package cc_library with explicit local trajectory/motion/vehicle/utility
and message dependencies. Keep the selected SDK headers native: Boost, Eigen,
fmt, range-v3, magic_enum and tl-expected come from the pinned RCR providers.
Declare the complete flattened ROS/tf2/diagnostic shared dependency closure.
The test rule includes every original test source and its helper headers, the
original src/test include roots and TEST_PACKAGE_NAME definition, plus the
already-supported pyplot/Python embedding headers and libraries. Preserve the
upstream default of no figure export; no assertion or test group is disabled.
Use the conversion library provider for tf2_geometry_msgs, whose name does not
make it a generated message package.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_deprecated_boundary_departure_checker//...
```

After the separate expected-header correction above, all 66 existing cases pass
in 0.2 seconds, zero failures/skips. Coverage includes boundary crossing,
footprint generators, passing areas, trajectory cuts/projections/search ranges
and parameterized resampling. There is no dependency-based test omission or
source behavior change. Targeted buildifier/clang-format and whitespace checks
pass. This completes the library's selected default suite, CPU/Cyclone DDS with
Agnocast disabled; figure-output tooling and full driving acceptance are not
claimed. The remaining 7.3.2 dependency groups continue before 7.3.3 composition.

## 2026-11-21: Phase 7.3.2 — Freespace test filesystem compatibility

Purpose: compile the complete original freespace algorithm suite against the
pinned Lyrical rcpputils API. Compilation exposes a removed
rcpputils::fs::remove_all call in the optional rosbag-output cleanup helper.
Inspect the helper and its callers: this is directory cleanup, not a ROS path
abstraction or a planning operation. Replace the obsolete helper include with
the C++17 filesystem header and use std::filesystem::remove_all; leave solver
parameters, test assertions and bag-output defaults unchanged.

Checked 2026-11-21: the upstream main copy still contains the obsolete call
(test source blob 49b85f9a9017415a61b8a41d67159f2bbb21724b). A scoped upstream
PR search for freespace/filesystem found no matching correction; this is not
evidence that no wider compatibility PR exists. Record that distinction in
UPSTREAM_AWF.md. Keep this original-test correction in its own source commit.

The following module entry gives the exact full-suite command. All ten original
freespace cases and both original RRT cases pass, as do both added Python checks;
there are no failures or skips. Targeted clang-format and whitespace checks pass.
This demonstrates CPU/Lyrical compilation and unchanged default test execution;
optional bag export and older ROS distributions are not validated.

## 2026-11-21: Phase 7.3.2 — Freespace planning algorithms and Python binding

Purpose: provide the original A*/RRT implementation used by the selected start
and goal planners. Inspect CMake, all production sources/public headers, both
original native test sources, the pybind source, original Python wrappers and
debug_plot.py. Preserve both original native test executables and the binding;
do not reduce this package to the C++ library needed by its first consumer.

Integration: add a package-local module/library with explicit local geometry,
math and vehicle providers plus pinned Boost, JSON and ROS/tf2 providers. Flatten
the shared runtime dependencies. Expose the original tests: the main freespace
test supplies its own global main, whereas the RRT source's main is inside a
namespace and needs gtest_main. This is a build declaration choice, not a reason
to edit the original test. The separate filesystem correction above resolves
the actual source compatibility blocker.

Build the original pybind source as the package-nested extension, using the
selected rules_python interpreter's headers/libraries. Keep the original wrapper
imports and install both wrappers and extension in that interpreter's Python
site-packages through the package-owned ament index. Expose/install the original
debug tool with its declared matplotlib, NumPy and rosbag Python dependencies;
include the pinned MCAP storage plugin in the index, not a host installation.
The root owns the dependency version selection. The two added tests under
bazel/tests/autoware_freespace_planning_algorithms exercise an actual A* plan
through the Python wrapper/CDR/native boundary and the debug tool's import/help.
LD_LIBRARY_PATH=lib makes nested ROS C introspection libraries available in the
test environment; it does not introduce a source-tree or host ROS lookup.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_freespace_planning_algorithms//... \
  //bazel/tests/autoware_freespace_planning_algorithms:binding_test
```

All 14 cases pass across three targets: ten original freespace cases in 56.7 s,
two original RRT cases in 6.8 s and two Python checks in 2.2 s, no failures/skips.
The Python case obtains a greater-than-5 m path from (-3, 0) to (3, 0), checks
endpoint poses and obstacle-distance access using the original binding. Targeted
buildifier, clang-format, Python/flake8 and whitespace checks pass. This completes
the CPU/Cyclone DDS, ENABLE_AGNOCAST=0 module gate. Interactive plot rendering,
optional bag export, full parking behavior and Fast DDS are not claimed.

## 2026-11-21: Phase 7.3.2 — Generated interface wire checks

Purpose: validate the message/service providers needed by the selected simulator
implementation closure without introducing a source-directory completeness test.
Inspect the selected planning/control/system/perception source headers and Python
imports, follow referenced interface fields, and preserve each package's existing
supported surface. BUILD declarations remain the registration authority;
undeclared files do not enter the graph. Mixed implementation/interface packages
retain their own boundaries and enter with their implementation tasks.

Add the small product-owned helper under bazel/tests/ros_interfaces. Each caller
passes a package name and explicitly depends on that provider's generated Python
library. The test imports the generated public message/service classes and checks
CDR encode/decode/encode stability for default messages and every service Request,
Response and Event. This exercises the native type-support extension and nested
runtime libraries; it does not scan source directories, enforce registration or
claim nondefault payload/service behavior. Use LD_LIBRARY_PATH=lib for the
runfiles runtime layout. Keep existing package-specific payload/constant tests.
A std_srvs caller also checks the helper against an already-supported provider.

Reproduce the shared checks with CPU/Cyclone DDS and Agnocast disabled:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests/ros_interfaces:generated_serialization_test \
  //bazel/interfaces:descriptor_test //bazel/agnocast:template_tests
```

The helper's std_srvs check passes all nine Request/Response/Event types. The
nine new/expanded provider checks pass another 115 generated wire types; the
existing preferred-lane and reroute nondefault payload tests, descriptor/template
checks and original RTC suite also pass. The grouped invocation totals 15 passing
test targets, zero failures/skips, CPU/Cyclone DDS with ENABLE_AGNOCAST=0. Native
provider libraries are built as dependencies of these checks. The separate full
plugin-consumer gate is recorded with the collector entry below; it does not
require rerunning these unchanged tests.

Diagnosis: inspecting classes directly on package.msg/package.srv initially finds
zero types because the pinned RCR import hook exposes them lazily through
`__getattr__`, without `__dir__`. Enumerate that provider's generated Python modules,
import them, then select their message classes/service Request/Response/Event
classes. Do not replace this with a scan of original schema sources or a copied
interface registry. Check at least one wire type so an empty discovery cannot
pass silently. Message-only packages and the service-only std_srvs provider both
pass. Targeted buildifier, Python/flake8 and whitespace checks pass. No enabled
Agnocast communication or Fast DDS check is selected.

## 2026-11-21: Phase 7.3.2 — Control-command selection, pause and stop interfaces

Purpose: supply tier4_control_msgs from its local source snapshot. Inspect the
package's manifest, CMake rosidl declarations, selected consumer includes and
referenced message/service fields. The complete original eight-interface
surface is needed by the selected external-command and vehicle-gate code.

Use package-local MODULE/BUILD files, root local override selection and explicit
ros_message/ros_service field dependencies. Keep original schema bytes unchanged.
The selected provider contains 8 interfaces; manifest dependencies stay in
MODULE.bazel, while default_generators.dependencies contains only packages
referenced by generated fields. Add the root-owned wire-test caller described in
[the shared check](#2026-11-21-phase-732--generated-interface-wire-checks).

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests/tier4_control_msgs:all
```

The generated-provider check passes all 14 message/Request/Response/Event
wire types in 0.7 seconds (the same grouped CPU/Cyclone DDS gate above),
without failures or skips. The complete expanded plugin-consumer build also
passes. Targeted buildifier and whitespace checks pass; original schemas are
unchanged. Simulator launch behavior and enabled-Agnocast communication are
deferred.

## 2026-11-21: Phase 7.3.2 — Legacy stamped debug and processing-time interfaces

Purpose: supply tier4_debug_msgs from its local source snapshot. Inspect the
package's manifest, CMake rosidl declarations, selected consumer includes and
referenced message/service fields. The legacy Universe utility/planning
consumers require the original Tier IV namespace; retain it alongside the newer
internal debug provider. SystemUsage interfaces are outside the selected
consumers.

Use package-local MODULE/BUILD files, root local override selection and explicit
ros_message/ros_service field dependencies. Keep original schema bytes unchanged.
The selected provider contains 14 interfaces; manifest dependencies stay in
MODULE.bazel, while default_generators.dependencies contains only packages
referenced by generated fields. Add the root-owned wire-test caller described in
[the shared check](#2026-11-21-phase-732--generated-interface-wire-checks).

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests/tier4_debug_msgs:all
```

The generated-provider check passes all 14 message/Request/Response/Event
wire types in 0.7 seconds (the same grouped CPU/Cyclone DDS gate above),
without failures or skips. The complete expanded plugin-consumer build also
passes. Targeted buildifier and whitespace checks pass; original schemas are
unchanged. Simulator launch behavior and enabled-Agnocast communication are
deferred.

## 2026-11-21: Phase 7.3.2 — External emergency, engage and map interfaces

Purpose: supply tier4_external_api_msgs from its local source snapshot. Inspect
the package's manifest, CMake rosidl declarations, selected consumer includes
and referenced message/service fields. Extend the existing preferred-lane
service provider for the selected API and command-gate consumers. Preserve
SetPreferredLane and its existing nondefault request/response/event test. The
extensionless autoware_map_loader/script/map_hash_generator additionally
imports MapHash and GetTextFile; declare both plus the referenced TextFile
type. Inspect installed extensionless scripts as well as .py files when finding
actual consumers. This task exposes interfaces only; the map executable and
launch gate are deferred.

Use package-local MODULE/BUILD files, root local override selection and explicit
ros_message/ros_service field dependencies. Keep original schema bytes unchanged.
The selected provider contains 8 interfaces; manifest dependencies stay in
MODULE.bazel, while default_generators.dependencies contains only packages
referenced by generated fields. Add the root-owned wire-test caller described in
[the shared check](#2026-11-21-phase-732--generated-interface-wire-checks).

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests/tier4_external_api_msgs:all
```

The generated-provider check passes all 16 message/Request/Response/Event
wire types in 0.7 seconds (the same grouped CPU/Cyclone DDS gate above),
without failures or skips. The complete expanded plugin-consumer build also
passes. Targeted buildifier and whitespace checks pass; original schemas are
unchanged. Simulator launch behavior and enabled-Agnocast communication are
deferred. The existing preferred-lane payload and
constant check also passes unchanged.

## 2026-11-21: Phase 7.3.2 — Metric report interfaces

Purpose: supply tier4_metric_msgs from its local source snapshot. Inspect the
package's manifest, CMake rosidl declarations, selected consumer includes and
referenced message/service fields. Both original interfaces are required by the
selected planning and perception evaluators.

Use package-local MODULE/BUILD files, root local override selection and explicit
ros_message/ros_service field dependencies. Keep original schema bytes unchanged.
The selected provider contains 2 interfaces; manifest dependencies stay in
MODULE.bazel, while default_generators.dependencies contains only packages
referenced by generated fields. Add the root-owned wire-test caller described in
[the shared check](#2026-11-21-phase-732--generated-interface-wire-checks).

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests/tier4_metric_msgs:all
```

The generated-provider check passes all 2 message/Request/Response/Event
wire types in 0.7 seconds (the same grouped CPU/Cyclone DDS gate above),
without failures or skips. The complete expanded plugin-consumer build also
passes. Targeted buildifier and whitespace checks pass; original schemas are
unchanged. Simulator launch behavior and enabled-Agnocast communication are
deferred.

## 2026-11-21: Phase 7.3.2 — Shared path, avoidance, offset and stop-factor interfaces

Purpose: supply tier4_planning_msgs from its local source snapshot. Inspect the
package's manifest, CMake rosidl declarations, selected consumer includes and
referenced message/service fields. Extend the existing reroute-availability
provider for the selected behavior planners and preserve its original payload
test. PathWithLaneId uses the local autoware_planning_msgs PathPoint
definition.

Use package-local MODULE/BUILD files, root local override selection and explicit
ros_message/ros_service field dependencies. Keep original schema bytes unchanged.
The selected provider contains 10 interfaces; manifest dependencies stay in
MODULE.bazel, while default_generators.dependencies contains only packages
referenced by generated fields. Add the root-owned wire-test caller described in
[the shared check](#2026-11-21-phase-732--generated-interface-wire-checks).

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests/tier4_planning_msgs:all
```

The generated-provider check passes all 10 message/Request/Response/Event
wire types in 0.7 seconds (the same grouped CPU/Cyclone DDS gate above),
without failures or skips. The complete expanded plugin-consumer build also
passes. Targeted buildifier and whitespace checks pass; original schemas are
unchanged. Simulator launch behavior and enabled-Agnocast communication are
deferred. The existing reroute payload check also
passes after adding LD_LIBRARY_PATH=lib to its test environment: the expanded
aggregate introspection library now needs std_msgs type support at dlopen time.
Keep the runfiles library path, rather than installing host ROS libraries.
Preserve the existing public RerouteAvailability.msg file export as well as its
message target while extending the package.

## 2026-11-21: Phase 7.3.2 — RTC coordination messages and services

Purpose: supply tier4_rtc_msgs from its local source snapshot. Inspect the
package's manifest, CMake rosidl declarations, selected consumer includes and
referenced message/service fields. Expose all nine original messages and three
services for the RTC helper used by selected behavior modules. UUID and builtin
time fields keep explicit type dependencies.

Use package-local MODULE/BUILD files, root local override selection and explicit
ros_message/ros_service field dependencies. Keep original schema bytes unchanged.
The selected provider contains 12 interfaces; manifest dependencies stay in
MODULE.bazel, while default_generators.dependencies contains only packages
referenced by generated fields. Add the root-owned wire-test caller described in
[the shared check](#2026-11-21-phase-732--generated-interface-wire-checks).

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests/tier4_rtc_msgs:all
```

The generated-provider check passes all 18 message/Request/Response/Event
wire types in 0.7 seconds (the same grouped CPU/Cyclone DDS gate above),
without failures or skips. The complete expanded plugin-consumer build also
passes. Targeted buildifier and whitespace checks pass; original schemas are
unchanged. Simulator launch behavior and enabled-Agnocast communication are
deferred.

## 2026-11-21: Phase 7.3.2 — Diagnostic graph and operation/MRM interfaces

Purpose: supply tier4_system_msgs from its local source snapshot. Inspect the
package's manifest, CMake rosidl declarations, selected consumer includes and
referenced message/service fields. Register the graph structure/status and
selected mode/MRM service closure used by the default system/API configuration.
Default-disabled system implementations do not enter merely because their
interface definitions are present.

Use package-local MODULE/BUILD files, root local override selection and explicit
ros_message/ros_service field dependencies. Keep original schema bytes unchanged.
The selected provider contains 24 interfaces; manifest dependencies stay in
MODULE.bazel, while default_generators.dependencies contains only packages
referenced by generated fields. Add the root-owned wire-test caller described in
[the shared check](#2026-11-21-phase-732--generated-interface-wire-checks).

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests/tier4_system_msgs:all
```

The generated-provider check passes all 30 message/Request/Response/Event
wire types in 0.7 seconds (the same grouped CPU/Cyclone DDS gate above),
without failures or skips. The complete expanded plugin-consumer build also
passes. Targeted buildifier and whitespace checks pass; original schemas are
unchanged. Simulator launch behavior and enabled-Agnocast communication are
deferred.

## 2026-11-21: Phase 7.3.2 — Virtual traffic-light infrastructure interfaces

Purpose: supply tier4_v2x_msgs from its local source snapshot. Inspect the
package's manifest, CMake rosidl declarations, selected consumer includes and
referenced message/service fields. All five original interfaces support the
selected virtual-traffic-light velocity module, including nested key/value
commands and timestamped state arrays.

Use package-local MODULE/BUILD files, root local override selection and explicit
ros_message/ros_service field dependencies. Keep original schema bytes unchanged.
The selected provider contains 5 interfaces; manifest dependencies stay in
MODULE.bazel, while default_generators.dependencies contains only packages
referenced by generated fields. Add the root-owned wire-test caller described in
[the shared check](#2026-11-21-phase-732--generated-interface-wire-checks).

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests/tier4_v2x_msgs:all
```

The generated-provider check passes all 5 message/Request/Response/Event
wire types in 0.7 seconds (the same grouped CPU/Cyclone DDS gate above),
without failures or skips. The complete expanded plugin-consumer build also
passes. Targeted buildifier and whitespace checks pass; original schemas are
unchanged. Simulator launch behavior and enabled-Agnocast communication are
deferred.

## 2026-11-21: Phase 7.3.2 — Actuation, battery and emergency interfaces

Purpose: supply tier4_vehicle_msgs from its local source snapshot. Inspect the
package's manifest, CMake rosidl declarations, selected consumer includes and
referenced message/service fields. Register the selected vehicle-interface and
command-converter field closure. Retain manifest dependencies even where the
generated selected types do not reference them.

Use package-local MODULE/BUILD files, root local override selection and explicit
ros_message/ros_service field dependencies. Keep original schema bytes unchanged.
The selected provider contains 6 interfaces; manifest dependencies stay in
MODULE.bazel, while default_generators.dependencies contains only packages
referenced by generated fields. Add the root-owned wire-test caller described in
[the shared check](#2026-11-21-phase-732--generated-interface-wire-checks).

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors //bazel/tests/tier4_vehicle_msgs:all
```

The generated-provider check passes all 6 message/Request/Response/Event
wire types in 0.7 seconds (the same grouped CPU/Cyclone DDS gate above),
without failures or skips. The complete expanded plugin-consumer build also
passes. Targeted buildifier and whitespace checks pass; original schemas are
unchanged. Simulator launch behavior and enabled-Agnocast communication are
deferred.

## 2026-11-21: Phase 7.3.2 — Shared RTC coordination library

Purpose: supply the original shared RTC state machine used by selected behavior
modules. Inspect the manifest, public headers, implementation, CMake and the
single original test source. Its one lifecycle case covers registration/removal,
activation and forced activation/deactivation, terminal states, clear/reset and
operator-request guards; do not mistake the single case for a UUID-only test.

Add the package-local module and native library with the local wrapper, QoS,
legacy utility, UUID and tier4_rtc_msgs providers. Compile the original explicit
instantiations for both rclcpp::Node and the Agnocast wrapper node. Export the
flattened shared dependency closure and package-owned ament identity. Expose the
unchanged original test with gtest_main; no added package-local test source,
original implementation edit or CMake change is needed.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors @autoware_rtc_interface//...
```

The complete original suite passes its lifecycle case in 0.1 seconds, zero
failures/disabled cases/skips. This result is repeated in the successful
15-target interface/RTC gate; tier4_rtc_msgs separately round-trips all 18
message/service wire types. Both native node forms compile; runtime execution
is the original rclcpp-node test, CPU/Cyclone DDS with ENABLE_AGNOCAST=0.
Enabled-Agnocast communication, operator integration and full behavior-planner
launch are outside this library gate. Targeted buildifier and whitespace checks
pass. The selected simulator implementation closure remains open.

## 2026-11-21: Phase 7.3.2 — Simulator interface collector and bridge compilation

Purpose: make the selected planning/control/system interface providers available
to root-owned typed consumers without repeating their type lists. Add the seven
new package compile/shared pairs to bazel/interfaces/BUILD.bazel. The existing
Tier IV planning/external-API pairs automatically pick up their expanded
registered surfaces. Leave generic collection and bridge-generation rules
unchanged; the plugin BUILD still takes only all_interfaces.

The collector exposes 339 message/service descriptors, including 49 services.
The existing bridge rule adds each service's synthesized Event pub/sub factory,
so this composition requires 388 typed instantiations. These counts describe
this accepted product set, not a source-completeness requirement. The nine
Tier IV provider surfaces total 89 registered interfaces and 115 generated wire
types; new/expanded provider checks and the standard-service baseline all pass,
alongside the existing payload, namespace/template and RTC checks: 15 test
targets, no failures/skips. Use the commands in the preceding module entries.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=runtime --config=no-agnocast //bazel/agnocast:plugins
```

The full library links successfully at
bazel-bin/bazel/agnocast/libagnocast_bridge_plugins.so, covering all 388 typed
instantiations. The final grouped test/build command also includes this target
and repeats all 15 test targets after the source-export preservation; no failures,
disabled cases or skips occur. Targeted buildifier, Python/flake8, Markdown-anchor
and whitespace checks pass. Original implementation, tests and schema bytes are
unchanged; the planning payload test's library-path change is build support.
Reuse the accepted compile/runtime results for unchanged code. Adding a provider
to the combined C++ context can invalidate prior bridge compilation, so collect
the interfaces required by the current consumers before running this full gate.

This completes the interface/RTC prerequisite group, CPU/Cyclone DDS with
ENABLE_AGNOCAST=0. It does not close the remaining 7.3.2 shared-planner, selected
plugin/controller, perception or system/API implementation closure. No enabled
Agnocast communication matrix, Fast DDS, planning-simulator launch or driving
acceptance is claimed. Keep the stashed shared-planner and map-launch
preparations separate and pause before further Phase 7.3.3 work.

## 2026-11-21: Deployment asset boundary and map provider relocation

Purpose: separate deployment-selectable resource providers from product
composition. With arrows meaning "supplies inputs to", use
(BCR/RCR + third_party/) -> src/ -> bazel/, plus assets/ -> bazel/.
Inspect the current map provider's four files, root MODULE include, archive
BUILD overlay, resource/native consumer tests and packaging/deployment notes.
Keep component-owned resources and existing model repository ownership intact.

Move bazel/maps/ to assets/map/ and retain its git-tracked definitions/provenance.
Give the asset its own module, autoware_planning_simulator_map version 1.0.0,
with a rosdistro dependency. Root selects it using an adjacent bazel_dep with
version 0.0.0 and local_path_override(path = "assets/map") at the end of the local
selection list. Remove the root maps.MODULE.bazel include. The asset's own
MODULE.bazel owns http_archive and selects its BUILD overlay with the
module-relative //:archive.BUILD.bazel label. Its private data repository supplies
the four inputs to the existing register_ament_files rule. Export the conventional
@autoware_planning_simulator_map//:ament_index target; keep all product tests and
scenario composition under bazel/. This module boundary lets the root select an
asset repository without owning that repository's acquisition implementation.

Keep the existing archive SHA-256
5536fce7bb8db7688fdf94ec004118b898637ad0d5b6175108b10989dd6e93b9,
four map payloads, installed share layout and runtime resource package name
unchanged. The apparent data repository name stays planning_simulator_map_data,
but its owner/canonical repository moves inside the asset module. Map bytes
still come from the declared archive; this does not import them into Git.
Update existing map instructions/links to the current module/labels so readers
can apply the asset boundary from the beginning of their own integration.

Add assets/README.md as the authoritative short design sketch: declared sample
resources for development, optional local selections exposed as Bazel inputs,
and backend selection of immutable data/metadata before final release assembly.
Stable adapters can expose data-only snapshots; collaborating repositories may
own their public Bazel targets. Runtime-only assets feed validation/packaging
without becoming compilation inputs. A future deployer would select the exact
software/resource/vehicle-configuration combination, request assembly, verify and
stage artifacts, and coordinate compatible activation/recovery. Distinguish
versioned domain resources, persistent vehicle records and runtime credentials.
No backend, injection rule, production profile or deployment tool is added.
Link the sketch from AGENTS.md, the adoption checklist and developer conventions;
keep the existing map-specific provenance and indexed-consumer instructions.

Reproduce the complete existing map-provider gate:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_planning_simulator_map//:ament_index \
  //bazel/tests/planning_simulator_resources:resources_test \
  //bazel/tests/planning_simulator_resources:route_test
```

The module target builds and all four existing cases pass, without failures or
skips: two Python payload/index-relocation cases in 0.5 seconds and two native
routing/PCL cases in 0.6 seconds. The indexed scenario retains preferred lanelets
9102/9540, and PCL reads all 1757841 points. CPU/Cyclone DDS with ENABLE_AGNOCAST=0
is the selected gate; no Fast DDS check runs. The tests resolve the same resource
package/share paths across the new module boundary.

The archive overlay is byte-identical to its previous content, and the original
archive digest and source URL are retained. Review the small MODULE/BUILD changes
against the existing declaration style; Bazel successfully parses/builds them.
Updated local Markdown links/anchors, active labels and whitespace checks pass.
The external-injection sketch is not an implemented or tested deployment feature;
planning-simulator launch work and stashed preparations remain deferred.

## 2026-11-21: General map asset identity and colcon resource installation

Purpose: give the map a deployment-selectable identity shared by core and
simulator products. Name asset modules/resource packages with the _asset suffix;
rename this module/index from autoware_planning_simulator_map to
autoware_map_asset. Inspect the existing module, archive overlay, resource
registration, four payload hashes and root native/resource consumers. Keep the
archive URL/digest and physical assets/map directory unchanged; rename the
private data repository to map_data and use the package manifest to identify
its ament index. Root retains adjacent version-0.0.0 dependency/local override
selection; consumers use @autoware_map_asset//:ament_index.

Revise the dependency sketch: assets may supply public module/resource APIs to
src/ components as well as bazel/ product composition. Components declare logical
module/package dependencies; root selects the concrete repository/revision.
Components still must not reference metarepository //assets/ or //bazel/ labels.
Asset providers retain generic dependencies and must not depend back on source
implementations or product targets. Product composition remains the primary
consumer. This allows source launchers to find a declared indexed asset rather
than requiring external map_path command-line injection.

Add minimal package.xml/ament_cmake support to the resource repository. CMake
3.24 FetchContent downloads the same SHA-256-pinned archive into its build tree;
install exactly the four unchanged payloads and let ament_package export the
index/config. Update both frontends and provenance together when changing this
accepted dataset. No custom fetch service, generator or host dependency install
is needed. Colcon --symlink-install points the large installed files at the
materialized build-tree data. This is a resource-only interoperability check,
not an Autoware C++ build under a second ROS distribution.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_map_asset//:ament_index \
  //bazel/tests/planning_simulator_resources:resources_test \
  //bazel/tests/planning_simulator_resources:route_test

# In a sourced ROS/colcon environment, use isolated writable build/install roots:
asset_check_root=$(mktemp -d)
colcon --log-base "$asset_check_root/log" build --base-paths assets/map \
  --build-base "$asset_check_root/build" --install-base "$asset_check_root/install" \
  --packages-select autoware_map_asset --symlink-install
source "$asset_check_root/install/setup.bash"
```

All four existing Bazel cases pass: Python payload/relocation (0.6 seconds) and
native route/PCL (0.7 seconds), CPU/Cyclone DDS with ENABLE_AGNOCAST=0, no skips.
The existing ROS Jazzy/colcon environment builds the resource package in
16.9 seconds. After sourcing the generated setup, ament_index_python resolves
the isolated install prefix; all four files are symlinks and match their recorded
SHA-256 values. No apt/pip/rosdep installation occurs. XML syntax and whitespace
checks pass. Indexed launcher consumption belongs to the following task; full
Universe launch, backend injection and Fast DDS are not tested here.

## 2026-11-21: Core launch consumes the indexed map asset

Purpose: make the supported core entrypoint runnable from its declared resource
closure, without caller-supplied map_path. Inspect autoware_core's original
launch, manifest, package-local module/index and the complete root headless
behavior/shutdown test. The accepted gate remains both existing full-core cases;
map delivery, shared vehicle parameters, robot description, velocity relay,
version API, graph endpoints and orderly shutdown must all pass.

Replace the entrypoint's public map_path argument with a private launch let
resolved by $(find-pkg-share autoware_map_asset). Keep the reusable lower-level
map loaders' file arguments and derive their paths inside the entrypoint.
Declare autoware_map_asset in the component MODULE.bazel, ament index deps and
package.xml runtime dependencies. Move the root asset dependency/override pair
immediately before its first source consumer. The source package uses logical
@autoware_map_asset labels, never //assets/ paths or metarepository product rules.
This is the requested indexed-resource behavior, not a compatibility patch.

Remove temporary map construction and map_path injection from the root full-core
test, including the fixture-only runtime edges it no longer needs. Exercise the
same behavior checks against the declared official map. Existing product bundles
inherit the asset through the component's exported index; do not add a separate
packaging file list. Update service/image wrappers and their complete delivery
gates in the following task. The unadopted Universe launch composition remains
its own pending phase; this task validates the supported core entrypoint.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  @autoware_core//... //bazel/tests:core_system_headless_launch_test
```

The module/index build and both original full-core behavior/shutdown cases pass
in the launch target (5.1 seconds), with no failures/skips. The entrypoint receives
no map argument and resolves the indexed asset from its declared closure.
CPU/Cyclone DDS with ENABLE_AGNOCAST=0 is the selected profile; no Fast DDS,
full Universe, GUI or driving result is claimed. Targeted Python/flake8, XML
syntax and whitespace checks pass. Runtime distribution/service checks follow.

## 2026-11-21: Indexed map delivery across core distribution profiles

Purpose: make the supported native, OCI, s6, systemd and application-only OSTree
products carry the map selected through autoware_core's declared asset index.
Prerequisites are the preceding autoware_map_asset and core-launch tasks, the
existing pinned runtime/image/supervisor inputs, and a working Docker Engine for
the external runtime gates. Inspect core_bundle/core_bundle_tests, image payload
assembly, the shared service wrapper, systemd mount/environment declarations,
and every existing delivery test before changing them. The complete gate is
all nine existing distribution test targets below, including their nested core
behavior/shutdown and supervisor readiness/failure/recovery assertions.

Keep packaging driven by the component's transitive ament/runfiles closure:
the asset already reaches both application and verification companion through
autoware_core. Do not add another packaging inventory or copy map fixtures into
the deployed filesystem. Remove map_path injection from the shared service
wrapper and normal OCI entrypoint test; remove temporary map construction and
map bind mounts from OCI/supervisor tests. Drop AUTOWARE_MAP_PATH propagation and
the /maps mount prerequisite from the systemd unit; retain the application
prefix as a required mount. Writable logs/readiness/state mounts remain separate
from immutable application resources.

Extend existing archive checks to require the asset package index and all four
payloads in both application and companion. Extend image structure checks to
require the asset index. The existing exact native-to-image payload comparison
then checks the same declared bytes, modes and links without an independent
map-file mapping. Runtime tests must resolve the asset from the product itself;
the separately mounted verification companion cannot supply the product index.
OSTree reuses this supervisor gate for independently selected application
checkouts, so its update/rollback path must retain the indexed resource closure
as well. This is transferable to other runtime-only asset providers: declare
the index at its actual source consumer, then package the resulting closure.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --nocache_test_results --test_output=errors \
  //bazel/tests:core_bundle_structure_test \
  //bazel/tests:core_image_structure_test \
  //bazel/tests:core_image_payload_test \
  //bazel/tests:core_image_runtime_test \
  //bazel/tests:core_s6_structure_test \
  //bazel/tests:core_s6_runtime_test \
  //bazel/tests:core_systemd_structure_test \
  //bazel/tests:core_systemd_runtime_test \
  //bazel/tests:core_ostree_runtime_test

python3 -m flake8 --max-line-length=150 \
  bazel/tests/runtime/test_bundle.py \
  bazel/tests/runtime/test_image_runtime.py \
  bazel/tests/runtime/test_supervised_image.py
sh -n images/services/core
git diff --check
```

All nine targets pass without failures/skips (668.7 seconds total invocation).
Archive closure passes in 22.9 seconds, OCI payload comparison in 15.3 seconds,
OCI runtime in 50.1 seconds, s6 runtime in 68.5 seconds and systemd runtime in
42.8 seconds; their three image structure targets pass too. The application-only
OSTree update/rollback gate passes in 201.0 seconds, including preserved
synthetic vehicle records. CPU/Cyclone DDS with ENABLE_AGNOCAST=0 is the selected
profile. No runtime map argument, fixture copy or map bind mount is supplied.
Targeted Python style, shell syntax and whitespace checks pass; a blank-line
style correction after the runtime run changes no behavior. Current developer
examples now invoke the products without external map provisioning.

This closes indexed-map delivery for the supported core product, not the
remaining Universe launch/implementation work. Full planning-simulator launch,
GUI/driving acceptance, alternative deployer-supplied assets, bootable OS updates
and Fast DDS are outside this gate. External asset injection remains only the
sketch in assets/README.md.

## 2026-11-21: Phase 7.3.2 — Shared behavior-path planning library

Purpose: supply the common implementation required by the default-selected
Universe behavior-path planner and its scene modules. Inspect the local
autoware_behavior_path_planner_common package manifest, CMake source/test lists,
public headers, and fixture lookups. Its existing dependencies now have local
Bazel providers: RTC, freespace planning, traffic-light utilities, boundary
departure checking, routing, planning factors, marker interfaces and core
geometry/motion utilities. No optional sampling planner is required by this
library; do not adopt default-disabled modules solely to grow the graph.

Add package-owned MODULE.bazel and BUILD.bazel. Build all 18 existing library
translation units and expose the public include tree. Declare the actual
message/library dependencies and collect their shared runtime providers, with
the test-only ament-index/test-utils providers composed separately. Select this
local module with an adjacent version-0.0.0 bazel_dep/local_path_override pair
after its dependencies at the root. No implementation, manifest or original
test source is changed; components acquire no dependency on metarepository labels.
Consumers link @autoware_behavior_path_planner_common and compose its
:runtime_dynamic_deps provider; :ament_index exposes the package/test resources.

Preserve CMake's seven test executable boundaries, covering all 12 original
test source files: utilities (path and traffic-light utilities included),
drivable-area expansion/footprints/static boundaries, occupancy-grid collision
detection, safety/object filtering, parking/departure, path shifting and turn
signals. Google Test's Bazel gtest_main target includes Google Mock; a separate
gmock_main target does not exist in the selected release. Export the original
test_data directory through this package's ament index and declare the
autoware_test_utils index for its map fixtures. Tests consume indexed resources,
not source-workspace paths. Give the tests the declared shared-library directory
and run sequentially on a dedicated ROS domain with Agnocast disabled.
Use the same runtime Python bootstrap configuration as the preceding validated
gates so generator actions can reuse their cached outputs.

The original sources contain two explicitly disabled cases:
DISABLED_StaticDrivableArea.generateDrivableArea and
CalcInterpolatedPoseWithVelocityTest.DISABLED_SpecialCases. The former has an
upstream TODO identifying a failing boundary-intersection assertion. Preserve
their existing status; distinguish these from failures/skips caused by missing
Bazel dependencies. The acceptance gate is every original executable with its
normal upstream test selection, with no extra test filters or omitted groups.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 --test_output=errors \
  @autoware_behavior_path_planner_common//...
```

All 18 library translation units build and all seven test executables pass
(497.9 seconds total invocation). Inspect their test.xml files beneath
bazel-testlogs/external/autoware_behavior_path_planner_common+ to distinguish
executed cases from the two upstream-disabled declarations:

| Original executable group | Active cases passed | Upstream-disabled cases |
| --- | ---: | ---: |
| Utilities | 36 | 0 |
| Drivable-area expansion | 14 | 1 |
| Occupancy-grid collision detection | 6 | 0 |
| Safety/object filtering | 34 | 1 |
| Parking/departure | 7 | 0 |
| Path shifter | 8 | 0 |
| Turn signals | 6 | 0 |
| Total | 111 | 2 |

There are zero failures, errors or runtime skips. CPU/Cyclone DDS with
ENABLE_AGNOCAST=0 is the selected profile. Test runtimes range from 0.5 to
1.7 seconds. No implementation or original-test compatibility fix is needed;
existing header/deprecation warnings are nonfatal. A targeted CMake/Bazel
inventory comparison confirms all 18 library sources, all 12 test sources and
the original seven executable groups are represented. Bazel parses/analyzes
the declarations, and git diff --check passes. No pre-commit, Fast DDS or
unchanged distribution matrix is run.

This completes the common-library task. The behavior-path node and selected
scene plugins remain subsequent Phase 7.3.2 tasks; this library does not establish
full planning-simulator launch or lane-driving acceptance.

## 2026-11-22: Defer the direct Pixi comparison until full Universe integration

Purpose: retain the build-system evaluation discussed alongside AWF's Pixi
report without diverting the ongoing Bazel adoption work. Inspect the current
Phase 7.3 boundaries and later priorities: the simulator-selected Universe
closure is a milestone, not full autoware_universe coverage. Record the user's
prerequisite of full Universe integration before any direct Pixi comparison.

The authoritative scope, comparison criteria and starting references are in
[the deferred comparison plan](PLAN.md#deferred-bazelpixi-comparison). Keep
existing phase numbers and package-level validation gates. No Pixi setup,
benchmark, new packaging pipeline or runtime change is introduced. Future
measurements must distinguish build-system effects from source, toolchain,
dependency and acceptance-scope differences.

Documentation-only validation: git diff --check passes, and the local plan
link/heading is checked. No build, test or behavior suite is rerun. Existing
passing module results remain applicable; no comparison result is claimed.

## 2026-11-22: Phase 7.3.2 — Behavior-path planner node

Purpose: expose the original Universe behavior-path planner as a composable
node and standalone executable, providing its test helpers for selected scene
modules. Prerequisites are the validated behavior-path common library, local
planning-test manager/test-utils, and the existing message/pluginlib/RCLCPP
providers. Inspect package.xml, CMakeLists.txt, both implementation sources,
the test-helper source, all public headers, the original YAML/launch resources
and both CMake-registered test groups before adding rules.

Add package-owned MODULE/BUILD declarations and root local selection after the
common library. Keep planner_manager.cpp and behavior_path_planner_node.cpp in
the production library; expose src/test_utils.cpp and its header as a test-only
library, as with the integrated velocity planner. Register the original
BehaviorPathPlannerNode factory through RCR's rclcpp_components helper, retain
the expected standalone executable name and use a unique component SONAME.
Export original config/launch files, component record and executable through
ament_index. Collect the common library's transitive shared dependencies plus
ament_index_cpp, pluginlib, component support, map messages and logging_demo.

The manifest lists sampling planners, BehaviorTree and OpenCV, but the actual
node/helper translation units and headers do not reference their APIs. Avoid
new otherwise-unused modules or direct links solely for those broad manifest
entries. Existing common providers retain their own actual dependencies. The
full node compilation, shared-library link and original behavior suite validate
this selection. No implementation, manifest or original test source changes.
The diagnostics interface used here is a header template; its existing provider
has no runtime_dynamic_deps label. Its required diagnostic message/updater DSOs
already reach the node through the common boundary-checker closure; inspect
readelf -d on libautoware_behavior_path_planner_component.so to confirm these,
RCLCPP, pluginlib and tinyxml2 are shared dependencies.

Preserve both original executables: utilities compiles test/input.cpp,
test/input.hpp and test/test_utils.cpp; node_interface compiles
test/test_autoware_behavior_path_planner_node_interface.cpp. Supply this
package's ament index plus common/route-handler/test-utils indexes and the
planning-test manager runtime. The six active cases cover path/goal/lanelet
utilities and actual node publication under normal/empty routes and off-track
odometry. No scene plugin is requested by those original node cases. Use
Cyclone DDS, ENABLE_AGNOCAST=0 and sequential tests on ROS domain 70.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @autoware_behavior_path_planner//...
```

All public targets build, including the shared component and generated
standalone executable. Both original test executables pass all six cases with
zero failures, errors, disabled cases or skips (131.5-second invocation;
utilities 0.5 seconds, node interface 9.5 seconds). Test XML confirms four and
two cases respectively. No source compatibility fix is needed. Bazel analysis,
targeted source/test inventory and git diff --check pass. No pre-commit,
Fast DDS or unchanged distribution matrix is run.

The subsequent standalone gate exposes a missing runtime edge: ConfigLogger
service creation aborts because liblogging_demo__rosidl_typesupport_introspection_cpp.so
is absent. The in-process test-helper closure supplied it, hiding the omission.
Export @logging_demo//:cc_shared from the planner's runtime_dynamic_deps and
add its module dependency. The full paired gate reruns both planner suites and
the scene-plugin suites plus the real executable; all 13 cases pass in
62.8 seconds. The following entry records that complete standalone reproducer.
Keep this build-closure correction with the planner definitions; no source fix
or host-runtime fallback is needed.

This completes the planner node's existing suite. The following entry documents
the paired standalone scene-plugin loading/configuration/shutdown gate. Full
selected scene coverage and simulator startup remain pending Phase 7.3.2/7.3.3.

## 2026-11-22: Phase 7.3.2 — Side-shift scene plugin and executable loading

Purpose: validate the first default-enabled behavior-path scene plugin through
its complete original suite and the real standalone planner. Inspect the
side-shift CMake/manifest, three implementation sources, public headers,
plugins.xml, original parameter YAML and both original test files. Reuse the
validated planner/common libraries and original generateNode test helper.
The complete gate is both original side-shift executables (six active cases)
and the added standalone runtime target below, CPU/Cyclone DDS with
ENABLE_AGNOCAST=0 and sequential execution on ROS domain 71.

Add package-local MODULE/BUILD rules and root selection after the planner.
Compile scene.cpp, utils.cpp and manager.cpp into the reusable library, then
build a shared plugin with the original XML basename
libautoware_behavior_path_side_shift_module.so. Export original config files,
the shared library and plugins.xml through ament_index, with plugin category
autoware_behavior_path_planner. The product/test caller composes this index with
the planner's index; the planner module must not depend back on its plugins.
Keep both original tests intact, with the planner test-helper runtime and
indexed test-utils/map resources declared explicitly. No source, original test,
manifest or parameter changes are needed by the initial integration.

Add the standalone adapter under bazel/tests/autoware_behavior_path_planner.
Launch the generated planner executable with its original indexed YAML files
and the side-shift class in launch_modules. Require the successful plugin-load
log and inspect /proc/<pid>/maps for both the planner component and exact plugin
DSOs; path output alone could conceal a plugin that was never registered.
Read the declared planner/plugin parameters through ROS services, change the
plugin's drivable-area margin through the node callback and read it back.
Require successful responses, no unavailable-plugin error and a clean SIGINT
exit. The original C++ suite supplies normal/empty-route, off-track-odometry,
orientation and shift-length behavior coverage; the adapter adds actual
executable/dlopen/configuration/shutdown evidence. This does not claim a full
lateral-maneuver or driving scenario.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @autoware_behavior_path_planner//... \
  @autoware_behavior_path_side_shift_module//... \
  //bazel/tests/autoware_behavior_path_planner:runtime_test
python3 -m flake8 --max-line-length=150 \
  bazel/tests/autoware_behavior_path_planner/test_runtime.py
git diff --check
```

All three original plugin sources build. Both complete original suites pass:
two node-interface cases (9.6 seconds) and four utility cases (0.5 seconds).
The standalone adapter passes its one case in 2.0 seconds after the planner
runtime correction documented above. Both original planner executables also
rerun and pass their six cases. The combined invocation completes in
62.8 seconds: five test targets, 13 cases, no failures, errors, disabled cases
or skips. The plugin is actually registered and loaded into the standalone
planner, with original defaults 300.0/5.0/0.3 and a successful margin update to
0.4; process shutdown exits zero. No source compatibility patch is required.

The targeted CMake/Bazel inventory, Python/flake8 and git diff --check pass.
No pre-commit, Fast DDS, GPU, enabled-Agnocast or unchanged packaging matrix is
run. This completes the side-shift package and paired standalone gate; remaining
selected scene plugins, the full Universe planning closure and original
simulator composition remain pending.

## 2026-11-22: Phase 7.3.2 — Bidirectional-traffic scene plugin

Purpose: add the default-enabled bidirectional-traffic scene plugin to the
existing behavior-path planner. Inspect package.xml, the CMake-recursive source
and test lists, eight implementation files, three original test sources plus
test_helper.hpp, plugins.xml, parameter YAML and indexed test_map/test_data.
The full original suite is one executable with three cases: bidirectional
lanelet discovery, keep-left path generation and give-way behavior.

Use the established package-owned plugin pattern: MODULE/BUILD declarations,
root local override, reusable C++ library, original plugin SONAME and
ament_index plugin category autoware_behavior_path_planner. Preserve all three
CMake-installed resource directories. The map/path fixture helpers resolve
package shares at runtime, so register those files through the owning index
rather than copying fixtures or using source-tree paths. Link the already-local
trajectory/common/geometry/system utilities and their shared runtime providers;
use existing registry fmt/Eigen/Boost/Lanelet providers. No implementation,
manifest, original test or map payload changes are introduced.

Extend the root standalone adapter with a concrete module profile and retain
its side-shift target as a regression check. Each target supplies only its own
plugin index; require the real executable's successful class-load log, exact
plugin/component DSOs, original parameter values and clean SIGINT exit.
BidirectionalTrafficModuleManager::updateModuleParams is an existing empty
implementation. Its adapter checks the original roadside/wait defaults, not a
parameter-store write presented as an implemented behavior update. All original
behavior cases remain required; no runtime test is skipped for dependencies.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @autoware_behavior_path_bidirectional_traffic_module//... \
  //bazel/tests/autoware_behavior_path_planner:bidirectional_traffic_runtime_test \
  //bazel/tests/autoware_behavior_path_planner:runtime_test
```

All eight implementation sources build and all three original cases pass in
1.3 seconds. The isolated commit gate passes three targets/five cases in
35.8 seconds: it reuses the unchanged original-suite result and reruns both
standalone profiles (bidirectional 1.2 seconds, side-shift 1.4 seconds).
The planner reads the original 0.2-metre roadside margin and 15-second wait
setting, loads the exact plugin/component DSOs and exits zero on SIGINT.
There are no failures, errors, disabled cases or runtime skips in this module.
Targeted Python style, source/test inventory and whitespace checks pass.
No implementation or original test changes are needed. Other selected scene
modules and full simulator startup remain pending; no Fast DDS, GPU,
enabled-Agnocast or unchanged distribution matrix is run.

## 2026-11-22: Phase 7.3.2 — Static-obstacle-avoidance scene plugin

Purpose: integrate the default-enabled static-obstacle-avoidance plugin and its
original planning-factor behavior. Inspect CMake/manifest, all five production
sources, public headers, plugin XML, config/test_data and all three original
test sources. The single original test executable combines geometry/object
utilities, normal/empty-route and off-track node cases, and actual predicted
object input with planning-factor publication. Preserve the existing disabled
TestUtils.DISABLED_fillObjectMovingTime case separately from active results.

Apply the same package-owned library/plugin/index pattern as the preceding
scene modules, keeping the original shared-library basename and category.
Export config and test_data through the package's index and provide the local
planner's test helper plus route-handler/test-utils fixture indexes. Reuse the
common library's runtime closure and actual signal-processing, RTC, visualization,
map/TF and sensor-message providers. No new dependency module or source changes
are needed. The test-file glob selects the real .cpp files; this package has no
additional test headers to declare.

The original utility test textually includes ../src/utils.cpp. Its first
sandbox compilation cannot find that undeclared file. Add a test-only cc_library
with textual_hdrs = ["src/utils.cpp"] and make the existing test depend on it.
This declares the include as an input without compiling an extra test object;
the production library retains its original source compilation. Preserve the
test and its relative include. The complete test and real-plugin gates below
validate compilation, linking and execution of that arrangement.

Add its profile to the same root standalone adapter: require actual plugin DLL
loading, original avoidance defaults, a supported max-compensation-time update
and clean exit. Keep the previous profiles as regression checks. Parameter
acceptance/readback is configuration coverage, not proof of a complete obstacle
avoidance maneuver; the original planning-factor case supplies its own behavior
assertions on unsafe factors, control points and object identities.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @autoware_behavior_path_bidirectional_traffic_module//... \
  @autoware_behavior_path_static_obstacle_avoidance_module//... \
  //bazel/tests/autoware_behavior_path_planner:all
python3 -m flake8 --max-line-length=150 \
  bazel/tests/autoware_behavior_path_planner/test_runtime.py
git diff --check
```

All five original implementation sources build. The complete original test
executable passes 21 active cases in 13.9 seconds; its one upstream-disabled
fillObjectMovingTime case retains that status. There are zero failures, errors
or runtime skips. The real standalone plugin check passes in 2.0 seconds,
including loaded DLLs, original defaults 0.3/2.0, a max-compensation-time update
to 2.5 and clean shutdown. The combined gate with bidirectional traffic and the
side-shift regression passes all 27 active cases across five targets in
179.5 seconds, CPU/Cyclone DDS with ENABLE_AGNOCAST=0.

Targeted source/test inventory, Python/flake8 and git diff --check pass. No source
compatibility patch, pre-commit, Fast DDS, GPU, enabled-Agnocast or unchanged
packaging matrix is required/run. This completes this plugin's declared gate;
lane-changing/parking scenes, the remaining planning closure and original
simulator composition remain pending.

## 2026-11-22: Phase 7.3.2 — Sampler-common library

Purpose: supply the local Frenet/lane-change consumers with their actual spline,
trajectory, footprint and constraint implementation. Inspect the manifest,
CMake source-directory registration, all four implementation files/public headers
and both original test files before adding rules. Add a package-owned module,
C++ library and ament index; select it through the root local override after its
existing interpolation/utilities dependencies. Declare real Eigen/Boost/geometry
inputs and the shared ROS/TF utility runtime closure. No source changes or host
package installation are required.

Preserve the original test_sampler_common executable with both test sources.
The source explicitly skips the toFrenet edge-case section after its ordinary
projection assertions, and skips benchFrenet entirely. Retain these upstream
GTEST_SKIP statements; they are not dependency-related skips or newly omitted
test groups. This gate validates supported original behavior, not those skipped
edge cases or a performance benchmark.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @autoware_sampler_common//...
```

All four library sources build and the full original executable passes in
0.3 seconds (41.4-second invocation). XML records five passing cases and the two
explicit upstream skips; no failures/errors or dependency skips. Source/test
inventory and git diff --check pass. CPU/Cyclone DDS with ENABLE_AGNOCAST=0 is
the selected configuration; no pre-commit, Fast DDS or unrelated runtime matrix
is run. Next validate the Frenet library and its lane-change consumers.

## 2026-11-22: Phase 7.3.2 — Frenet planning library

Purpose: provide the actual Frenet implementation referenced by lane-changing.
Inspect both production translation units and all four public headers, CMake
registration and manifest. Add package-local MODULE/BUILD rules and select the
local module after sampler-common. Build both polynomial and planner sources;
link sampler-common, math/legacy geometry helpers, Eigen and actual planning/TF
providers, exporting their shared runtime closure. No host SDK, source patch
or CMake-only facade is needed. CMake registers no native test executable.

Add a native consumer under bazel/tests/autoware_frenet_planner, preserving all
original sources. Check quintic position/velocity/acceleration boundary values
and generate a four-second lane-change trajectory over a straight reference
spline with 0.25-second resolution. Verify 17 samples, Cartesian/Frenet position
agreement, endpoints, finite curvature, aligned output arrays and initial speed
magnitude. This exercises the library API needed by its consumer rather than
merely compiling headers; lane-change package suites provide additional
integration coverage next.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @autoware_frenet_planner//... \
  //bazel/tests/autoware_frenet_planner:consumer_test
```

All package targets build and both consumer cases pass in 0.3 seconds
(33.5-second invocation), with no failures/errors/skips. Original CMake/test
inventory and whitespace checks pass. CPU/Cyclone DDS with ENABLE_AGNOCAST=0
remains selected; no pre-commit, Fast DDS, GPU or unchanged system matrix is
run. The library is ready for lane-change integration; full simulator assembly
remains outside the current execution boundary.

## 2026-11-22: Phase 7.3.2 — Lane-change scene plugins

Purpose: provide both selected left/right lane-change managers through the actual
behavior-path executable. Inspect CMake, manifest, all seven implementation
sources, public headers, plugin XML, config/test_data and all four original test
sources. Preserve the complete single test executable: geometry/acceleration,
node normal/empty-route/off-track, filtered-object/scene and planning-factor
coverage, including its one upstream-disabled filtered-object case.

Use the preceding package-owned library/shared-plugin/index pattern, retaining
both factory classes in one original-basename shared library. Declare the local
Frenet implementation and real map/TF/messages/helpers; expose config/test_data
through the package index and preserve planner test-helper/index dependencies.
Add a root standalone profile loading both factories into one actual planner,
checking defaults 50/200, backward-lane-length update to 201, mapped plugin DLL
and clean shutdown. Do not equate parameter coverage with full driving behavior.

The first compile fails twice at fmt::join in src/interface.cpp. fmt 12.2.0
requires fmt/ranges.h; declaring the fmt dependency is insufficient. Add that
single direct include, matching merged universe#13594 exactly after inspecting
the actual PR diff, current main and Lyrical/toolchain trackers on 2026-11-22.
Keep this source compatibility fix separate from Bazel declarations. Other
packages' related toolchain issues are outside this task.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @autoware_behavior_path_lane_change_module//... \
  //bazel/tests/autoware_behavior_path_planner:all
python3 -m flake8 --max-line-length=150 \
  bazel/tests/autoware_behavior_path_planner/test_runtime.py
git diff --check
```

All seven production sources build. The complete original suite passes eleven
active cases in 17.8 seconds; its one upstream-disabled filtered-object case
remains disabled. All four standalone profiles pass, including both lane-change
factories, the 50/200 defaults, update to 201 and clean exit. The combined
five-target gate passes 15 active cases in 107.3 seconds with no failures,
errors or runtime skips. Targeted flake8 and git diff --check pass.
CPU/Cyclone DDS with ENABLE_AGNOCAST=0 is selected; no pre-commit, Fast DDS or
full simulator run is performed. Next integrate derived avoidance, then parking
scenes.

The source header alignment is committed separately as 4120f86e. Package-owned
Bazel definitions and the root two-factory profile form the following module
commit, using the same passing gate above without rerunning unchanged actions.

## 2026-11-22: Phase 7.3.2 — Avoidance-by-lane-change scene plugin

Purpose: expose the preset-selected avoidance manager using its local lane-change
and static-avoidance implementations. Inspect all three production sources,
four src-local headers, factory XML, configuration, manifest/CMake and the complete
original two-case node-interface suite. Both tests send normal routes plus
empty-route or off-track odometry and require planner output.

Add the package-owned library/plugin/index and root local override after both
base modules. Its headers live under src, so declare them and that include root
instead of inventing an include directory. Link actual base libraries and their
runtime closures. Both the original tests and standalone profile require all
three indexed YAML providers: lane_change, static_obstacle_avoidance and
avoidance_by_lane_change. Transitive C++ dependencies do not substitute for
these runtime resources. Extend the root adapter with that resource list;
require successful factory/DLL loading, own longitudinal-margin default 80,
inherited backward-lane-length default 200/update to 201 and clean shutdown.
No original implementation or test changes are required.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @autoware_behavior_path_avoidance_by_lane_change_module//... \
  //bazel/tests/autoware_behavior_path_planner:all
python3 -m flake8 --max-line-length=150 \
  bazel/tests/autoware_behavior_path_planner/test_runtime.py
git diff --check
```

All three implementation sources and package targets build. Both original cases
pass in 9.6 seconds, the new standalone profile in 2.0 seconds and the four prior
profiles pass. The complete six-target gate passes seven cases in 102.4 seconds
with no failures, errors, disabled cases or skips. Targeted flake8 and whitespace
checks pass. Configuration is CPU/Cyclone DDS, ENABLE_AGNOCAST=0; no pre-commit,
Fast DDS or unrelated deployment matrix runs. Parking scenes and jointly loading
all selected scenes remain to validate before simulator composition.

## 2026-11-22: Phase 7.3.2 — Bézier sampler library

Purpose: supply the local goal planner with its actual quintic Bézier sampler.
Inspect both production sources, both public headers and manifest/CMake. CMake
registers no native tests. Add package-owned module/library/index, declaring
sampler-common and Eigen and exposing sampler-common's runtime closure; select
the local override after that dependency. Keep implementation unchanged.

Add a root native consumer testing actual curve construction from boundary
states, endpoint position/velocity/zero-acceleration, agreement between scalar
and matrix evaluation, nine Cartesian/heading samples and finite curvature.
A second case exercises the sampling API's eight-candidate family and verifies
all endpoint positions/headings. These cover the API required by goal planning;
they do not certify arbitrary driving or optimization quality.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @autoware_bezier_sampler//... \
  //bazel/tests/autoware_bezier_sampler:consumer_test
git diff --check
```

Both production sources and all package targets build; both consumer cases pass
in 0.3 seconds (26.8-second invocation), zero failures/errors/skips. Source/test
inventory and whitespace checks pass. CPU/Cyclone DDS, ENABLE_AGNOCAST=0 is
selected; no source patch, pre-commit, Fast DDS or unrelated matrix is needed.
Next validate the goal-planner consumer together with the selected parking scenes.

## 2026-11-22: Phase 7.3.2 — Start-planner scene plugin

Purpose: supply the selected parking pull-out manager. Inspect the nine production
sources, public headers, factory/config/test_data, manifest/CMake and all six
original test/helper translation units. Preserve the complete original single
test executable: clothoid, shift and geometric pull-out generation plus the
start-prevention planning-factor case. The original freespace pull-out test is
explicitly disabled upstream; keep that state visible. Several extra route
fixtures are commented out in the original cases and are not newly enabled.

Add package-local module/library/shared-plugin/index rules and the root local
override. Declare the actual freespace implementation, planner common helpers,
map/TF/messages and utilities. The existing Lanelet2 extension exports distinct
CcInfo and shared-library targets: use autoware_lanelet2_extension_lib_library
for compile dependencies, retaining its shared closure through planner common.
The test helper lives under test/src with a test/include header; compile it once
with the complete original test glob and declare that include directory. Link
the pinned pyplot/Python embedding API rather than host Python. Preserve CMake's
EXPORT_TEST_PLOT_FIGURE=OFF default; optional figure export is not an enabled
test group or an acceptance claim here.

Extend the root standalone profile to load StartPlannerModuleManager through the
actual planner executable/index. Require its DLL mapping, arrival/stopped-time
defaults 1/1, supported stopped-time update to 1.5 and clean shutdown. Keep the
five earlier profiles as regressions. No original source/test edits are needed.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @autoware_behavior_path_start_planner_module//... \
  //bazel/tests/autoware_behavior_path_planner:all
python3 -m flake8 --max-line-length=150 \
  bazel/tests/autoware_behavior_path_planner/test_runtime.py
git diff --check
```

All nine production and six original test/helper sources build. The original
suite passes four active cases in 7.6 seconds and retains its one disabled case;
standalone start-planner passes in 2.1 seconds and all five prior profiles pass.
The combined gate has ten active cases across seven targets, zero failures,
errors or runtime skips (243.3-second invocation). The final declaration-only
cleanup gate also passes, reusing unchanged profile results and rerunning the
original suite (7.8 seconds) and start profile (1.4 seconds). CPU/Cyclone DDS with
ENABLE_AGNOCAST=0 is selected; no pre-commit, Fast DDS or unchanged packaging
matrix runs. Goal-planner and joint scene loading are the next parking gates.

## 2026-11-22: Phase 7.3.2 — Goal-planner scene plugin

Purpose: provide the preset-selected goal/parking manager with local Bézier,
freespace and trajectory implementations. Inspect all thirteen production
sources/public headers, factory XML/config/test_data, CMake/manifest and all
three original test sources. Preserve all seventeen registered cases, including
two cases in the original disabled map-utility fixture. Active coverage includes
lane-change state transitions, map/boundary geometry, sharp-curvature lateral
acceleration and the blocked-goal planning factor.

Add the package-owned module/library/shared-plugin/index and root local override
after its sampler dependencies. Reuse planner common and the actual Bézier,
freespace and trajectory runtime closures. The original tests need their own,
planner/common, route-handler and test-utils indexes plus the planner's existing
test helper. Expose original config/test_data and factory basename. Add a root
standalone profile for GoalPlannerModuleManager, indexed defaults 1/2, stopped-
time update to 2.5, actual DLL mapping and clean shutdown; retain prior profiles.

The first compile reports five missing std::execution::par declarations. The
pinned libc++ 21 headers gate PSTL behind experimental support. Inspect its
execution/__algorithm/pstl.h and generated backend configuration plus LLVM's
[documented opt-in](https://libcxx.llvm.org/UserDocumentation.html#enabling-experimental-c-library-features).
Use package-local copts = ["-fexperimental-library"]. This exposes the original
stable_sort execution-policy overloads without replacing algorithms or modifying
source. The configured LLVM backend is serial; no parallel-speedup claim follows
from successful policy compilation. Current upstream main retains the same five
calls; PR searches and the Lyrical/toolchain trackers reviewed on 2026-11-22 show
no matching source fix to adopt. A build configuration choice needs no upstream
source patch. Keep the flag scoped to this consumer rather than changing the
whole tree's experimental ABI surface.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @autoware_behavior_path_goal_planner_module//... \
  //bazel/tests/autoware_behavior_path_planner:all
python3 -m flake8 --max-line-length=150 \
  bazel/tests/autoware_behavior_path_planner/test_runtime.py
git diff --check
```

All thirteen production sources and three original test sources build unchanged.
The complete original suite passes fifteen active cases in 10.4 seconds and
retains two disabled cases. Goal-planner standalone passes in 1.8 seconds; the
initial combined eight-target gate passes 22 active cases in 204.8 seconds.

Extend the same root adapter to load all eight preset-selected factories across
seven scene packages in one actual planner. Require every successful factory
load, indexed configuration/parameter update, no unavailable-plugin error and
clean shutdown. Its first DLL assertion wrongly requires a separate base DLL:
derived avoidance statically includes lane-change factories, and pluginlib can
supply them from that already-loaded library. A trial explicit base-shared link
also encounters duplicated private common-library ownership. Retain the existing
valid static composition; independent profiles prove each package's own DLL,
while the joint profile checks the actual eight-factory lifecycle. Do not infer
a source/runtime bug from an overly strict mapping assertion or force an ABI
redesign without a behavioral need. Final nine-target gate passes 23 active
cases, including the joint profile (1.4 seconds), with no failures/errors/runtime
skips; unchanged native results are reused. Flake8 and whitespace checks pass.
CPU/Cyclone DDS with ENABLE_AGNOCAST=0 is selected. No pre-commit, Fast DDS,
full simulator assembly or unchanged deployment matrix is run. Selected behavior-
path scenes are now available; behavior/motion velocity and controllers follow.

## 2026-11-22: Phase 7.3.2 — Behavior-velocity RTC scene support

Purpose: provide the shared approval/safety interface needed by the selected
behavior-velocity plugins. Inspect both legacy and experimental implementation
files/public headers and manifest/CMake. CMake registers no native tests. Add a
package-owned module/library/index and root local override, declaring the actual
planner-common, RTC, planning-factor, message and utility inputs. Export the
existing shared runtime providers; neither a host package nor a source patch is
needed. Both implementations build.

Add a root native consumer deriving the legacy scene API. Check module identity,
initial inactive/unsafe state, automatic activation following safety when RTC
is off, independent safety/approval with RTC on, explicit activation, distance
and returning to unsafe. Initialize the RTC-enabled flag before exercising that
API. This is a scene-state consumer, not a vehicle-level approval test; selected
plugin suites and experimental executable checks follow.

Reuse the existing root planner lifecycle adapter for subsequent plugin packages:
make the module an explicit argument, derive its factory/configuration names,
and export the source from its own root test package. Existing standard and
experimental stop-line gates retain their factory, DLL, parameter checks,
unload/reload and clean-exit assertions. Future package test declarations can
reference that exported adapter with their own indexes; source components do
not acquire root labels. Preserve the stop-line-specific duration assertion.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @autoware_behavior_velocity_rtc_interface//... \
  //bazel/tests/autoware_behavior_velocity_rtc_interface:consumer_test \
  //bazel/tests:behavior_velocity_planner_standard_runtime_test \
  //bazel/tests:behavior_velocity_planner_experimental_runtime_test
python3 -m flake8 --max-line-length=150 \
  bazel/tests/behavior_velocity_planner/test_runtime.py
clang-format --dry-run --Werror \
  bazel/tests/autoware_behavior_velocity_rtc_interface/consumer_test.cpp
git diff --check
```

All package targets and both implementations build. The native state consumer
passes in 0.6 seconds; standard/experimental stop-line lifecycles pass in 1.4/1.5
seconds. The final three-target invocation passes three cases in 40.3 seconds,
with no failures/errors/skips. Targeted formatting, flake8 and whitespace checks
pass. CPU/Cyclone DDS with ENABLE_AGNOCAST=0 is selected; no pre-commit, Fast DDS
or full simulator launch runs. Next integrate selected traffic-light, detection,
virtual-light and no-stopping-area consumers, preserving their complete suites.

## 2026-11-22: Phase 7.3.2 — Traffic-light behavior-velocity plugin

Purpose: provide the preset-selected traffic-light plugin. Inspect all six production sources, src-local headers,
factory XML/config, manifest/CMake and every original test source. Preserve the
complete original executable, covering yellow transitions, arrow/turn-lane decisions, stop-point geometry and normal/empty/off-track node input.

Apply package-owned module/library/shared-plugin/index declarations and the root
local override after RTC/common dependencies. Export both standard and
experimental factories under the original library basename. Declare actual local
helpers and message/TF providers, shared runtime closures, src include roots and
all original fixtures. Only declare header/source globs that exist; Bazel 9
rejects each unmatched pattern even when another pattern matches. No original
implementation/test edits, missing-test exclusions or host installations are
needed. Reuse the root lifecycle adapter from the RTC step with this package
index. Check factory/DLL loading, indexed configuration, planner parameters,
unload/reload and clean shutdown through both real executables. The launch
selects the experimental factory; the standard check also preserves the API
exercised by the original node suite.

Run the four related package gates together, with heavy actions/tests serialized.
The targets for each package can also be selected independently. The exact
combined invocation used for this entry and the following three entries is:

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors \
  @autoware_behavior_velocity_traffic_light_module//... \
  @autoware_behavior_velocity_detection_area_module//... \
  @autoware_behavior_velocity_virtual_traffic_light_module//... \
  @autoware_behavior_velocity_no_stopping_area_module//... \
  //bazel/tests/autoware_behavior_velocity_traffic_light_module:all \
  //bazel/tests/autoware_behavior_velocity_detection_area_module:all \
  //bazel/tests/autoware_behavior_velocity_virtual_traffic_light_module:all \
  //bazel/tests/autoware_behavior_velocity_no_stopping_area_module:all
python3 -m flake8 --max-line-length=150 \
  bazel/tests/behavior_velocity_planner/test_runtime.py
git diff --check
```

All six production sources and the complete original suite build. All 13
original cases pass in 10.2 seconds; standard/experimental lifecycle checks
pass in 1.7/2.0 seconds. There are no failures, errors, disabled cases or
skips. The whole four-package group passes 47 cases across twelve targets in
423.6 seconds. Targeted flake8 and whitespace checks pass. Configuration is
CPU/Cyclone DDS with ENABLE_AGNOCAST=0; no pre-commit, Fast DDS or full simulator
launch is run. Remaining selected scenes and motion/controller dependencies
remain separate gates.

## 2026-11-22: Phase 7.3.2 — Detection-area behavior-velocity plugin

Purpose: provide the preset-selected detection-area plugin. Inspect all six production sources, src-local headers,
factory XML/config, manifest/CMake and every original test source. Preserve the
complete original executable, covering stop-line/obstacle geometry, stop-state clearing, braking distance and normal/empty/off-track node input.

Apply package-owned module/library/shared-plugin/index declarations and the root
local override after RTC/common dependencies. Export both standard and
experimental factories under the original library basename. Declare actual local
helpers and message/TF providers, shared runtime closures, src include roots and
all original fixtures. Only declare header/source globs that exist; Bazel 9
rejects each unmatched pattern even when another pattern matches. No original
implementation/test edits, missing-test exclusions or host installations are
needed. Reuse the root lifecycle adapter from the RTC step with this package
index. Check factory/DLL loading, indexed configuration, planner parameters,
unload/reload and clean shutdown through both real executables. The launch
selects the experimental factory; the standard check also preserves the API
exercised by the original node suite.

Verification uses the exact sequential group invocation in the
[traffic-light entry](#2026-11-22-phase-732--traffic-light-behavior-velocity-plugin),
including these package-specific targets:

```text
@autoware_behavior_velocity_detection_area_module//...
//bazel/tests/autoware_behavior_velocity_detection_area_module:all
```

All six production sources and the complete original suite build. All 6
original cases pass in 10.1 seconds; standard/experimental lifecycle checks
pass in 1.7/1.7 seconds. There are no failures, errors, disabled cases or
skips. The whole four-package group passes 47 cases across twelve targets in
423.6 seconds. Targeted flake8 and whitespace checks pass. Configuration is
CPU/Cyclone DDS with ENABLE_AGNOCAST=0; no pre-commit, Fast DDS or full simulator
launch is run. Remaining selected scenes and motion/controller dependencies
remain separate gates.

## 2026-11-22: Phase 7.3.2 — Virtual-traffic-light behavior-velocity plugin

Purpose: provide the preset-selected virtual-traffic-light plugin. Inspect all six production sources, src-local headers,
factory XML/config, manifest/CMake and every original test source. Preserve the
complete original executable, covering V2X conversion, head poses, collision-index handling, stop-velocity insertion and normal/empty/off-track node input.

Apply package-owned module/library/shared-plugin/index declarations and the root
local override after RTC/common dependencies. Export both standard and
experimental factories under the original library basename. Declare actual local
helpers and message/TF providers, shared runtime closures, src include roots and
all original fixtures. Only declare header/source globs that exist; Bazel 9
rejects each unmatched pattern even when another pattern matches. No original
implementation/test edits, missing-test exclusions or host installations are
needed. Reuse the root lifecycle adapter from the RTC step with this package
index. Check factory/DLL loading, indexed configuration, planner parameters,
unload/reload and clean shutdown through both real executables. The launch
selects the experimental factory; the standard check also preserves the API
exercised by the original node suite.

Use the already-local tier4_v2x_msgs provider for V2X messages, including its
shared typesupport; do not substitute registry sources for this local closure.

Verification uses the exact sequential group invocation in the
[traffic-light entry](#2026-11-22-phase-732--traffic-light-behavior-velocity-plugin),
including these package-specific targets:

```text
@autoware_behavior_velocity_virtual_traffic_light_module//...
//bazel/tests/autoware_behavior_velocity_virtual_traffic_light_module:all
```

All six production sources and the complete original suite build. All 11
original cases pass in 10.7 seconds; standard/experimental lifecycle checks
pass in 1.6/1.7 seconds. There are no failures, errors, disabled cases or
skips. The whole four-package group passes 47 cases across twelve targets in
423.6 seconds. Targeted flake8 and whitespace checks pass. Configuration is
CPU/Cyclone DDS with ENABLE_AGNOCAST=0; no pre-commit, Fast DDS or full simulator
launch is run. Remaining selected scenes and motion/controller dependencies
remain separate gates.

## 2026-11-22: Phase 7.3.2 — No-stopping-area behavior-velocity plugin

Purpose: provide the preset-selected no-stopping-area plugin. Inspect all six production sources, src-local headers,
factory XML/config, manifest/CMake and every original test source. Preserve the
complete original executable, covering stuck-object selection, stop-line/polygon geometry, stopping feasibility and normal/empty/off-track node input.

Apply package-owned module/library/shared-plugin/index declarations and the root
local override after RTC/common dependencies. Export both standard and
experimental factories under the original library basename. Declare actual local
helpers and message/TF providers, shared runtime closures, src include roots and
all original fixtures. Only declare header/source globs that exist; Bazel 9
rejects each unmatched pattern even when another pattern matches. No original
implementation/test edits, missing-test exclusions or host installations are
needed. Reuse the root lifecycle adapter from the RTC step with this package
index. Check factory/DLL loading, indexed configuration, planner parameters,
unload/reload and clean shutdown through both real executables. The launch
selects the experimental factory; the standard check also preserves the API
exercised by the original node suite.

Verification uses the exact sequential group invocation in the
[traffic-light entry](#2026-11-22-phase-732--traffic-light-behavior-velocity-plugin),
including these package-specific targets:

```text
@autoware_behavior_velocity_no_stopping_area_module//...
//bazel/tests/autoware_behavior_velocity_no_stopping_area_module:all
```

All six production sources and the complete original suite build. All 9
original cases pass in 10.1 seconds; standard/experimental lifecycle checks
pass in 1.8/1.7 seconds. There are no failures, errors, disabled cases or
skips. The whole four-package group passes 47 cases across twelve targets in
423.6 seconds. Targeted flake8 and whitespace checks pass. Configuration is
CPU/Cyclone DDS with ENABLE_AGNOCAST=0; no pre-commit, Fast DDS or full simulator
launch is run. Remaining selected scenes and motion/controller dependencies
remain separate gates.

## 2026-11-22: Phase 7.3.2 — Grid-map dependency provider

Purpose: prepare the actual grid-map libraries needed by crosswalk and its local
iterator helper. No grid-map source snapshot exists locally. The RCR metadata
URLs for grid_map_core, grid_map_cv, grid_map_ros and grid_map_msgs return HTTP
404 on 2026-11-22. Inspect upstream ROS 2 tag 2.5.0 and its immutable commit
0ece1b53ca03199527d0d42913cf2567d0d6a913, manifests/CMake, source/header layouts,
message/service declarations, all original test registrations, Eigen extras and
ROS bag fixtures. The archive SHA-256 is
c621063445ee59e73495528ee3a00022f435accc20cd5c0cf2a494e998d9b82b.

Keep reusable acquisition/build support under third_party/grid_map. Root selects
this module through a local override; component modules use its public providers.
Apply one BUILD overlay inside the immutable source archive for the nineteen
core/CV/ROS implementation sources. Use explicit package/export identities for
multiple ROS packages in that repository, including both messages and all four
services, and preserve the original shared-library basenames. Root pins required
registry inputs: filters 2.2.2-3.rcr.2, nav2_msgs 1.5.2-2.rcr.0 and SQLite storage
0.33.4-1.rcr.0. Do not replace these sources with host dependency installation.

Inspect the CMake-only helper's effects before omitting it as a module. Native
C++ selection belongs to the root; the core Eigen extras additionally export
EIGEN_DENSEBASE_PLUGIN and EIGEN_FUNCTORS_PLUGIN. Preserve both on the owning
C++ provider so consumers compile the same Eigen extensions. The initial
defines lose unescaped include-file quotes during Bazel tokenization, producing
expected-FILENAME errors in Eigen. Preserve escaped quotes in the defines
values; do not change the SDK headers. Declare archive
format tar.gz explicitly because the codeload URL ends in a hash.

Preserve all twenty original test translation units across three suites. Core
and CV use their original test mains; a generic dependency-owned adapter runs
the ROS test executable with writable copies of both declared SQLite bag
fixtures and the actual storage plugin/index. Resolve runtime paths before
changing to a temporary directory and forward GTest XML. Native tests using
inline plugin/OpenCV APIs initially fail to link through a raw shared-library
provider alone. Publish per-library collect_transitive_dynamic_deps aggregates
that flatten the full declared dynamic closure, and use them for binary links;
do not rely on indirect ELF dependencies satisfying direct references. Add an indexed original
CV filter factory check; do not modify upstream tests merely to replace their
expected working directory. No suite is excluded because of setup work.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @grid_map//:tests
python3 -m flake8 --max-line-length=120 third_party/grid_map/test_ros.py
clang-format --dry-run --Werror third_party/grid_map/filter_factory_test.cpp
git diff --check
```

GridMapMsgHelpers initially cannot find boost/assign.hpp: add its real
Boost.Assign header dependency rather than patching the existing include. All
ROS conversion cases then run, but four bag cases throw because the converter
factory cannot find package rosbag2_cpp. A shared-library link and SQLite index
do not supply that package identity; include the actual rosbag2_cpp package
provider in the ROS runtime index. Retain both writable fixture bags and the
pinned SQLite provider. All four bag cases then pass unchanged.

All nineteen implementation sources and all twenty original test translation
units build. The full gate passes 109 cases across four targets: 91 core, six
CV, eleven ROS and one filter-factory case. There are zero failures, errors,
disabled cases or skips. The final invocation takes 7.5 seconds, reusing unchanged
core/CV/factory results after the bag resource fix; the initial registry interface
closure required a cold build. Targeted flake8, clang-format and whitespace
checks pass. Source compatibility patches and host packages are unnecessary.
Scope is these four source packages; other upstream grid-map packages have no
Bazel declaration here. CPU/Cyclone DDS with ENABLE_AGNOCAST=0 is selected; no
pre-commit, Fast DDS or enabled-Agnocast/kernel runtime gate is run.

Next expose the SDK's concrete interface targets to the root collection and
validate descriptor/template/plugin compilation, then the local polygon-iterator
helper and selected crosswalk consumer. SDK task success is the complete
four-target gate above, independently of later product composition.

## 2026-11-22: Phase 7.3.2 — Grid-map interfaces in product composition

Purpose: make the SDK's declared messages/services available through the same
concrete root interface set consumed by bridges and other product-level tools.
The prerequisite grid-map package gate passes all 109 cases. Add one
ros_interface_package entry using the public SDK message cc/cc_shared providers,
without repeating message lists in bridge rules or introducing source-to-root
labels. Preserve explicit grid_map_msgs ROS identity inside the grid_map Bazel
module. Extend the existing descriptor test to check the six canonical SDK
identities and reject the Bazel module name as a ROS package namespace. This
checks declared provider metadata, not source-directory completeness.

The plugin library currently compiles every generated source against one
aggregate CcInfo. Adding a package changes that common include/dependency
context, so existing plugin translation units rebuild even when their generated
text is unchanged. Keep that cost distinct from a communication-runtime matrix;
changes to compilation granularity are a later optimization, not part of this
provider adoption.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors //bazel/interfaces:descriptor_test \
  //bazel/agnocast:template_tests //bazel/agnocast:plugins
```

The manifest contains 345 canonical interface descriptors, including all six
SDK identities under grid_map_msgs. The full libagnocast_bridge_plugins.so builds
with 398 typed instantiations (345 pubsub/event and 53 service sources, plus its
generic implementation). Full compilation takes 1140.6 seconds; a final
2.2-second invocation reuses that build and passes the updated descriptor test
(two unittest cases) plus the unchanged template regression. Bazel's fallback
XML records each Python/shell invocation as one case. Flake8 and whitespace
checks pass. The AWF finding also receives its existing local source-fix commit
reference, 4120f86e; that documentation correction reuses the lane-change gate.

This is CPU/Cyclone DDS with ENABLE_AGNOCAST=0. Kernel-dependent bridge runtime
communication suites are outside this package/provider change; their unchanged
communication behavior is not a new acceptance claim. No Fast DDS, pre-commit
or full simulator launch is run. Next validate the local iterator and remaining
selected scene plugins.

## 2026-11-22: Phase 7.3.2 — Local grid-map polygon iterator

Purpose: provide crosswalk planning with the local scan-line iterator rather
than a registry substitute. Inspect its one production source/header, manifest,
CMake, all seven original polygon cases and original benchmark. Select the local
module after the validated grid-map SDK. Production code uses only the core
provider; keep the legacy stopwatch dependency in its test/benchmark consumers.
Declare the original benchmark with its actual CV/OpenCV providers and build it,
but do not make a long performance/optional interactive experiment the native
correctness gate. Its --visualize mode is not exercised here.

The first libc++ 21/C++23 compile rejects std::sort's comparison of const Edge
operands: the header's operator< lacks a const qualifier. Add that qualifier,
preserving the same read-only descending endpoint ordering. This is a source
compatibility fix, separate from MODULE/BUILD declarations. Inspect AWF's Lyrical
and toolchain trackers, current upstream header and repository PR/issue searches
for grid_map_utils, PolygonIterator and const on 2026-11-22. Current main still
has the non-const member; no matching fix is found. Record the finding in
UPSTREAM_AWF.md without associating unrelated feature/performance work.

Add package-owned module/library/index, the complete original GTest executable
and the original benchmark target. SDK Eigen hooks and shared providers flow
through their declared core/CV dependencies. No new test body, host library,
source relocation or disabled test is needed.

```sh
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @autoware_grid_map_utils//...
git diff --check
```

The production iterator, original benchmark and complete seven-case suite build.
All seven cases pass in 0.1 seconds (22.6-second invocation): full cover, outside,
square/triangle coverage, moved maps, differential comparison and self-crossing
polygons. There are no failures/errors/skips/disabled cases. Whitespace checks
pass; the one-keyword source edit retains existing formatting. CPU/Cyclone DDS
with ENABLE_AGNOCAST=0 is selected. No pre-commit, Fast DDS, performance timing
claim or full simulator launch is included. Next validate crosswalk and the
remaining selected behavior-velocity scene group.

The source const-correctness fix is 73b37641. The following module commit adds
its already-validated build rules and root selection using the same passing
gate; no source/test behavior changes follow that result.

## 2026-11-22: Phase 7.3.2 — Intersection UUID construction compatibility

Purpose: unblock the selected intersection implementation with Boost.UUID 1.90,
LLVM/libc++ 21.1.8, C++23 and RCR Lyrical on Ubuntu 24.04. The first real module
compile rejects the old sixteen-element aggregate initialization: modern uuid
has constructors and no such sixteen-argument overload. Inspect the UUID hash
specialization and its unordered-map consumers, AWF's Lyrical/toolchain trackers,
current main and matching PR #13605. Main retains the old initializer; the PR is
open at c4e36a66e05eef83f000a4d28e59c9cab648c71d, reviewed 2026-11-22.

Align the intersection portion exactly: include algorithm, value-initialize the
UUID and std::copy all sixteen message bytes into its iterator range. Preserve
the hash function, byte order and keys. Do not apply the PR's optional roundabout
change before that module enters the selected graph. Commit this source fix
separately from the following package-owned build definitions. An added root
regression constructs equal UUID copies and sixteen one-byte variants, then
checks independent unordered-map insertion/lookup; it belongs to the module's
build commit. No original test bodies change.

Verification uses the exact combined scene gate in the following crosswalk
entry, including the intersection original executable, UUID regression and both
real planner lifecycles. All pass. Targeted clang-format and git diff --check
also pass. Older Boost distributions are not exercised. This is alignment with
an existing upstream PR, not a new submission; UPSTREAM_AWF.md records the
association. The next commit supplies the reusable local module declarations.
