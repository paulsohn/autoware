# Upstreaming to the ROS Central Registry (RCR)

What this PoC found that is worth sending to the RCR (openrobotics/ros-central-registry) and to rules_cuda
(bazel-contrib/rules_cuda), what to send it as, and the evidence. Upstream state checked 2026-11-10.

Context worth stating in each report: Autoware's `autoware_core` + CUDA packages (94 ROS packages, ~205 tests)
on `ros@lyrical.2026-10-05.rcr.0`, Ubuntu 24.04 x86_64, Bazel 9.3.2. Install tree via `ament_underlay`, runtime image via
`rules_oci`, a planning-simulator demo that drives from that image. It is a large consumer outside the RCR's own CI,
and it exercises install trees, headless containers and CUDA, which the RCR's examples barely cover.

## Already solved upstream? (rcr.0 → rcr.3)

Only `rosdistro` has newer releases (`lyrical.2026-10-05.rcr.1`–`rcr.3`); `ros`, `ros_core`, `pcl`, `vtk`, `rules_cuda`
and the ros2cli extensions are unchanged. The rcr.3 overlay differs from rcr.0 in two files:
- `cc/BUILD.bazel`: `@rosdistro//cc:curl` now aliases `@curl//:curl` (was `curl_shared_lib_headers`).
- `cc/rules.bzl`: `dynamic_deps` are dropped when `--dynamic_mode=off` (a select on `//:dynamic_mode_off`).
- `MODULE.bazel`: openssl 3.5.8 → 4.0.1, curl 8.12.0 → 8.22.0, gz-rendering, gz-tools, libxcb, libtorch bumps.

None of this overlaps our patches: `ament/ament_package.bzl`, `ament/install_gen.py` and `cc/macros.bzl` have the same
hashes in rcr.0 and rcr.3, and `rosdistro_vtk_core.patch` still applies to rcr.3's `cc/BUILD.bazel` (dry run: hunks 4–5
at offset 2). No RCR issue or PR mentions entry points, VTK/GL, host `python3` or CUDA.

Bumping is a separate, cheap experiment: change `version` in the root `single_version_override(module_name =
"rosdistro", ...)` to `lyrical.2026-10-05.rcr.3`, re-lock, then run the full suite and the image check. The openssl major
bump is the only change likely to need attention. Do it before filing, so reports are against the latest release.

## 1. Install-tree bugs, patches ready (RCR PRs)

None of these show up in `bazel test`; they only appear in an `ament_underlay` install tree, which is probably why the RCR's CI
misses them. Each one has a patch in `bazel/patches/` that we generated from the pristine file with a diff tool.

