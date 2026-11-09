"""Bazel counterpart of autoware_package() in cmake/autoware_package.cmake. Keep the two in sync.

Use on every cc_library/cc_binary/cc_test of an Autoware package:
    copts = autoware_copts(),
    cxxopts = AUTOWARE_CXXOPTS,
    local_defines = ros_local_defines() + AUTOWARE_DEFINES,

Not carried over: ROS_DISTRO_* defines (only GALACTIC/HUMBLE are ever checked; Lyrical gets none, as in CMake)
and the include_directories(SYSTEM ...) workarounds (covered by the system-header handling below).
"""

load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

AUTOWARE_WARNINGS = [
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Werror",
    # Clang: PCL headers use these extensions.
    "-Wno-gnu-anonymous-struct",
    "-Wno-nested-anon-types",
]

AUTOWARE_CXXOPTS = ["-std=c++17"]

AUTOWARE_DEFINES = [
    "BOOST_ALLOW_DEPRECATED_HEADERS",
    # PCL propagates -fopenmp; without this Eigen spin-waits on OpenMP threads and starves callbacks.
    "EIGEN_DONT_PARALLELIZE",
]

def autoware_copts(own_include_prefixes = None):
    """Warning flags, applied to this package's code only, like CMake does.

    CMake passes dependency include dirs as -isystem; the hermetic LLVM toolchain passes them as -I, so
    -Werror would fire inside Boost/ROS/gtest headers. Bazel's external_include_paths feature fixes that only
    when enabled on the dependency (i.e. globally), which changes every RCR compile command and misses the
    remote cache. Instead: every #include is a system header except the package's own, by include prefix.

    Args:
      own_include_prefixes: include prefixes of this package's headers. Default: "<pkg>/" and, for
        autoware_<x>, "autoware/<x>/" (the two Autoware layouts).
    """
    pkg = native.module_name()
    if own_include_prefixes == None:
        own_include_prefixes = [pkg + "/"]
        if pkg.startswith("autoware_"):
            own_include_prefixes.append("autoware/" + pkg[len("autoware_"):] + "/")
    return AUTOWARE_WARNINGS + ["--system-header-prefix="] + [
        "--no-system-header-prefix=" + p
        for p in own_include_prefixes
    ]

def _cc_headers_only_impl(ctx):
    return [CcInfo(compilation_context = cc_common.merge_compilation_contexts(
        compilation_contexts = [d[CcInfo].compilation_context for d in ctx.attr.deps],
    ))]

cc_headers_only = rule(
    implementation = _cc_headers_only_impl,
    doc = """Headers and include paths of `deps`, without linking them.

    For a library that includes a dependency's headers but leaves its symbols to the final executable, as CMake
    allows (e.g. a test-helper shared library including gtest.h, linked into tests that link gtest themselves).
    Linking such a dependency statically into the shared library would give the executable a second copy.
    """,
    attrs = {"deps": attr.label_list(providers = [CcInfo], mandatory = True)},
    provides = [CcInfo],
)
