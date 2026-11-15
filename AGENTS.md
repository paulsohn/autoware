# Autoware + Bazel integration experiment

This branch is a personal experiment that integrates Autoware with Bazel on top
of the ROS Central Registry (RCR). It does not track the upstream Autoware build
system and must not change `main`.

Before working, read `docs/bazel/PLAN.md`, then the latest entries in
`docs/bazel/JOURNAL.md`. Use `docs/bazel/DEVELOPING.md` only for steady-state
development of targets that already have Bazel support. Adoption sequencing,
experiments, and milestones belong in PLAN.md and JOURNAL.md.

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
- Define integration tasks at module/package scope and identify the complete
  existing test suite and required behavior checks before implementation.
  Declare a module task successful only after that whole suite passes in the
  required configurations. Prepare dependencies and runtime prerequisites
  needed by those tests; dependency-related skips, omitted test groups, and
  failures deferred to another session do not satisfy the completion gate.
  Keep working through in-scope failures. If an external prerequisite prevents
  completion, report the task as incomplete with the evidence and required
  next action; do not narrow the acceptance scope to claim success.
- Treat versions, flags, and patches as hypotheses until a command in this
  branch validates them. Pin accepted inputs in git and record why in PLAN.md.
- Develop and validate Lyrical compatibility independently. When a current
  target needs a source fix, inspect AWF's Lyrical trackers and related PRs,
  compare their actual scope, and align compatible fixes where practical.
  Record local commit/PR associations and verification dates in
  `docs/bazel/UPSTREAM_AWF.md`; distinguish matching fixes from related work.
  Do not pre-apply unrelated changes or treat an upstream PR as local test
  evidence.
- Write each JOURNAL.md entry so a reader without this branch can repeat the
  experiment in their own repository. Record the purpose, prerequisites and
  inspected inputs, minimum integration pattern and rationale, transferable
  know-how and decisions, exact verification commands and observable results,
  diagnosed failures as symptom/cause/fix, and remaining limitations or next
  step. Do not rely on the diff alone to explain how the integration works.
- Keep one journal entry per meaningful module task. Consolidate routine
  reruns into the final result and explain consequential failures through
  their symptom, cause, and fix. Update the same entry while the task remains
  incomplete; do not create a milestone for each test invocation or label an
  incomplete module integration as successful.
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
