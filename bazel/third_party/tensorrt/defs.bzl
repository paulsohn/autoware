"""Native test registration with declared TensorRT runtime discovery."""

load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@rules_python//python:defs.bzl", "py_test")
load("//:pins.bzl", "TENSORRT_VERSION")

TENSORRT_VERSION_MAJOR = TENSORRT_VERSION.split(".")[0]

def tensorrt_cc_test(name, srcs, deps, data = [], local_defines = [], dynamic_deps = []):
    """Keep native GTest cases while making bare-name builder dlopen portable."""
    binary = name + "_binary"
    cc_binary(
        name = binary,
        testonly = True,
        srcs = srcs,
        deps = deps,
        dynamic_deps = dynamic_deps,
        data = data,
        local_defines = local_defines,
    )
    runtime_files = [
        Label("@tensorrt_artifacts//:usr/lib/x86_64-linux-gnu/libnvinfer.so.10"),
        Label("@tensorrt_libstdcxx//:lib/libstdc++.so.6.0.30"),
        Label("@tensorrt_libgcc//:lib/libgcc_s.so.1"),
    ]
    py_test(
        name = name,
        srcs = [Label("//:test_runner.py")],
        main = Label("//:test_runner.py"),
        args = ["$(rlocationpath :" + binary + ")"] + ["$(rlocationpath " + str(p) + ")" for p in runtime_files],
        data = [":" + binary, Label("//:runtime_libs")] + runtime_files,
        deps = [Label("@rules_python//python/runfiles")],
        tags = ["no-remote", "no-sandbox", "requires-gpu"],
        timeout = "moderate",
        precompile = "disabled",
    )
