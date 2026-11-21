# acados dependency provider

This module owns acados v0.5.3 native libraries and its Python-to-C generator.
The root selects the local override; source components depend on `@acados` and
load `acados_codegen` from `@acados//:codegen.bzl`. There are no product labels,
host `/opt/acados`, virtual environments, installation commands or downloads
inside build actions.

MODULE.bazel pins the acados commit, its actual BLASFEO/HPIPM/Catch submodule
revisions, archive checksums, tera_renderer v0.2.0 and CasADi 3.7.2/SciPy 1.17.1
wheel checksums, and the required GNU C++/GCC runtime payloads. The generator
sets its library search path from these declared inputs. Other Python inputs come from the selected registry's hashed
pip_ros closure. The generator uses Linux x86_64 CPython 3.14 inputs on the
execution platform; native C targets use generic C kernels. Other builder
platforms and optimized architecture kernels need their own declared inputs
and validation. Do not detect the builder CPU during repository acquisition.

Native overlays preserve acados' default HPIPM backend and upstream source
selection. They provide both flat and upstream-prefixed BLASFEO/HPIPM header
layouts, Eigen-independent native library providers, generic panel-major kernels
and BLASFEO's default K_MAX_STACK=300. The upstream C++ unit suite is separate
from its C model fixtures so its bundled legacy Catch framework can use C++14;
Autoware's C++ standard remains controlled by the root.

Declare a component-local py_binary using its original generator sources, main
and import root, with `deps = ["@acados//:template"]`. Pass that executable and
its concrete relative output paths to acados_codegen. The rule supplies the
pinned renderer/templates, runs in an isolated temporary directory and publishes
only declared C/header outputs. Compile those outputs with a cc_library depending
on `@acados`, adding the generated target's directory as an include root.
Generated files must shadow source-tree IDE stubs. Python/CasADi are build tools;
the native consumer does not acquire their runtime closure from this C library.

Run the SDK gate before accepting a consumer:

```sh
bazel test --config=runtime --config=no-agnocast --local_test_jobs=1 \
  --test_output=errors @acados//:tests
```

It includes every source in upstream's enabled native unit_tests executable and
a scalar-integrator experiment: generate symbolic solver C, compile it against
the pinned native libraries, solve from x0=1 and require successful convergence
with terminal magnitude below 0.01. No host CMake, CasADi installation or runtime
Python solver compilation is involved. This is numerical correctness coverage,
not a real-time performance claim.
