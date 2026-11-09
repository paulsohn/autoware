# Generated from @tensorrt//:BUILD.debs.tpl: TensorRT {version} ({triple}) from NVIDIA's .debs.
load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "headers",
    hdrs = glob(["usr/include/{triple}/*.h"]),
    includes = ["usr/include/{triple}"],
)

# By SONAME (the .debs ship libX.so.10 -> libX.so.10.N.M), so the runtime loader finds what the linker recorded.
[
    cc_import(
        name = name,
        shared_library = "usr/lib/{triple}/lib%s.so.10" % name,
    )
    for name in [
        "nvinfer",
        "nvinfer_plugin",
        "nvonnxparser",
    ]
]

cc_library(
    name = "tensorrt",
    deps = [
        ":headers",
        ":nvinfer",
        ":nvinfer_plugin",
        ":nvonnxparser",
    ],
)

# Files libnvinfer dlopen()s by bare name at run time (builder resources per GPU architecture). NVIDIA forbids
# linking them (each NEEDS "do_not_link_against_nvinfer_builder_resource_*"); they are found through
# LD_LIBRARY_PATH (tensorrt_test_env() in @tensorrt//:defs.bzl), as an apt install finds them in /usr/lib/{triple}.
filegroup(
    name = "runtime_libs",
    srcs = glob(["usr/lib/{triple}/*.so*"]),
)

exports_files(glob(["usr/share/doc/**"]))
