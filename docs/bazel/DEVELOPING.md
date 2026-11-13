# Developing the Bazel integration

## Read first

Read `AGENTS.md`, `PLAN.md`, and the latest `JOURNAL.md` entry before changing
the integration. The plan defines the current experiment; the journal records
what the repository has actually demonstrated.

## Local configuration

Use Ubuntu 24.04 x86_64 for the first phase. Install Bazelisk as `bazel`; the
repository's `.bazelversion` selects the tested Bazel release.

Put machine-specific limits in `user.bazelrc`, which is ignored. For example:

```text
build --jobs=6
build --local_resources=cpu=6
```

Choose limits appropriate to the machine. Run only one heavy Bazel build at a
time.

## Clean environment

Run Bazel without a sourced ROS environment when validating integration work:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" bazel <command>
```

Do not run `vcs import` or `vcs pull` into `src/`.

## Bootstrap smoke test

Cyclone DDS is the default. Build the publisher and subscriber:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build //bazel/smoke:talker //bazel/smoke:listener
```

Run the listener in one terminal and the talker in another:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel run //bazel/smoke:listener
```

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel run //bazel/smoke:talker
```

The listener exits successfully after receiving five messages.

No Cyclone DDS XML or host network tuning is required for this loopback smoke
test. Add a network configuration only when the topology under test requires
one.

Run the Tier 2 Fast DDS compatibility gate by adding `--config=fastdds` to
every command:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel build --config=fastdds \
  //bazel/smoke:talker //bazel/smoke:listener
```

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel run --config=fastdds //bazel/smoke:listener
```

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel run --config=fastdds //bazel/smoke:talker
```

## Development image

Build the pinned Ubuntu 24.04 development image:

```bash
docker build -f images/dev.Dockerfile -t autoware-bazel-dev:bootstrap .
```

Use a named volume for Bazel's cache and mount the source read-only:

```bash
docker volume create autoware-bazel-bootstrap-cache
docker run --rm \
  -v autoware-bazel-bootstrap-cache:/home/ubuntu/.cache \
  -v "$PWD:/workspace:ro" \
  autoware-bazel-dev:bootstrap \
  bazel build --experimental_convenience_symlinks=ignore \
  //bazel/smoke:talker //bazel/smoke:listener
```

Add `--config=fastdds` to the container's Bazel command for the compatibility
build.

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
