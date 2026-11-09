# LLVM 23 toolchain (route A) — parked experiment

Branch `bazel-llvm23`: one commit on top of `bazel` that moves the hermetic toolchain from LLVM 21.1.8 (the RCR's
pin) to 23.1.2 and compiles CUDA with clang instead of nvcc. Parked 2026-11-09 in favour of route B (keep LLVM 21,
nvcc 13.2 with clang 21 as host compiler); see the main JOURNAL ("P3: CUDA toolchain routes").

## Why it was tried

CUDA 13 (needed for Jetson Thor, `sm_110`) cannot be compiled with the build's clang 21 + libc++ 21:
- nvcc 13.0 rejects clang ≥ 21 as host and its EDG front end cannot parse libc++ 21 (`__builtin_common_type`).
- clang 21 as CUDA compiler supports CUDA ≤ 12.8 and no `sm_110`.

clang 23 supports CUDA ≤ 13.2 and `sm_110`/`sm_120`, so with LLVM 23 one compiler and one standard library cover
C++ and CUDA, on x86 and SBSA. The `llvm` module (0.8.25.rcr.0) already ships 23.1.2 (prebuilt minimal toolchain
for linux-amd64/arm64 + llvm-project sources for libc++); the switch is `llvm.version(llvm_version = "23.1.2")`.

## Measurements (x86_64, 12 jobs)

| Run | Change | Wall time | Tests |
|---|---|---|---|
| 1 | LLVM 23.1.2 only | 8547 s (cold; 6731 of 64268 actions from the disk cache) | 6 pass, 188 fail to build |
| 2 | + wave-1 fixes below | 728 s | 146 pass, 35 fail to build, 1 fails |

Every compiler change discards all cached C++ outputs (RCR remote cache and local disk cache); after one build the
local disk cache serves hits again.

## What LLVM 23 needs

### Done on this branch (wave 1)
Patches (`bazel/patches/`, applied with `single_version_override` in `MODULE.bazel`):
| Patch | Module | Cause |
|---|---|---|
| `rosidl_runtime_cpp_locale.patch` | rosidl_runtime_cpp 5.2.1-1.rcr.2 | libc++ 23 dropped transitive includes: `<codecvt>` no longer brings `<locale>` (`std::wstring_convert`). Breaks every ROS message target. |
| `fastdds_libcxx23_includes.patch` | fastdds 3.4.2.bcr.1.rcr.0 | `<algorithm>` (sort/unique/lower_bound), `<locale>` (wstring_convert) |
| `libzmq_new.patch` | libzmq 4.3.5.bcr.7 | `<new>` (std::nothrow) |
| `rules_bison_cstdlib.patch` | rules_bison 0.4.bcr.1 | `<cstdlib>` (getenv) in the runfiles helper rules_bison patches into bison |
| `rules_cuda_clang_cuda_version.patch` | rules_cuda 0.3.0.rcr.0 | clang reads the CUDA version from `--cuda-path`/version.json `"cuda"`; the nvvm component has only `"nvvm"`, so clang assumed CUDA > 13.2 and emitted PTX 9.2, which CUDA 13.0's ptxas rejects. **Written, not yet built.** |
| `rules_cuda_sm110.patch` | rules_cuda 0.3.0.rcr.0 | `sm_110` missing from the arch list (also needed by route B; on `bazel` too) |

Flags (`.bazelrc`):
- `--per_file_copt=external/osqp.*@-Wno-error=incompatible-pointer-types` (and host): clang 23 makes this C warning an
  error; OSQP 0.6.3 and 1.0 pass `long long*` as `int64_t*` (`long*`), the same type on LP64.
- `--@rules_cuda//cuda:compiler=clang`, `--repo_env=CUDA_COMPILER_USE_CC_TOOLCHAIN=true`,
  `--@rules_cuda//cuda:copts=-D_ALLOW_UNSUPPORTED_LIBCPP` (CUDA 13 headers refuse libc++ on x86 by policy).

Our sources:
- `agnocast_ioctl_wrapper` (node_info/topic_info/topic_list.cpp): `#include <errno.h>`.
- `autoware_gnss_poser` test: `(void)std::make_shared<...>` inside `EXPECT_THROW` (libc++ 23 marks it `[[nodiscard]]`).

### Still needed (wave 2 findings, not done)
- **pcl** (BCR): `common/include/pcl/PolygonMesh.h` uses `std::back_inserter` without `<iterator>` → patch. Many PCL
  users are blocked behind it; expect a further, smaller wave.
- Our sources:
  - `autoware_trajectory` `src/detail/util.cpp:32`: `<iterator>`.
  - `autoware_pose_initializer` test lines 554, 562; `autoware_lanelet2_extension` `test/src/test_regulatory_elements.cpp:161`: unused `[[nodiscard]]` results.
  - `autoware_gyro_odometer` test line 164: `tf2_ros::StaticTransformBroadcaster(node)` (NodeT constructor, deprecated);
    clang 23 now reports it through `std::make_unique` → pass `*node` (NodeInterfaces), as done for gnss_poser.
- **Behaviour change:** `autoware_lanelet2_utils` `test/topology.cpp` (`ordinary_succeeding_lanelet_sequences*`):
  `get_succeeding_lanelet_sequences` returns the same two sequences in the opposite order (2271 before 2265). Code
  taking `.front()` would pick another branch at that intersection. Find what order it depends on (likely unordered
  iteration or an unstable sort) before accepting LLVM 23; this is a correctness question, not a build fix.
- CUDA: build and test `autoware_cuda_utils` with the version patch (or CUDA 13.2.2, whose ptxas accepts PTX 9.2).

## Resuming

```
git switch bazel-llvm23          # or: git worktree add ../autoware-llvm23 bazel-llvm23
git rebase bazel                 # conflicts only where bazel changed the same files
bazel test --keep_going $(cat bazel/autoware_packages.txt)
```
Re-check each patch first: drop it if the RCR module version moved past the fix (a patch that no longer applies fails
the fetch). The RCR moving to LLVM ≥ 23 itself would make most of the third-party patches unnecessary.

## Decider: autoware_tensorrt_plugins CUDA kernels (2026-11-10)

The same package and BUILD as on `bazel-llvm21` (7 kernels + `reference_kernels_test`; nvcc-only flags behind a
compiler `select()`), CUDA 13.2.2, clang 23 as CUDA compiler. Brought over from `bazel-llvm21`: the `tensorrt`
module, `rules_cuda_culibos_linkable.patch`, the component-patch support for rules_cuda
(`rules_cuda_component_patches_*`), cuda_utils' `alwayslink` fix, and the `argsort.hpp` `<cstddef>` fix.

- The CUDA version-detection patch works: no PTX-version errors with 13.2.
- 4 kernels and 4 cuda_utils test files compiled as they are. The 4 files that include CCCL's `cuda/std/string_view`
  (via thrust/CUB) failed: "in CUDA/HIP, deduction guides may only be annotated with '__host__ __device__'". CCCL
  13.2 declares four guides host-only (`_CCCL_HOST`). nvcc accepts that, clang 23 errors, and there is no flag to
  disable it → `cuda_cccl_string_view_guides.patch` (annotate them `_CCCL_HOST_DEVICE`, like the other guide).
- Then: `test_autoware_cuda_utils` 17 and `reference_kernels_test` 18 cases pass on the GPU (RTX 5070), as on route B.

So the CUDA side costs both routes one NVIDIA-header patch (route B: CUDA's math headers for libc++; route A: CCCL's
deduction guides for clang). The difference between the routes is the C++ side above.
