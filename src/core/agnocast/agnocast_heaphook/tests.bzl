"""Run a C++ test with the allocator preloaded only into the final process."""

load("@rules_cc//cc:defs.bzl", "cc_binary")

def _runfile(file):
    path = file.short_path
    return path[3:] if path.startswith("../") else "_main/" + path

def _preloaded_test_impl(ctx):
    executable = ctx.actions.declare_file(ctx.label.name + ".sh")
    preloads = [_runfile(file) for file in ctx.files.preload]
    ctx.actions.write(
        executable,
        "#!/usr/bin/env bash\nset -euo pipefail\n" +
        'root="${RUNFILES_DIR:-${TEST_SRCDIR:-$0.runfiles}}"\n' +
        'export LD_PRELOAD="' + ":".join(["$root/" + path for path in preloads]) + '"\n' +
        'exec "$root/' + _runfile(ctx.executable.binary) + '" "$@"\n',
        is_executable = True,
    )
    runfiles = ctx.runfiles(files = ctx.files.preload)
    for target in [ctx.attr.binary] + ctx.attr.preload:
        runfiles = runfiles.merge(target[DefaultInfo].default_runfiles)
    return [
        DefaultInfo(executable = executable, runfiles = runfiles),
        RunEnvironmentInfo(environment = ctx.attr.env),
    ]

_preloaded_test = rule(
    implementation = _preloaded_test_impl,
    test = True,
    attrs = {
        "binary": attr.label(executable = True, cfg = "target", mandatory = True),
        "preload": attr.label_list(allow_files = True),
        "env": attr.string_dict(),
    },
)

def heaphook_test(name, preload = [], kernel = True, timeout = "moderate", **kwargs):
    """Preserve allocator/mock interposition and the host PID namespace."""
    cc_binary(name = name + "_binary", testonly = True, tags = ["manual"], **kwargs)
    _preloaded_test(
        name = name,
        binary = ":" + name + "_binary",
        preload = preload + [Label("//:agnocast_heaphook")],
        env = {"GTEST_DEATH_TEST_STYLE": "threadsafe"},
        tags = ["no-sandbox", "no-remote", "requires-heaphook"] + (["requires-kernel-module"] if kernel else []),
        timeout = timeout,
    )
