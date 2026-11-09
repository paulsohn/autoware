"""heaphook_test: a cc test run with libagnocast_heaphook.so preloaded, as CMake does for Agnocast tests.

CMake sets ENVIRONMENT "LD_PRELOAD=<prefix>/lib/libagnocast_heaphook.so" on such tests. Here a launcher sets
LD_PRELOAD to the runfiles paths just before exec'ing the C++ test, so that wrappers in front of it (e.g. the RCR's
`--run_under` test isolation, a Python process) do not get the hook preloaded.
"""

load("@rules_cc//cc:defs.bzl", "cc_binary")

def _runfiles_path(file):
    # short_path is "../<repo>/<path>" for external files and "<path>" for the main repo.
    return file.short_path[3:] if file.short_path.startswith("../") else "_main/" + file.short_path

def _heaphook_test_impl(ctx):
    preloads = [f for t in ctx.attr.preload for f in t[DefaultInfo].files.to_list() if f.basename.endswith(".so")]
    launcher = ctx.actions.declare_file(ctx.label.name + ".sh")
    ctx.actions.write(
        output = launcher,
        is_executable = True,
        content = "\n".join([
            "#!/bin/bash",
            "set -euo pipefail",
            'R="${RUNFILES_DIR:-$0.runfiles}"',
            'export LD_PRELOAD="' + ":".join(['$R/' + _runfiles_path(f) for f in preloads]) +
            '${LD_PRELOAD:+:$LD_PRELOAD}"',
            'exec "$R/' + _runfiles_path(ctx.executable.binary) + '" "$@"',
            "",
        ]),
    )
    runfiles = ctx.runfiles(files = preloads).merge(ctx.attr.binary[DefaultInfo].default_runfiles)
    for t in ctx.attr.preload:
        runfiles = runfiles.merge(t[DefaultInfo].default_runfiles)
    return [
        DefaultInfo(executable = launcher, runfiles = runfiles),
        RunEnvironmentInfo(environment = ctx.attr.env),
    ]

_heaphook_test = rule(
    implementation = _heaphook_test_impl,
    test = True,
    attrs = {
        "binary": attr.label(executable = True, cfg = "target", mandatory = True),
        "preload": attr.label_list(allow_files = True),
        "env": attr.string_dict(),
    },
)

def heaphook_test(name, preload = [], env = {}, tags = [], size = None, timeout = None, **kwargs):
    """A cc test (kwargs as for cc_binary) run with LD_PRELOAD=libagnocast_heaphook.so (+ `preload`, before it)."""
    cc_binary(
        name = name + "_bin",
        testonly = True,
        tags = tags + ["manual"],
        **kwargs
    )
    _heaphook_test(
        name = name,
        binary = ":" + name + "_bin",
        preload = preload + [Label("//:agnocast_heaphook")],
        env = env,
        tags = tags,
        size = size,
        timeout = timeout,
    )
