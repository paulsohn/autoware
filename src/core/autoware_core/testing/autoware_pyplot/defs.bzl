"""pyplot_test: run a C++ test that embeds Python (pybind11::embed) with the hermetic interpreter.

CMake/colcon tests find the system Python and its site-packages. Here the embedded interpreter must be told where
the hermetic runtime (PYTHONHOME) and the pip packages (PYTHONPATH) are in runfiles, so the C++ test binary is
wrapped by a launcher that sets both. bazel/sitecustomize.py makes the interpreter load extension modules with
RTLD_DEEPBIND, because the host binary's libc++ runtime would otherwise capture the wheels' libstdc++ symbols.
"""

load("@rules_python//python:defs.bzl", "PyInfo")
load("@pip_ros//:requirements.bzl", "requirement")
load("@rules_cc//cc:defs.bzl", "cc_binary")

def _runfiles_path(file):
    # short_path is "../<repo>/<path>" for external files and "<path>" for the main repo.
    return file.short_path[3:] if file.short_path.startswith("../") else "_main/" + file.short_path

def _pyplot_test_impl(ctx):
    runtime = ctx.toolchains["@rules_python//python:toolchain_type"].py3_runtime
    interpreter = _runfiles_path(runtime.interpreter)  # <prefix>/bin/python3
    home = interpreter.rsplit("/", 2)[0]
    imports = depset(transitive = [d[PyInfo].imports for d in ctx.attr.python_deps]).to_list()
    imports = [_runfiles_path(ctx.file._sitecustomize).rsplit("/", 1)[0]] + imports

    launcher = ctx.actions.declare_file(ctx.label.name + ".sh")
    ctx.actions.write(
        output = launcher,
        is_executable = True,
        content = "\n".join([
            "#!/bin/bash",
            "set -euo pipefail",
            'R="${RUNFILES_DIR:-$0.runfiles}"',
            'export PYTHONHOME="$R/' + home + '"',
            'export PYTHONPATH="' + ":".join(['$R/' + i for i in imports]) + '"',
            'exec "$R/' + _runfiles_path(ctx.executable.binary) + '" "$@"',
            "",
        ]),
    )
    runfiles = ctx.runfiles(files = [ctx.file._sitecustomize], transitive_files = runtime.files)
    runfiles = runfiles.merge(ctx.attr.binary[DefaultInfo].default_runfiles)
    for d in ctx.attr.python_deps:
        runfiles = runfiles.merge(d[DefaultInfo].default_runfiles)
    return [DefaultInfo(executable = launcher, runfiles = runfiles)]

_pyplot_test = rule(
    implementation = _pyplot_test_impl,
    test = True,
    attrs = {
        "binary": attr.label(executable = True, cfg = "target", mandatory = True),
        "python_deps": attr.label_list(providers = [PyInfo]),
        "_sitecustomize": attr.label(default = "//:bazel/sitecustomize.py", allow_single_file = True),
    },
    toolchains = ["@rules_python//python:toolchain_type"],
)

def pyplot_test(name, python_deps = None, tags = [], size = None, timeout = None, **kwargs):
    """A cc test (kwargs as for cc_binary) run with the hermetic Python runtime and python_deps (default: matplotlib)."""
    if python_deps == None:
        python_deps = [requirement("matplotlib")]
    cc_binary(
        name = name + "_bin",
        testonly = True,
        tags = tags + ["manual"],
        **kwargs
    )
    _pyplot_test(
        name = name,
        binary = ":" + name + "_bin",
        python_deps = python_deps,
        tags = tags,
        size = size,
        timeout = timeout,
    )
