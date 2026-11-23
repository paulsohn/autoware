"""Native programs with declared TensorRT runtime discovery."""

load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@rules_python//python:defs.bzl", "py_binary", "py_test")
load("//:pins.bzl", "TENSORRT_VERSION")

TENSORRT_VERSION_MAJOR = TENSORRT_VERSION.split(".")[0]

def _launcher_impl(ctx):
    paths = []
    for file in [ctx.file.binary] + ctx.files.runtime_files:
        path = file.short_path
        paths.append(path[3:] if path.startswith("../") else ctx.workspace_name + "/" + path)
    ctx.actions.expand_template(
        template = ctx.file.template,
        output = ctx.outputs.out,
        substitutions = {"_BAKED_RUNTIME = None": "_BAKED_RUNTIME = " + repr(paths)},
    )

_runtime_launcher = rule(
    implementation = _launcher_impl,
    attrs = {
        "binary": attr.label(allow_single_file = True, mandatory = True),
        "runtime_files": attr.label_list(allow_files = True),
        "template": attr.label(allow_single_file = True, default = Label("//:test_runner.py")),
    },
    outputs = {"out": "%{name}.py"},
)

def _native_program(name, srcs, deps, data, local_defines, dynamic_deps, libstdcxx_runtime, run_under, timeout, tags, is_test):
    """Make bare-name builder dlopen portable for tests and ordinary executables."""
    binary = name + "_binary"
    cc_binary(
        name = binary,
        testonly = is_test,
        tags = tags,
        srcs = srcs,
        deps = deps,
        dynamic_deps = dynamic_deps,
        data = data,
        local_defines = local_defines,
    )
    runtime_files = [
        Label("@tensorrt_artifacts//:usr/lib/x86_64-linux-gnu/libnvinfer.so.10"),
        libstdcxx_runtime or Label("@tensorrt_libstdcxx//:lib/libstdc++.so.6.0.30"),
        Label("@tensorrt_libgcc//:lib/libgcc_s.so.1"),
    ]
    runner_args = ["--run-under", "$(rlocationpath " + str(run_under) + ")"] if run_under else []
    runner_data = [run_under] if run_under else []
    main = Label("//:test_runner.py")
    args = ["$(rlocationpath :" + binary + ")"] + ["$(rlocationpath " + str(p) + ")" for p in runtime_files] + runner_args
    if not is_test:
        # args is only applied by bazel run/test, not by ros2 run or a subprocess.
        _runtime_launcher(name = name + "_sdk", binary = ":" + binary, runtime_files = runtime_files)
        main = ":" + name + "_sdk.py"
        args = []
    wrapper = py_test if is_test else py_binary
    wrapper(
        name = name,
        srcs = [main],
        main = main,
        args = args,
        data = [":" + binary, Label("//:runtime_libs")] + runtime_files + runner_data,
        deps = [Label("@rules_python//python/runfiles")],
        tags = ["no-remote", "no-sandbox", "requires-gpu"] + tags,
        precompile = "disabled",
        **({"timeout": timeout} if is_test else {})
    )

def tensorrt_cc_test(name, srcs, deps, data = [], local_defines = [], dynamic_deps = [], libstdcxx_runtime = None, run_under = None, timeout = "moderate", tags = []):
    """Run a native suite with the declared SDK and GNU runtime."""
    _native_program(name, srcs, deps, data, local_defines, dynamic_deps, libstdcxx_runtime, run_under, timeout, tags, True)

def tensorrt_cc_binary(name, srcs = [], deps = [], data = [], dynamic_deps = [], local_defines = [], tags = []):
    """Run a native preparation tool with the same SDK discovery as native tests."""
    _native_program(name, srcs, deps, data, local_defines, dynamic_deps, None, None, None, tags, False)