| # | Symptom | Cause | Our patch | Verified |
|---|---|---|---|---|
| 1.1 | `import numpy` in an install tree: `No module named 'numpy._core._multiarray_umath'` | `ament_package` installs only the `files` of `python` targets; a pip wheel's compiled modules are in its runfiles | `rosdistro_python_extensions.patch`: also install the target's *own-repository* runfiles under `site-packages/` (taking all runfiles gave every package every pip package and broke metadata lookup) | image: numpy, ros2cli work |
| 1.2 | `ros2: error: invalid choice: 'pkg' (choose from )` | ros2cli extensions register via a hand-written `<pkg>.egg-info/entry_points.txt` inside the `py_library`; `ament_package` writes its own egg-info without it | `rosdistro_python_extensions.patch` (collects them) + `rosdistro_entry_points.patch` (`install_gen.py --entry-points-file`, merged) | `ros2 pkg list` → 344 packages |
| 1.3 | `ros2 launch/run/node/service/param/component/action/lifecycle`: `invalid choice` | 8 ros2cli extension overlays have no `entry_points.txt` (ros2topic's has one). Still true at the latest versions (ros2action 0.40.9-1.rcr.0, ros2launch 0.29.9-1.rcr.0, ...) | `bazel/patches/ros2*_entry_points.patch`, generated from each `setup.py` by `bazel/tools/ros2cli_entry_points_patch.py` (same `write_file` as the ros2topic overlay) | demo launches from the image |
| 1.4 | `python3: command not found` (exit 127) in `@proxsuite//:jrl_headers` on a builder without host Python | `ros_version_header` / `ros_jrl_headers` genrules run `python3 -c` from `PATH` | `rosdistro_hermetic_python_genrules.patch`: `tools = ["@rules_python//python/bin:python"]`, `$(execpath ...)`. Exec configuration, so also right for cross builds. Output headers byte-identical | A2 container build |

Suggested form: one PR for 1.1 + 1.2 (same rule, `ament_package.bzl` + `install_gen.py`), one PR per overlay group for
1.3 (or contribute the generator so overlays get entry points from `setup.py`), one PR for 1.4. Add a test to the RCR CI that
builds a small `ament_underlay` and runs `ros2 pkg list` / `ros2 launch --help` from it. That one test would catch
1.1–1.3.

Related, report only (no patch):
- **`bootstrap_impl`**: rules_python's default `system_python` bootstrap starts every `py_binary` build tool through
  `/usr/bin/env python3`. The RCR CI does not notice because its hosts have Python and `BAZELRC_TEMPLATE` pins
  `PATH=/bin:/usr/bin:/usr/local/bin`. We set `--@rules_python//python/config_settings:bootstrap_impl=script` in `.bazelrc`.
  Proposal: add it to `BAZELRC_TEMPLATE` (it changes launcher digests, not behaviour), and make one CI job build in an image
  without `python3`. That job found 1.4 and this one at the same time.
- **Install-tree size**: `ament_package` copies each library's dependencies into every package's `lib/` (libpcl.so ×19,
  librclcpp.so ×93, libc++.so.1 ×156). Our underlay is 4.2 GB, or 2.37 GB after deduplication: `images/underlay.bzl` turns repeats
  of the same Bazel file into relative symlinks at analysis time, and `images/dedup_tar.py` does the same for identical content
  under different paths. Proposal: do the first one in `ament_underlay` itself.
- **Incomplete GUI install**: `ldd` over the install tree finds 5 RViz GUI libraries missing (Qt platform plugins,
  `libOgrePaging`). It does not matter headless, but `rviz2` from an install tree would fail.
- **`--run_env=ROS_HOME=.ros`** (RCR `.bazelrc`) is relative to the runfiles directory: a binary run from a
  `--script_path` script in a read-only runfiles tree aborts (`spdlog_ex: Failed opening file .ros/log/...`).
  Documentation, or an absolute default.

## 2. Headless PCL: split VTK's rendering from its data modules (RCR PR)

Finding: every node that links `@rosdistro//cc:pcl` needs Qt and the system OpenGL loader (glvnd) at run time.
`@pcl//:io` and `:surface` depend on the `@vtk` umbrella, and `@rosdistro//cc:vtk` is one shared library with rendering, Qt
widgets and `@libglvnd//:glvnd_stubs`. pcl's io and surface sources include only data-model, IO, imaging and filter headers.
The RCR builds only link stubs for glvnd, so a container without apt GL packages fails (`libGL.so.1: cannot open
shared object file`, exit 127) for map loaders, planners and API adaptors. Tests pass on a developer host only because the host
has libGL.

Patches: `pcl_io_surface_vtk_modules.patch` makes pcl's io and surface depend on CommonCore, CommonDataModel,
FiltersCore, FiltersGeneral, FiltersModeling, IOGeometry, IOImage, IOLegacy, IOPLY and ImagingCore.
`rosdistro_vtk_core.patch` adds a `vtk_core` shared library under `libpcl.so`; `:vtk` (rendering) builds on it, and PCL
visualization/people/outofcore move to `pcl_visualization_shared`.

Evidence: ELFs in the image that reference libGL/libGLX/libEGL/libOpenGL went from 3,876 to 557, and none of the
remaining 557 are Autoware binaries (they are Qt, VTK rendering, OpenCV GUI and RViz). The runtime image has no apt
packages. 205 tests pass and the demo drives. Method (reusable for other umbrella targets): grep the `#include <vtk*.h>` of
the component, map the headers to modules, then check that `bazel cquery 'somepath(<modules>, @@qtbase+//:gui +
@@libglvnd+//:all)'` is empty. Confirm with `ldd` over the artifact, because `somepath` also follows non-link edges such as
`collect_transitive_dynamic_deps`.

Breaking change for users: a target that uses `pcl::visualization` through `@rosdistro//cc:pcl` must now depend on
`pcl_visualization_shared`. Mention this in the PR.

## 3. CUDA with the RCR toolchain (rules_cuda + RCR)

The RCR ships `rules_cuda` 0.3.0.rcr.0 with 4 patches (`clang_host_defines`, `cudart_static_archives_linkable_more_than_once`,
`module_dot_bazel_version`, `multiplatform_redist`). What we added on top, with where each item belongs:

| # | Finding | Patch | Send to |
|---|---|---|---|
| 3.1 | `sm_110` (Jetson Thor; CUDA 13 renamed SM101 → SM110) rejected: `compute_110 is not a supported cuda arch` | `rules_cuda_sm110.patch` | Already in rules_cuda **v0.4.0-beta1** (not in v0.3.0). Ask the RCR to move to 0.4.x once it is out of beta, or carry the 3-line patch |
| 3.2 | Two `cc_shared_library`s using the CUDA runtime fail analysis: `culibos_a` "linked statically by different cc_shared_libraries but not exported" | `rules_cuda_culibos_linkable.patch`: CUDA 13 moved `libculibos.a` into its own component, which the RCR's cudart patch does not tag `LINKABLE_MORE_THAN_ONCE` | RCR: extend `cudart_static_archives_linkable_more_than_once.patch` |
| 3.3 | No way for a root module to patch a redist component (we needed it for CUDA headers) | `rules_cuda_component_patches_{repositories,extension}.patch`: `cuda.component_patch(component, patches)`, applied on every redist platform | rules_cuda (generic feature). Two files because Bazel's patch parser misreads this multi-file diff that GNU `patch` applies (`CONTENT_DOES_NOT_MATCH_TARGET`). That parser problem may be worth a Bazel issue too |
| 3.4 | Reproducibility: CUDA objects depend on the absolute source path and on the PID (below) | `cuda_nvcc_cudafe_wrapper.patch` + `cuda_nvcc_profile_wrap.patch` (path), `rules_cuda_nvcc_keep_dir.patch` (PID) | rules_cuda: comment on #372, or a new issue citing it; the keep-dir change is a rules_cuda PR |

**3.4 in detail** (JOURNAL "A2"). Host and container builds differed only in 12 CUDA `.pic.o` files (and the `.so` and
tests that link them). The difference is in the anonymous-namespace names (`_GLOBAL__N__675670bf_...` vs `_4a905558_...`).
rules_cuda #372 diagnosed this hash and closed it with `--frandom-seed` (`nvcc_fixed_random_seed`, nvcc ≥ 12.9), which
rules_cuda already passes. It is not enough. In experiments that rebuilt one object, neither the PID nor the hostname changed
the hash, but mounting the workspace at the host's path in the container gave the host's hash. nvcc passes the *realpath* of
the source to cudafe++ as `--orig_src_path_name`, and cudafe++ hashes it. Bazel sandboxes symlink their inputs, so for a
`local_path_override` (or main-repo) source the realpath is the checkout path. Two builds at the same path are identical (our
A1: 0 of 116,465 outputs differ), so the problem only shows up across machines or checkouts, which is exactly where a remote
cache needs hits. Fix: a cudafe++ wrapper that passes the execroot-relative `--orig_src_file_name` instead (added as a
text file: Bazel's patch tool corrupts a renamed binary). After that, 8 bytes still differed: nvcc's temporary names
`tmpxft_<pid>_...` end up in the `FILE` symbol and debug strings. linux-sandbox's fresh PID namespace hides this
(processwrapper-sandbox does not). This is the same `tmpxft` PID that #372 solved with `-objtemp`. Our fix:
`--keep --keep-dir=<per-action output directory>`, which names intermediates after the source; the directory is emptied
after the compile. Result (JOURNAL "A2 continued"): the same runtime image digest from the host and the container;
what still differs is runfiles manifests and the Qt headers (section 4).

**Toolchain data point** (for the RCR's toolchain choices; a write-up, not a patch). CUDA with the RCR's hermetic LLVM 21 +
libc++:
- nvcc 13.0 cannot use it: it refuses host clang ≥ 21, and its EDG front end fails on libc++ 21 (`__builtin_common_type
  is not a template`).
- nvcc **13.2** works with host clang 21. It accepts clang < 22 and parses libc++ 21. One more fix is needed: CUDA's
  `crt/math_functions.h{,pp}` enable their libc++ ≥ 21 branches only for Android/HORIZON. `cuda_crt_libcxx_math_functions_*.patch`
  defines `__NV_LIBCPP_LINUX` and enables the three libc++ branches (not the bionic ones), plus
  `-D_ALLOW_UNSUPPORTED_LIBCPP`. NVIDIA does not support libc++ on x86; the SBSA headers are byte-identical.
- clang as the CUDA compiler: clang 21 supports CUDA ≤ 12.8 and has no sm_110; clang 23 works with CUDA 13.2 after one CCCL
  patch. We parked an LLVM 23 build of the whole tree on branch `bazel-llvm23` (`docs/bazel/LLVM23.md` there): libc++ 23
  dropped transitive includes in rosidl_runtime_cpp, Fast DDS, libzmq, bison and PCL; clang 23 errors on C pointer
  mismatches in OSQP; and one Autoware utility returned results in a different order. That list is a head start for
  the RCR's own LLVM ≥ 23 bump.
- Result: 7 perception kernels + 35 GPU tests (RTX 5070, sm_120) pass on both routes.

**Patterns others will need** (docs or an example, not patches):
- TensorRT from NVIDIA's apt `.deb`s, sha256-pinned per CPU (`bazel/third_party/tensorrt`). Bazel's `deb` archive type only
  unpacks the `ar` layer, and each `.deb` overwrote the previous `data.tar.xz`, so we extract each one into its own directory.
  Use one repository per CPU plus a separate paths repository, so x86 does not fetch the 2.3 GB aarch64 set. `libnvinfer`
  `dlopen()`s its builder resources by bare name, so tests need `LD_LIBRARY_PATH`. It links the host's `libstdc++.so.6` and
  `libgcc_s.so.1`, so "only the driver comes from the host" does not hold for it.
- `cuda_library` gtest cases need `alwayslink = True`, or the test "passes" with 0 tests.
- CUDA 13 moved CCCL to `include/cccl/`: depend on `@cuda//:thrust` / `:cub` / `:libcudacxx`.

## 4. Reproducibility findings (RCR issue)

- A1: two cold host builds at the same output base path gave identical outputs (0 of 116,465 files differ) and the same
  image digest. That is a good result for the RCR to cite.
- A2 (host vs `dev` container) found two RCR-side items besides CUDA:
  - **Qt `syncqt` directory order**: `qtbase` `QtGui/QRgbaFloat16` / `QRgbaFloat32` forwarding headers include
    `qcolortransform.h` on one filesystem and `qrgbafloat.h` on the other. A class declared in two headers is resolved by
    directory iteration order (`qt+/qt_tools.bzl`). The compiled objects were identical, but the headers are outputs.
    Proposed fix: sort, or pin the header.
  - 302 `*.runfiles_manifest` files differ by their absolute paths. This is Bazel behaviour and they are not shipped; mention it
    only so others don't chase it.
- Lesson for the RCR's docs: `linux-sandbox`'s PID namespace and a fixed checkout path hide nondeterminism. A second
  environment (a container with a different path, `processwrapper-sandbox`) finds it. Scripts:
  `bazel/tools/repro/cold_build.sh`, `compare_digests.sh`.

## 5. Documentation / UX (RCR issues or a docs PR)

Each of these cost us real time, and each has a short, concrete fix:
1. `examples/.bazelrc` lacks the per-file C++ standard exceptions of `BAZELRC_TEMPLATE` (`tools/ci/setup_workspace.py`):
   Fast DDS fails after 45 min (`sizeof` to incomplete `StatisticsAncillary`). Matching the template exactly also raised
   remote-cache hits from 1,504 to 10,746 of ~23k actions. Generate `examples/.bazelrc` from the template, or point to it.
2. `bazel_dep` on `ros_core` (or a variant) silently resolves packages outside the variant to the empty `0.0.0` stub:
   `no such package '@@geographic_msgs+//msg'`. Document "depend on `ros`", or make stubs fail with a message naming
   the release module.
3. Version ordering: `1.11.0-3.rcr.2` sorts *below* `1.11.0` (semver pre-release), so a module cannot state a real
   minimum version of an RCR package. `0.0.0` + a root override is the only working pattern. Document it, since it
   shapes how downstream workspaces are laid out (our PLAN.md "Local source vs RCR").
4. `--@rosdistro//:rmw=dynamic` (the default) pulls the whole distribution into every node: the RMW loader's ament index
   → every RMW's `ament_package` → `@rosdistro//:ament_package` (all third-party libraries, Ogre, OpenCV, PySide6). The
   talker went from 89,186 to 19,268 configured targets with a fixed RMW, and analysis from ~5 min to 46 s. Document it as the first
   tuning step.
5. rules_rust's `crate_universe` fails (`Unable to load package for //:MODULE.bazel`) without a root `BUILD.bazel`.
6. Embedding Python (`pybind11::embed`) in binaries built with the hermetic libc++: libstdc++-based wheels (matplotlib)
   bind `__cxa_throw` and the unwinder to LLVM's `libc++abi`, so their exceptions are not caught. Fix:
   `sys.setdlopenflags(RTLD_NOW | RTLD_DEEPBIND)` in a `sitecustomize.py` (`autoware_pyplot`). Any ROS project that
   embeds Python under the RCR toolchain hits this.
7. The toolchain targets glibc 2.34: glibc ≥ 2.35 additions (`M_PIf`, ...) are missing even on a 24.04 host. Say so
   where the platform is documented.
8. `bazel query` ignores `select()` and fetches every platform's dependencies, including a SourceForge mirror that does
   not resolve. Recommend `cquery`, and check that mirror URL.
9. OpenMP: the toolchain builds libomp only as a static `cc_runtime_stage0_library`, which every consumer would copy.
   We re-link it as one shared `libomp.so` (`bazel/third_party/openmp`). A shared OpenMP target in the toolchain would
   remove the need for that.
10. Developing a package that the RCR also ships (local source vs registry): use `version = "0.0.0"` everywhere, put
    `local_path_override` only in the root, and use `--override_module` in `user.bazelrc` per developer. Worth an RCR docs
    page; ours is in PLAN.md and DEVELOPING.md.

## Order of work

1. Bump to rosdistro rcr.3 locally (section "Already solved"), so every report is against the latest release.
2. File the install-tree PRs (1.1–1.4) with the underlay CI test proposal. They are self-contained and easy to review.
3. VTK/PCL split PR (section 2), with the `ldd` evidence and the breaking-change note.
4. rules_cuda: the component-patch feature (3.3) upstream; culibos (3.2) to the RCR; ask about 0.4.x (3.1).
5. The CUDA path and PID findings on rules_cuda #372 (3.4), with the keep-dir patch as a PR.
6. One docs issue that collects section 5 (maintainers can split it), plus the Qt `syncqt` issue.
