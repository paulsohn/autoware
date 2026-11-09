# Developing with Bazel

Day-to-day commands for this repository. Background and decisions: [PLAN.md](PLAN.md).

## Running Bazel
Plain `bazel`, from any shell:

```sh
bazel build --target_pattern_file=bazel/autoware_packages.txt     # every Autoware package
bazel test  --target_pattern_file=bazel/autoware_packages.txt
bazel test  @autoware_interpolation//...                          # one package
bazel test  --config=no-agnocast @autoware_interpolation//...     # machine without the agnocast kernel module
```

A shell that sourced a ROS distro (e.g. `/opt/ros/jazzy/setup.bash`) is fine; `.bazelrc` handles it:
- Build actions see only a pinned environment (`--incompatible_strict_action_env`), so outputs and cache hits are the
  same either way (checked: 12,148 action cache hits, nothing recompiled).
- `bazel test` passes only the `--test_env` variables to tests.
- `bazel run` passes the shell's environment to the program. `.bazelrc` removes `AMENT_PREFIX_PATH`, `PYTHONPATH`,
  `CMAKE_PREFIX_PATH` and `COLCON_PREFIX_PATH` for it; without that, a sourced Jazzy makes nodes look up packages in
  `/opt/ros/jazzy` instead of their runfiles. Other variables you set on purpose still reach the program
  (`ROS_DOMAIN_ID`, `ROS_AUTOMATIC_DISCOVERY_RANGE`, ...). `RMW_IMPLEMENTATION`, `ROS_DISTRO`, `LD_LIBRARY_PATH` and
  `ENABLE_AGNOCAST` are set by `.bazelrc`.

To rule out the shell entirely, e.g. when debugging an environment problem:
`env -i HOME=$HOME PATH=$HOME/.local/bin:/usr/bin:/bin bazel ...`.

`//...` only covers the root module (`bazel/`), not the packages: each ROS package is its own module, so address it
as `@<pkg>//...`. Machine-specific flags (`--jobs`, memory) go in the gitignored `user.bazelrc`.

## Host requirements (CUDA, TensorRT)
Bazel fetches the CUDA toolkit (13.2.2) and TensorRT (10.16.1) itself, pinned by sha256: no `/usr/local/cuda`, no
TensorRT packages and no `ansible/roles/cuda` or `ansible/roles/tensorrt` are needed to build or test. The host only
provides:
- the NVIDIA GPU driver (kernel module and `libcuda.so`), new enough for CUDA 13.x (`ansible/playbooks/install_nvidia.yaml`);
- `libstdc++.so.6` and `libgcc_s.so.1`, which TensorRT's prebuilt libraries need (always present on Ubuntu).

Without a GPU, CUDA code still builds and the GPU tests skip themselves.

To change versions:
- CUDA: `CUDA_VERSION` and `CUDA_REDIST_SHA256` in `bazel/cuda.MODULE.bazel`. Re-check the CUDA header patches
  (`bazel/patches/cuda_crt_libcxx_*`), since NVIDIA does not support libc++ on x86.
- TensorRT: `python3 bazel/third_party/tensorrt/update_pins.py <apt version, e.g. 10.16.1.11-1+cuda13.2>`.

Tests that load TensorRT need `data = ["@tensorrt//:runtime_libs"]` and `env = tensorrt_test_env()` (from
`@tensorrt//:defs.bzl`): libnvinfer loads its builder resources by bare name. `bazel run` of a node that builds
TensorRT engines does not set this up yet (runtime image, P4).

## Where a package comes from
Each Autoware package resolves to one of:
- **Local source**: `src/...`, selected by a `local_path_override` in `bazel/autoware_packages.MODULE.bazel`.
  The default for everything under `src/`.
- **RCR**: a released version from https://bazel.ros.org, picked by the `ros` release snapshot pinned in the root
  `MODULE.bazel`. Only some Autoware packages are published there (msgs, `autoware_utils_*`,
  `autoware_lanelet2_extension`, `autoware_cmake`).

Check which one is in use:

```sh
bazel mod explain autoware_common_msgs
# autoware_common_msgs@_                 -> local source (overridden)
# autoware_common_msgs@1.11.0-3.rcr.2    -> RCR
```

Switching never needs edits inside `src/`: package `MODULE.bazel` files depend on each other with
`version = "0.0.0"` ("whatever the root selects"), so only the root changes.

## Switch a package from local source to the RCR
For good, e.g. we stop carrying changes to a package. In `bazel/autoware_packages.MODULE.bazel`, replace

```starlark
bazel_dep(name = "autoware_common_msgs")
local_path_override(module_name = "autoware_common_msgs", path = "src/core/autoware_msgs/autoware_common_msgs")
```

with

```starlark
bazel_dep(name = "autoware_common_msgs", version = "0.0.0")  # from the RCR; version chosen by the ros snapshot
```

Keep the `bazel_dep`: without it `@autoware_common_msgs` is not visible from the root. Also remove its line from
`bazel/autoware_packages.txt` (it is not our code anymore, so its targets need not be in our build). Build once and
commit `MODULE.bazel.lock` with the change: it gains the RCR file hashes. Leave or delete the source under `src/`.

Need a specific RCR version rather than the snapshot's? `version = "X"` only sets a minimum (the highest requested
version wins). To force one, add `single_version_override(module_name = "autoware_common_msgs", version = "X")`.

## Switch a package from the RCR to local source
**Just on your machine, without touching tracked files**: point Bazel at a source directory in `user.bazelrc`:

```
common --override_module=autoware_common_msgs=%workspace%/src/core/autoware_msgs/autoware_common_msgs
```

The directory needs a `MODULE.bazel` and BUILD files. For a package not in `src/`, clone the upstream repository
anywhere and copy the RCR's overlay files into the package directory
(`https://bazel.ros.org/modules/<pkg>/<version>/overlay/BUILD.bazel`, plus `msg/BUILD.bazel` etc.). This does not
modify `MODULE.bazel.lock`. Remove the line to go back.

**For good** (we start carrying the package):
1. `git subtree add --squash --prefix=src/<path> <upstream url> <tag or sha>` and record the URL and SHA in the commit
   message (`chore(src): import <path> <version>`).
2. Add `MODULE.bazel` and `BUILD.bazel` to the package. Start from the RCR overlay if there is one, then apply the
   conventions in [PLAN.md](PLAN.md#package-conventions) (`version = "0.0.0"` on every ROS `bazel_dep`, the
   `autoware_cmake` settings on C++ targets).
3. Add the `bazel_dep` + `local_path_override` pair to `bazel/autoware_packages.MODULE.bazel` and `@<pkg>//...` to
   `bazel/autoware_packages.txt`.
4. Build and test the package and the packages that use it.

## Pitfalls
- An override replaces the whole module. RCR packages that depend on it still reference the RCR's target names
  (`:cc`, `:cc_shared`, `//msg:X`, `<pkg>`, `<pkg>_library`, `:ament_package`); a missing name fails only when
  something builds that dependent target.
- A `bazel_dep` without a version is allowed only while the root overrides that module. Removing the override then
  fails with `bad bazel_dep on module '<pkg>' with no version`.
- RCR versions like `1.11.0-3.rcr.2` sort **below** `1.11.0`, so never write the `package.xml` version as a minimum.
- Overrides in a non-root module are ignored; only the root (this repository) decides.
