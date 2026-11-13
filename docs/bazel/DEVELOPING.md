# Developing the Bazel integration

## Read first

Read `AGENTS.md`, `PLAN.md`, and the latest `JOURNAL.md` entry before changing
the integration. The plan defines the current experiment; the journal records
what the repository has actually demonstrated.

## Local configuration

Use Ubuntu 24.04 x86_64 for the first phase. Install Bazelisk as `bazel`; the
repository's `.bazelversion` will select Bazel after bootstrap files are added.

Put machine-specific limits in `user.bazelrc`, which is ignored. For example:

```text
build --jobs=6
build --local_resources=cpu=6
```

Choose limits appropriate to the machine. Run only one heavy Bazel build at a
time.

## Clean environment

Until the workspace proves that it removes inherited ROS variables itself, run
Bazel without a sourced ROS environment:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" bazel <command>
```

Do not run `vcs import` or `vcs pull` into `src/`.

## Task workflow

1. State the target and its observable success condition in JOURNAL.md.
2. Make the smallest change that tests the current question.
3. Run the relevant build and test commands.
4. For executable targets, verify behavior rather than relying only on a build.
5. Record exact commands and results. For a failure, separate symptom, confirmed
   cause, and fix.
6. Update PLAN.md if the result establishes or reverses a durable decision.
7. Add upstream notes only when the evidence is reproducible and useful beyond
   this branch.
8. Run `git diff --check`, review the staged diff, and commit with sign-off.

## Commit boundaries

- Bazel rules, BUILD files, workspace configuration, and integration docs use
  scopes such as `feat(bazel)`, `fix(bazel)`, or `docs(bazel)`.
- Compatibility changes under an imported repository use
  `fix(src/<repository>)` and remain separate from BUILD-file commits.
- Do not combine unrelated package ports to reduce commit count.

## Validation language

Report only commands run against the current commit. Distinguish build success,
test success, runtime behavior, and artifact comparison; one does not imply the
others. Documentation-only changes should say that no build was run.
