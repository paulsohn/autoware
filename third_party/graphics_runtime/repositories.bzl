"""Fetch an immutable GLVND runtime closure without a package manager."""

def _graphics_runtime_impl(ctx):
    archives = json.decode(ctx.read(ctx.attr.archives))
    for archive in archives:
        directory = ".archives/" + archive["Package"]
        ctx.download_and_extract(
            url = "https://archive.ubuntu.com/ubuntu/" + archive["Filename"],
            sha256 = archive["SHA256"],
            output = directory,
            type = "deb",
        )
        payloads = [p for p in ctx.path(directory).readdir() if p.basename.startswith("data.tar.")]
        if len(payloads) != 1:
            fail("Expected one data archive in " + archive["Package"])
        ctx.extract(payloads[0])
        ctx.delete(directory)
    ctx.file("BUILD.bazel", """
package(default_visibility = ["//visibility:public"])
filegroup(name = "runtime", srcs = glob(["usr/lib/x86_64-linux-gnu/*.so*"]))
filegroup(name = "copyrights", srcs = glob(["usr/share/doc/*/copyright"]))
""")

graphics_runtime = repository_rule(
    implementation = _graphics_runtime_impl,
    attrs = {
        "archives": attr.label(default = Label(":archives.json"), allow_single_file = True),
    },
)
