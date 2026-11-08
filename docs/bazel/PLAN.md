# Bazel-native Autoware: plan

## Goals
- Reproducible and traceable builds: every input pinned and recorded in git.
- Build caching (Bazel disk cache now, remote cache once CI exists).
- Developer, CI and vehicle environments may differ, but the differences must not matter: the same commit gives the same artifacts, and the runtime image is identical by digest (see "Environments").

## Decisions (and why)
| Decision | Why |
|---|---|
| ROS 2 Lyrical from the RCR, built hermetically by Bazel. No apt or colcon Lyrical install | The RCR only covers Lyrical. Its build is hermetic (its own LLVM and Python), so the host OS only has to provide glibc |
| Host and base image: Ubuntu 24.04 | Matches the dev machine and JetPack 7 on Thor. glibc 2.39 |
| Monorepo, one `MODULE.bazel`; Autoware packages are plain Bazel packages | No upstream sync, so per-package modules buy nothing |
| `src/` imported with `git subtree --squash` | Small repo. Each import commit records the upstream SHA |
| Bazel owns everything that gets linked. No apt, rosdep or Pixi for build deps | Avoids untracked inputs and ABI clashes |
| Private registry in-repo (`registry/`, `file://`) listed ahead of bazel.ros.org and BCR | Third-party modules the RCR lacks. Registry and code changes share one history |
| CUDA 13 via `rules_cuda` hermetic redist (`linux-x86_64`, `linux-sbsa`); TensorRT/cuDNN as sha256-pinned tarballs | Thor (JetPack 7) is SBSA, so no L4T-specific libraries are needed at build time. Only the driver comes from the host |
| Runtime image built by Bazel (`rules_oci`), not by `docker build` | git SHA leads straight to the image digest; no Docker daemon needed |
| Deferred: remote cache server, signing, SBOM, vendoring, multi-arch CI | Personal PoC. These are hardening work for later |
| `autoware_cmake` is not ported. Its `autoware_package()` flags become `.bazelrc` / macro defaults: `-std=c++17 -Wall -Wextra -Wpedantic -Werror`, `-DBOOST_ALLOW_DEPRECATED_HEADERS -DEIGEN_DONT_PARALLELIZE`, Clang `-Wno-gnu-anonymous-struct -Wno-nested-anon-types` | It only sets compile options and calls ament_cmake_auto. `ROS_DISTRO_*` defines are only checked for GALACTIC/HUMBLE, so Lyrical takes the default branch with no define |
| `autoware_system_designer` (and its only consumer `autoware_sample_designs`) removed. Leftovers are dropped, not ported: any `autoware_system_designer_*` CMake call (none in the 2026-11-09 snapshot; the BUILD generator must skip them if they appear), `design/*.{node,module,system,parameter_set}.yaml` files (318, inert data, not built), and the `lint-system-design-format` pre-commit hooks / CI workflows inside subtrees | Orthogonal to the Bazel integration |
| ML models: pinned in `models.lock.json` (HF repo, tag, commit SHA, per-file sha256), fetched by Bazel `http_file` from `huggingface.co/<repo>/resolve/<sha>/<path>`. No git submodule, LFS, Xet client or DVC | See "ML models" below |

## Target layout
```
MODULE.bazel  MODULE.bazel.lock  .bazelversion  .bazelrc
bazel/rules/       # autoware_package(), gen_param_library(), pluginlib_export(), xacro, launch
bazel/platforms/   # x86_64, thor
bazel/tools/gen_build/  # package.xml + CMakeLists.txt -> BUILD.bazel generator (output committed)
registry/          # private Bazel registry
images/            # base/dev Dockerfiles + rules_oci runtime image
src/
```

## Rules needed (colcon/CMake feature -> Bazel)
- `autoware_package()` / `ament_auto_*`: macro producing `cc_library` + `cc_shared_library` + `ament_index` + `ament_package` (RCR rules).
- `rclcpp_components_register_node`: shared library plus the ament index resource. Needs `--dynamic_mode` on.
- `pluginlib`: plugin XML and the ament index entry.
- `generate_parameter_library`: codegen rule (`py_binary` + `genrule`).
- msgs/srv/action: RCR `ros_interface` + `cc_ros_library` / `py_ros_library`.
- xacro: `genrule`. Launch, config and map files: `ament_index` data, then `ros2 launch` from the `:tar` install tree.
- Lint stays out of the build (pre-commit / clang-tidy).

## Scope 1 dependencies (core/ + core/cuda)
"core" pulls in all of `src/core/` (msgs, adapi/internal msgs, utils, lanelet2_extension)
plus `velodyne_description`. `autoware_behavior_velocity_template_module` is a universe test dependency: drop or stub it.

- Probably in the RCR (verify against `ros-*.txt`): rclcpp(+components, lifecycle), tf2*, message_filters,
  diagnostic_updater, pluginlib, class_loader, cv_bridge, pcl_conversions, launch*, rviz2,
  robot_state_publisher, xacro, Eigen, PCL, Boost, fmt, yaml-cpp.
- Private registry: lanelet2 (core/io/projection/routing/traffic_rules), grid_map_core,
  generate_parameter_library, geographiclib, osqp, proxsuite, pcl_ros, point_cloud_msg_wrapper,
  range-v3, tl_expected, glog, nlohmann_json, rclpy_message_converter, topic_tools, pybind11, CUDA/TensorRT.

## ML models
Source: 21 AutowareFoundation HF repos listed in `ansible/roles/artifacts/tasks/main.yaml`, pinned by **tag**. Tags can move, so the lock records the commit SHA.

