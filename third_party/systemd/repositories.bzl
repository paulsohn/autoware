"""Materialize pinned Ubuntu systemd payloads without package installation."""

def _systemd_runtime_impl(ctx):
    rules = ["""
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")
load("@rules_python//python:defs.bzl", "py_binary")
package(default_visibility = ["//visibility:public"])
py_binary(name = "payload_tar", srcs = ["payload_tar.py"], python_version = "3.14")
"""]
    tar_targets = []
    for archive in json.decode(ctx.read(ctx.attr.archives)):
        directory = "payloads/" + archive["Package"]
        ctx.download_and_extract(
            url = [
                "https://mirrors.edge.kernel.org/ubuntu/" + archive["Filename"],
                "https://security.ubuntu.com/ubuntu/" + archive["Filename"],
            ],
            sha256 = archive["SHA256"],
            output = directory,
            type = "deb",
        )
        payload = [p for p in ctx.path(directory).readdir() if p.basename.startswith("data.tar.")]
        if len(payload) != 1:
            fail("Expected one data archive in " + archive["Package"])
        source = directory + "/" + payload[0].basename
        name = "payload_" + archive["Package"]
        tar_targets.append(":" + name)
        rules.append("""
genrule(
    name = %r,
    srcs = [%r],
    outs = [%r],
    tools = [":payload_tar"],
    cmd = "$(execpath :payload_tar) $(execpath %s) $@",
)
""" % (name, source, name + ".tar", source))
    rules.append("""
pkg_tar(
    name = "runtime",
    deps = %s,
    preserve_mode = True,
    allow_duplicates_with_different_content = False,
    stamp = 0,
    extension = "tar.gz",
)
""" % repr(tar_targets))
    ctx.file("payload_tar.py", ctx.read(ctx.attr.payload_tar))
    ctx.file("BUILD.bazel", "\n".join(rules))

systemd_runtime = repository_rule(
    implementation = _systemd_runtime_impl,
    attrs = {
        "archives": attr.label(default = Label(":archives.json"), allow_single_file = True),
        "payload_tar": attr.label(default = Label(":payload_tar.py"), allow_single_file = True),
    },
)
