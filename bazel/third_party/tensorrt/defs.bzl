"""TensorRT helpers for packages: version define and the run-time library path for tests."""

load("//:pins.bzl", "TENSORRT_VERSION")
load("@tensorrt_paths//:defs.bzl", "LIB_DIR_AARCH64", "LIB_DIR_X86_64")

# TENSORRT_VERSION_MAJOR in CMake (tensorrt_cmake_module).
TENSORRT_VERSION_MAJOR = TENSORRT_VERSION.split(".")[0]

def tensorrt_test_env():
    """env for tests whose process loads TensorRT: libnvinfer dlopen()s its builder resources by bare name.

    Put Label("@tensorrt//:runtime_libs") in the test's data as well.
    """
    return select({
        Label("@platforms//cpu:aarch64"): {"LD_LIBRARY_PATH": LIB_DIR_AARCH64},
        Label("@platforms//cpu:x86_64"): {"LD_LIBRARY_PATH": LIB_DIR_X86_64},
    })