Verified 2026-11-09 against the HF API (lidar_centerpoint v4.1 -> `494c8171…`):
- `GET /api/models/<repo>/revision/<tag>` returns the commit `sha`.
- `GET /api/models/<repo>/tree/<sha>?recursive=true&expand=true` returns per-file `size`, plus `lfs.oid` (= sha256 of the content) and `xetHash` for large files. Small files (yaml, README) have no hash, so the lock script downloads and hashes them.
- `resolve/<sha>/<path>` serves Xet-stored files over plain HTTPS (302 to the xet-bridge CDN, `x-linked-etag` = sha256). No git, git-lfs or git-xet client is needed.

Design:
- `bazel/tools/lock_models.py` (the only code that talks to the HF API) turns `{repo, tag, dest}` entries into `models.lock.json`.
- A module extension creates one external repo per (repo, revision), with `http_file`s and a `filegroup`. Only models a built target uses get fetched; Bazel's repository cache stores them by content and checks the sha256.
- Each HF repo becomes two external repos, split by whether the API reports an `lfs` entry: `@<model>_config` (non-LFS: `*.param.yaml`, class remappers, a few KB) and `@<model>` (LFS weights, `*.onnx`). CI tests (e.g. validating param files against the node's `schema/*.schema.json`) depend only on `_config`, so weights are never downloaded. A target that needs undeclared weights fails at build time. (Submodule equivalent: `GIT_LFS_SKIP_SMUDGE=1`, but it leaves pointer files at the weights' paths, which fails silently.)
- Several revisions of the same repo (diffusion_planner v3.0–v5.0) are just separate entries.
- Models go into their own `rules_oci` layer at the `dest` path the launch files expect.
- Gated/private repos later: `.netrc` or `--credential_helper` for huggingface.co.

Alternative (documented, not chosen): one git submodule per HF repo, pinned to the commit, files fetched with git-lfs.
- Equivalent on integrity and traceability: the superproject pins the commit, the commit pins the LFS pointer (which holds the sha256). Files are ordinary source inputs to Bazel, so action caching works the same.
- Simpler: no lock script, no module extension. Keeping only the latest version avoids multiple submodules per repo.
- Why `http_file` instead: submodules download everything up front, per clone (each worktree, CI runner and image build fetches again unless a shared `lfs.storage` is set up), need git-lfs on every machine, and get no help from Bazel's shared repository cache, `bazel vendor`, `--distdir` or `--remote_downloader`. `http_file` downloads lazily, only for targets being built.
- Switching later is cheap in either direction: both pin the same HF commit SHA.

Rejected: DVC needs its own storage remote and duplicates HF's versioning.

## Environments
| | Developer (local) | CI | Vehicle |
|---|---|---|---|
| Where the build runs | Host (Ubuntu 24.04) | `dev` image (pinned `ubuntu:24.04` + bazelisk + git) | Not built there; receives the runtime image |
| Toolchain, ROS, third-party libs | Bazel (hermetic, `MODULE.bazel.lock`) | Same | Same artifacts |
| Shell/coreutils seen by build actions | Host's (drifts with `apt upgrade`) | Pinned by image digest | n/a |
| Bazel sandbox | `linux-sandbox` | Likely `processwrapper-sandbox` (no user namespaces in Docker) | n/a |
| Where things run | `bazel run` / `bazel test` on the host | In the runtime image | Runtime image (`rules_oci`, base = pinned `ubuntu:24.04`) |
| From the machine, never from Bazel | GPU driver, Mesa/GL, kernel, devices, sysctl/DDS tuning, agnocast kmod | Same | Same (JetPack 7 on Thor) |

Why the differences must not matter:
- Build outputs depend only on Bazel-declared inputs. Host-only inputs (shell, coreutils) must not change them, and this is checked by comparing digests (below).
- Runtime: the runtime image is the single runtime environment for integration tests and the vehicle, identical by digest. Host `bazel run` is for fast iteration only.
- What comes from the machine is listed above and kept to the driver/kernel boundary. Versions tested against are recorded in the progress log.
- x86 and Thor cannot be byte-identical (different architecture). There, the guarantee is the same commit, the same pinned inputs and the same base image digest, compared by test results.

Checks (exit criteria in Phases): host vs container build digests match (P0, P2); planning simulator passes in the runtime image on the dev host (P4); the same tests pass on Thor (P5).

## Phases
| Phase | Work | Done when |
|---|---|---|
| P0 | Bazel 9 + RCR on x86 24.04; check the hermetic toolchain's glibc target (must be <= 2.39); `rclcpp` hello node; `dev` image on `ubuntu:24.04` | Hello node runs on the host and in the container; talker sha256 is identical whether built on the host or in the `dev` container |
| P1 | Rule library, BUILD generator; core msgs + `autoware_utils_*` | All core msgs and utils build |
| P2 | Registry modules (scope 1); `autoware_core`; Jazzy->Lyrical source fixes | `autoware_core` tests pass; host vs container output digests match |
| P3 | `rules_cuda` x86 + SBSA, TensorRT; `autoware_cuda_utils`, `cuda_blackboard`, `negotiated` | CUDA tests pass on x86 |
| P4 | `runtime` image via `rules_oci`; planning simulator via core launch | Demo runs from the `runtime` image |
| P5 | Thor (`--config=thor`) | Same tests pass on Thor |
| P6+ | Universe, one domain per phase (sensing -> perception -> planning -> ...) | One running pipeline per domain |

## Risks
- The RCR is a proof of concept (Bazel 9 only, tested on Ubuntu 26.04 only). Fallback: a 26.04 build container that targets glibc 2.39.
- Components and pluginlib need shared libraries; static/portable builds may not work.
- Cold builds are slow (Qt, PCL from source): keep the disk cache mounted from day one.

## Progress log
See [JOURNAL.md](JOURNAL.md): steps taken, commands, issues and lessons, in order.
