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
- Import model repositories as squashed git subtrees under `src/models/`,
  preserving their existing model-data-directory-relative subpaths where
  practical. Each model repository owns one Bazel module, one ament package
  index and its model/metadata targets; keep MODULE.bazel, BUILD.bazel,
  package.xml and minimal colcon/ament_cmake build support inside that boundary.
  Do not replace these indexes with a metarepository-owned aggregate package.
  For diffusion_planner, import only the current autoware_launch-selected
  revision into `src/models/diffusion_planner`, with no version subdirectory.
  Record immutable upstream provenance and validate LFS/Xet materialization.
- Update every affected autoware_launch model-path parameter together with its
  provider. Resolve model paths exclusively through each repository's indexed
  resources; remove the common model `data_path` argument and its propagation.
  Do not retain HOME/external-directory fallbacks or inject absolute workspace
  paths. Colcon users must obtain and build the required model packages and
  source the resulting environment; Ansible downloads into HOME alone no longer
  satisfy launcher dependencies. Validate per-repository lookup, dependency
  declarations and colcon --symlink-install behavior, including flat diffusion.
- `repositories/`, `docker/`, and `ansible/` are reference implementations for
  the existing build and deployment. Do not extend them for Bazel integration.
- With arrows meaning "supplies inputs to", keep dependency direction
  `(BCR/RCR modules + third_party/)` -> `src/` -> `bazel/`, plus
  `assets/` -> `bazel/`. Deployment-selectable resource providers and metadata
  belong under `assets/`; source-owned resources remain with their components.
  Keep the current `assets/map/` integration git-tracked as its own module,
  owning acquisition and its resource index; root selects its local override.
  External asset
  injection and deployer responsibilities remain a sketch in `assets/README.md`;
  do not introduce a backend or provisioning framework without a concrete task.
  `bazel/` owns product composition, standalone integration tests and interface
  collections; it may also consume third-party providers directly. `src/`
  components own their build definitions. Reusable SDK/dependency definitions,
  generic dependency helpers and dependency patches live under `third_party/`
  and must not depend on Autoware components or product labels.
  Asset providers likewise must not depend on application implementations or
  product targets; generic registry/third-party rules may support them.
  The root owns version selection from `third_party/`, registry releases and
  local overrides; architecture/product settings also remain root-owned.
  Directory placement alone does not require a new facade Bazel module.
- Keep container definitions under `images/` and integration documentation under
  `docs/bazel/`.
- Keep interface collection and product-level plugin assembly under `bazel/`.
  Defer a separate `autoware_bazel` source repository until a concrete
  cross-repository need requires it. Keep component-specific build support with
  its component; components must not depend on metarepository paths or labels.
  The metarepository selects source overrides and composes interface sets
  usable by bridges, loggers, or other consumers.
- Keep existing source packages unchanged except for Bazel definitions/build
  support. Modify existing implementation or original tests only when a
  demonstrated issue blocks Bazel integration/testing (including ROS/toolchain
  compatibility) or the implementation is genuinely incorrect; document and
  validate the fix separately.
- Put newly added Autoware-specific integration tests, adapters and fixtures
  under `bazel/` (normally `bazel/tests/<package>/`), not inside original source packages.
  Keep original upstream tests in place and expose them through package-local
  Bazel rules. Compose added tests at the root without component-to-root labels.
  Generic dependency test/runtime helpers belong with their third-party owner.
- BUILD declarations define supported interfaces; registering interfaces and
  their dependencies is the developer's responsibility. Undeclared interface
  sources remain outside the Bazel graph. Validate generated targets and
  consumers rather than enforcing source-directory completeness.

## Working rules

- Work directly on branch `bazel` and push validated commits to remote
  `paulsohn`.
- Commit each validated module/package boundary separately, including its
  relevant journal and documentation updates. A phase may contain several
  commits; do not accumulate independent integrations into one phase commit.
- Use signed-off Conventional Commits. Keep source compatibility fixes in
  separate `fix(src/<repository>): ...` commits from Bazel build definitions.
- Preface every Bazel dependency patch file with its affected module/version,
  observed failure, rationale and scope, verification, and upstream status or
  reference before the diff. Keep verification claims current.
- Prefer source snapshots under `src/` whenever a required dependency is present
  locally. Missing Bazel definitions are integration work: add package-local
  MODULE/BUILD declarations transitively for required local dependencies,
  including message packages, and select them with root local_path_override.
  Do not substitute an older registry source because it already has Bazel
  support. Use registry sources for dependencies absent locally; use a registry
  counterpart for a local package only when the user explicitly requests a
  source-selection comparison. Validate the affected local dependency closure.
- For CMake-only dependency shims or metapackages, experiment with direct
  Bazel toolchain/library providers before adding an otherwise empty module.
  Omit the shim only after inspecting its exported effects and validating the
  complete affected consumer suites; preserve required flags, definitions and
  runtime resources in the owning Bazel targets. Journal the evidence. This
  does not permit replacing locally available implementation or interface code.
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
- Keep validation proportional to the affected module and behavior. Routine
  work builds affected targets and runs the changed module's complete suite
  with Cyclone DDS plus relevant behavior checks. Full system communication
  matrices are for communication-layer changes or explicitly scoped milestones,
  not automatic requirements for every commit or phase. Select the required
  middleware configurations in advance and record exactly what passed.
  Reuse passing results for unchanged code/configuration; documentation-only
  updates do not require runtime reruns. Preserve the complete module gate.
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
- When an experiment establishes a generally applicable convention, update the
  [adoption checklist](docs/bazel/PLAN.md#before-starting-your-integration) and
  its linked rule/evidence in the same commit. Keep the checklist concise;
  link to the authoritative description rather than duplicate it.
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
- Use Cyclone DDS for routine validation. Reserve Fast DDS checks for major
  milestones, and always obtain the user's approval before running any Fast
  DDS build, test, or behavior check. Earlier results do not authorize a rerun.
- Add a finding to `UPSTREAM_RCR.md` or `UPSTREAM_AWF.md` only after the
  experiment supplies reproducible evidence and the finding is useful outside
  this repository.
