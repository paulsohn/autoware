# Bazel-native Autoware: plan

## Goals
- Reproducible and traceable builds: every input pinned and recorded in git.
- Build caching (Bazel disk cache now, remote cache once CI exists).
- One image family from a single base, for local development, CI and the vehicle.

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
"core" pulls in all of `src/core/` (msgs, adapi/internal msgs, utils, lanelet2_extension, autoware_cmake)
plus `velodyne_description`. `autoware_behavior_velocity_template_module` is a universe test dependency: drop or stub it.

- Probably in the RCR (verify against `ros-*.txt`): rclcpp(+components, lifecycle), tf2*, message_filters,
  diagnostic_updater, pluginlib, class_loader, cv_bridge, pcl_conversions, launch*, rviz2,
  robot_state_publisher, xacro, Eigen, PCL, Boost, fmt, yaml-cpp.
- Private registry: lanelet2 (core/io/projection/routing/traffic_rules), grid_map_core,
  generate_parameter_library, geographiclib, osqp, proxsuite, pcl_ros, point_cloud_msg_wrapper,
  range-v3, tl_expected, glog, nlohmann_json, rclpy_message_converter, topic_tools, pybind11, CUDA/TensorRT.

## Phases
| Phase | Work | Done when |
|---|---|---|
| P0 | Bazel 9 + RCR on x86 24.04; check the hermetic toolchain's glibc target (must be <= 2.39); `rclcpp` hello node; `dev` image on `ubuntu:24.04` | Hello node runs on the host and in the container |
| P1 | Rule library, BUILD generator; core msgs + `autoware_utils_*` | All core msgs and utils build |
| P2 | Registry modules (scope 1); `autoware_core`; Jazzy->Lyrical source fixes | `autoware_core` tests pass |
| P3 | `rules_cuda` x86 + SBSA, TensorRT; `autoware_cuda_utils`, `cuda_blackboard`, `negotiated` | CUDA tests pass on x86 |
| P4 | `runtime` image via `rules_oci`; planning simulator via core launch | Demo runs from the `runtime` image |
| P5 | Thor (`--config=thor`) | Same tests pass on Thor |
| P6+ | Universe, one domain per phase (sensing -> perception -> planning -> ...) | One running pipeline per domain |

## Risks
- The RCR is a proof of concept (Bazel 9 only, tested on Ubuntu 26.04 only). Fallback: a 26.04 build container that targets glibc 2.39.
- Components and pluginlib need shared libraries; static/portable builds may not work.
- Cold builds are slow (Qt, PCL from source): keep the disk cache mounted from day one.

## Progress log
- 2026-11-08: Imported all 36 repos into `src/` with `git subtree --squash` (versions from
  `repositories/*.repos` at commit a7397a7). Added this plan and `CLAUDE.md`. Next: P0.
