# Autoware + Bazel integration journal

## 2026-11-13: Define scope and validation gates

Goal: build Autoware core and CUDA packages with pinned Bazel inputs on
Ubuntu 24.04 x86_64, then run a headless planning simulator in an OCI image.

Changes: added PLAN.md with package layout, toolchain choices, runtime packaging,
and validation gates. Development uses local branch `bazel`, with direct pushes
to `paulsohn/bazel` and signed-off Conventional Commits.

Validation: `git diff --check` passed. This change contains documentation only;
no build or runtime experiments have been performed.

Next: scope cleanup, pinned Bazel workspace and talker/listener smoke checks.
