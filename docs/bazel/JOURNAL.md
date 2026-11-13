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
