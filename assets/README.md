# Deployment-selectable assets

This is a PoC convention and a deployment design sketch. The map integration
under [map/](map/README.md) stays git-tracked as its own Bazel module. It owns
the resource index, archive pin and BUILD overlay; the root selects it with
`bazel_dep` and `local_path_override`. Its provenance identifies the existing
checksum-pinned archive. Map payloads are fetched by Bazel;
this move does not vendor them into Git. No deployer backend or injection
mechanism is implemented here.

Arrows below mean "supplies inputs to":

```text
BCR/RCR modules + third_party/ -> src/ -> bazel/
assets/ -> src/
assets/ --------------------------------> bazel/
```

`assets/` owns deployment-selectable resource inputs and metadata. `bazel/`
composes software and selected resources, runs integration checks and assembles
deployable artifacts. Generic registry/third-party build rules can serve both.
Keep source-owned defaults, meshes and original fixtures with their components;
existing model repositories retain their `src/models/` ownership. Asset
providers must not depend on application implementations or product targets.
Name asset modules/resource packages with the `_asset` suffix; the current map
provider is `autoware_map_asset`. Components under `src/` may consume its public
module targets and ament index, declaring that dependency in their own module
and package metadata. Use logical names, not metarepository `//assets/` paths.
The root selects the concrete repository/revision; `bazel/` remains the primary
product/validation/packaging consumer.

## Proposed selection and packaging

- Development products select declared sample resources, so a fresh checkout
  needs no personal provisioning. Tests use the same indexed layout as releases.
  A developer's optional local resource selection belongs in ignored
  `user.bazelrc`, with actual files exposed as declared Bazel inputs.
- A deployment backend would select software, domain assets and vehicle
  configuration before invoking the final Bazel release-assembly target.
  Runtime-only data should enter validation/packaging targets; changing a map
  should reuse unchanged compiled software. Resource preprocessing, when needed,
  must also declare its inputs in the graph.
- For backend-supplied data, prefer the selected snapshot plus a manifest of
  version/digest, layout and compatibility metadata. Keep stable Bazel adapters
  here, rather than requiring each deployment to supply build rules. Repositories
  that own useful Bazel targets can instead expose their pinned public targets.
- Bazel would emit the final archive/image/application payload with the selected
  resources and their ament indexes. Reuse the runtime providers and packaging
  mappings; avoid a separate deployer-maintained list of files to copy.
- Separate asset updates could later use Bazel-built resource packages with the
  same indexed layout and compatibility contract. Bytes supplied after a software
  build are runtime inputs; they are not retroactive dependencies of that build.

## Proposed deployer responsibility

Select and record the exact software/resource/configuration combination; request
assembly from the build backend; verify and stage the resulting artifacts; check
vehicle assignment and compatibility; then coordinate activation, readiness and
recovery. Product launchers would establish the declared resource prefixes and
resolve package shares without workspace or HOME lookups.

Maps and domain parameters can be versioned resources. Vehicle identity and
calibration require vehicle-bound revision and persistence policies. Keep device
private keys and hardware-backed credentials in protected runtime provisioning;
only contracts or synthetic fixtures belong in ordinary build inputs. Software
rollback must preserve vehicle records and apply explicit configuration/state
compatibility policy.

This sketch does not add backend services, production asset profiles, credential
handling, deployment tooling or an OTA implementation. See the existing
[deployment requirements](../docs/bazel/PLAN.md#runtime-packaging-and-deployment-requirements)
for the deferred deployment milestones.
