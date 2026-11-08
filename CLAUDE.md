# Autoware Bazel PoC (personal fork)

Personal proof of concept: build Autoware natively with Bazel on top of the
ROS Central Registry (RCR, https://bazel.ros.org, ROS 2 Lyrical).
This fork has diverged from autowarefoundation/autoware and does not sync either way.

**Read [docs/bazel/PLAN.md](docs/bazel/PLAN.md) first** (goals, decisions and why), then the end of
[docs/bazel/JOURNAL.md](docs/bazel/JOURNAL.md) (what was done last). The journal must let someone
repeat the integration on another ROS project: as you work, append steps with exact commands,
results, and every issue as symptom → cause → fix, plus lessons that generalize.
Record new decisions in PLAN.md.

## Layout
- `src/`: all source, imported with `git subtree add --squash`. It is ordinary
  tracked code now: edit it in place. Each import commit message records the upstream
  URL and SHA (`chore(src): import <path> <version>`).
- `repositories/*.repos`, `docker/`, `ansible/`: legacy colcon/apt tooling, kept only
  as reference until the Bazel replacement works. Do not extend them.
  Exception: `ansible/playbooks/install_nvidia.yaml` and `install_docker.yaml` for host setup.

## Conventions
- Branch: `bazel` (push directly to remote `paulsohn`). `main` tracks upstream and is not used.
- Commits: Conventional Commits (`feat(bazel): ...`, `fix(src/autoware_core): ...`), with `git commit -s`.
  Keep `src/` source edits (Lyrical porting fixes) in separate commits from Bazel build-file additions.
- Never run `vcs import` / `vcs pull` into `src/`.
- Local development builds on the host; CI builds in `images/dev.Dockerfile`; integration tests and the vehicle use the
  Bazel-built runtime image. The differences must not affect outputs (see "Environments" in PLAN.md).
- Never run `bazel` from a shell that sourced `/opt/ros/*/setup.bash` (the host has Jazzy installed).
  Safe form: `env -i HOME=$HOME PATH=$HOME/.local/bin:/usr/bin:/bin bazel ...`.
- Machine-specific Bazel flags (e.g. `--jobs`, memory limits) go in the gitignored `user.bazelrc`.
- Pinned versions live in git: `.bazelversion`, `MODULE.bazel.lock`, image digests.
- Target: x86_64 Ubuntu 24.04 host first, then Jetson Thor (JetPack 7, Ubuntu 24.04, SBSA).
