# Autoware Bazel PoC (personal fork)

Personal proof of concept: build Autoware natively with Bazel on top of the
ROS Central Registry (RCR, https://bazel.ros.org, ROS 2 Lyrical).
This fork has diverged from autowarefoundation/autoware and does not sync either way.

**Read [docs/bazel/PLAN.md](docs/bazel/PLAN.md) first**: it holds the plan, the
decisions taken and why, and the progress log. Update its progress log at the end
of every session.

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
- Never run `bazel` from a shell that sourced `/opt/ros/*/setup.bash` (the host has Jazzy installed).
- Pinned versions live in git: `.bazelversion`, `MODULE.bazel.lock`, image digests.
- Target: x86_64 Ubuntu 24.04 host first, then Jetson Thor (JetPack 7, Ubuntu 24.04, SBSA).
