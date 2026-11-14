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
  bazel build //bazel/smoke:talker //bazel/smoke:listener
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
  //bazel/smoke:talker //bazel/smoke:listener
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
  bazel build //bazel/smoke:talker //bazel/smoke:listener
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=fastdds \
  //bazel/smoke:talker //bazel/smoke:listener
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
  //bazel/smoke:talker //bazel/smoke:listener
docker run --rm \
  -v autoware-bazel-bootstrap-cache:/home/ubuntu/.cache \
  -v "$PWD:/workspace:ro" \
  autoware-bazel-dev:bootstrap \
  bazel build --config=fastdds --experimental_convenience_symlinks=ignore \
  //bazel/smoke:talker //bazel/smoke:listener
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
  bazel test //bazel/smoke:autoware_common_msgs_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds //bazel/smoke:autoware_common_msgs_test
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
  //bazel/smoke:autoware_common_msgs_test
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
`//bazel/smoke:autoware_common_msgs_test` unchanged:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --lockfile_mode=off \
  //bazel/smoke:autoware_common_msgs_test
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
  //bazel/smoke:autoware_common_msgs_test \
  //bazel/smoke:autoware_localization_msgs_test
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds \
  //bazel/smoke:autoware_common_msgs_test \
  //bazel/smoke:autoware_localization_msgs_test
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
  //bazel/smoke:autoware_common_msgs_test \
  //bazel/smoke:autoware_localization_msgs_test
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
  bazel test //bazel/smoke:autoware_interface_layer_test
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
  //bazel/smoke:autoware_interface_layer_test
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
  --nocache_test_results //bazel/smoke:autoware_interface_layer_test
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
  bazel test //bazel/smoke:autoware_interface_base_test
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
  //bazel/smoke:autoware_interface_base_test
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
  //bazel/smoke:autoware_interface_dependent_test
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
  //bazel/smoke:autoware_interface_dependent_test
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
  //bazel/smoke:autoware_common_msgs_test \
  //bazel/smoke:autoware_localization_msgs_test \
  //bazel/smoke:autoware_interface_layer_test \
  //bazel/smoke:autoware_interface_base_test \
  //bazel/smoke:autoware_interface_dependent_test
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
  //bazel/smoke:autoware_common_msgs_test \
  //bazel/smoke:autoware_localization_msgs_test \
  //bazel/smoke:autoware_interface_layer_test \
  //bazel/smoke:autoware_interface_base_test \
  //bazel/smoke:autoware_interface_dependent_test
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
