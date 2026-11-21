# Grid-map source provider

This module supplies the upstream core, CV, ROS and message packages needed by
native grid-map consumers. It contains build support; the implementation is
fetched from [ANYbotics/grid_map](https://github.com/ANYbotics/grid_map/tree/0ece1b53ca03199527d0d42913cf2567d0d6a913),
tag 2.5.0, commit `0ece1b53ca03199527d0d42913cf2567d0d6a913`.
The tar.gz SHA-256 is
`c621063445ee59e73495528ee3a00022f435accc20cd5c0cf2a494e998d9b82b`.
The source archive retains its upstream BSD licensing and provenance.

The product root selects this module through a local override and pins ROS
registry inputs. Component modules depend on `grid_map` and its public library
labels, without metarepository labels. `BUILD.sdk.bazel` is applied inside the
source archive, so source/header paths remain independent of workspace placement.
No dependency source exists locally, and the four grid-map package metadata URLs
returned HTTP 404 from RCR when checked on 2026-11-22.

Use `:grid_map_core`, `:grid_map_cv` or `:grid_map_ros` in C++ dependencies and the
corresponding `*_runtime_dynamic_deps` provider in binary dynamic dependencies.
The `*_shared` labels expose individual libraries; the runtime aggregators also
link the declared transitive libraries needed by inline/plugin/OpenCV APIs. Each `*_index` owns
its original ROS package identity. The message bindings are exposed through
`:grid_map_msgs_cc`, `:grid_map_msgs_cc_shared`, `:grid_map_msgs_py` and
`:grid_map_msgs_ament_index`. The original two messages and four services retain
explicit `grid_map_msgs` identity inside the shared source repository.

Core exports the Eigen DenseBase and Functors plugin definitions from its CMake
extras to consumers. Include-file macro values retain escaped quotes through
Bazel defines tokenization; unescaped quotes are removed before preprocessing. The CMake-only helper's default language/warning settings
do not require an extra module; the root selects the native toolchain and C++
standard. CV preserves its filter registration and shared-library basename.
The source archive URL ends in a commit ID, so acquisition declares `type =
"tar.gz"` explicitly.

The integration gate is:

```sh
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @grid_map//:tests
```

It includes all original core/CV/ROS test translation units and an indexed
InpaintFilter factory check. ROS tests run in a temporary writable directory
with declared copies of `double_chatter` and `test_multitopic`, and the pinned
SQLite storage provider in their index. The adapter preserves GTest XML output
and normalizes runtime paths before changing directory. No original tests are
edited or excluded. The complete gate passes 109 cases (91 core, six CV, eleven ROS and one factory),
with no failures or skips. See the integration journal for prerequisites and
diagnosed setup failures. Other upstream packages are outside this provider
scope; their presence in the archive does not declare Bazel support.
