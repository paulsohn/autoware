# Autoware + Bazel integration experiment

This branch is a personal experiment that integrates Autoware with Bazel on top
of the ROS Central Registry (RCR). It does not track the upstream Autoware build
system and must not change `main`.

Before working, read `docs/bazel/PLAN.md`, then the latest entries in
`docs/bazel/JOURNAL.md`. Use `docs/bazel/DEVELOPING.md` for commands and local
configuration.

## Repository layout

- `src/` contains source snapshots imported as squashed git subtrees. Edit these
  files in place. Do not run `vcs import` or `vcs pull` into `src/`.
- `repositories/`, `docker/`, and `ansible/` are reference implementations for
  the existing build and deployment. Do not extend them for Bazel integration.
- Put Bazel workspace support under `bazel/`, container definitions under
  `images/`, and integration documentation under `docs/bazel/`.

## Working rules

- Work directly on branch `bazel` and push validated commits to remote
  `paulsohn`.
- Use signed-off Conventional Commits. Keep source compatibility fixes in
  separate `fix(src/<repository>): ...` commits from Bazel build definitions.
- Introduce only what the current experiment requires. Investigate a dependency
  or patch when a current target exposes the need; do not pre-apply anticipated
  fixes.
- Treat versions, flags, and patches as hypotheses until a command in this
  branch validates them. Pin accepted inputs in git and record why in PLAN.md.
- Append exact commands and results to JOURNAL.md after each task. Record issues
  as symptom, cause, and fix once the cause is demonstrated.
- Keep developer-specific resource limits and paths in ignored
  `user.bazelrc`. Do not commit convenience symlinks or Bazel output trees.
- Run build, test, and behavior checks appropriate to each commit. State
  explicitly when a documentation-only commit has no runtime validation.
- Do not run pre-commit for this experiment. Run only targeted format and
  static checks against files changed by the current task.
- Run heavy builds sequentially and respect the resource limits in
  `user.bazelrc`.
- Add a finding to `UPSTREAM_RCR.md` or `UPSTREAM_AWF.md` only after the
  experiment supplies reproducible evidence and the finding is useful outside
  this repository.
