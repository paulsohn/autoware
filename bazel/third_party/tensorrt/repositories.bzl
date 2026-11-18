"""Fetch only the pinned x86 SDK payloads, without installing Debian packages."""

load("//:pins.bzl", "TENSORRT_DEBS")

def _artifacts_impl(ctx):
    for i, (url, sha256) in enumerate(TENSORRT_DEBS):
        temporary = "deb_" + str(i)
        ctx.download_and_extract(url = url, sha256 = sha256, type = "ar", output = temporary)
        payload = [f for f in ctx.path(temporary).readdir() if f.basename.startswith("data.tar.")]
        if len(payload) != 1:
            fail("Expected exactly one Debian data archive: " + url)
        ctx.extract(payload[0])
        ctx.delete(temporary)
    ctx.template("BUILD.bazel", ctx.attr._build)

_artifacts = repository_rule(
    implementation = _artifacts_impl,
    attrs = {"_build": attr.label(default = "//:BUILD.sdk.bazel")},
)

def _sdk_impl(_ctx):
    _artifacts(name = "tensorrt_artifacts")

sdk = module_extension(implementation = _sdk_impl)
