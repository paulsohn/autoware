# Autoware + Bazel integration journal

## 2026-11-13: Define scope and validation gates

Goal: define the experiment's initial scope on Ubuntu 24.04 x86_64, with a
headless planning simulator in an OCI image as the first end-to-end milestone.

Changes: added PLAN.md with goals and validation gates. Development uses local
branch `bazel`, with direct pushes to `paulsohn/bazel` and signed-off
Conventional Commits.

Validation: `git diff --check` passed. This change contains documentation only;
no build or runtime experiments have been performed.

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

Next: add `autoware_system_msgs`, the smallest remaining interface package and
the next package whose only Autoware dependency is already integrated. Repeat
the read-only development-image test when the Docker daemon is available.
