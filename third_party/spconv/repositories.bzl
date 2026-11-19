"""Pinned sparse-convolution SDK payloads; no host package installation."""

_DEBS = [
    ("cumm_0.5.3_amd64.deb", "30c776917c8082a5c54264293b9dbdfae15c858c390b99b4d55e61d82ade1833"),
    ("spconv_2.3.8_amd64.deb", "24e08b4af087059dc5758627303356b441bd5e9296a2d25876f2a77c20acb0ff"),
]
_BASE = "https://github.com/autowarefoundation/spconv_cpp/releases/download/spconv_v2.3.8%2Bcumm_v0.5.3%2Bcu130-rev1/"

def _artifacts_impl(ctx):
    for i, (name, sha256) in enumerate(_DEBS):
        temporary = "deb_" + str(i)
        ctx.download_and_extract(url = _BASE + name, sha256 = sha256, type = "ar", output = temporary)
        payload = [p for p in ctx.path(temporary).readdir() if p.basename.startswith("data.tar.")]
        if len(payload) != 1:
            fail("Expected one data archive: " + name)
        ctx.extract(payload[0])
        ctx.delete(temporary)
    ctx.patch(ctx.attr._patch, strip = 1)
    ctx.template("BUILD.bazel", ctx.attr._build)

_artifacts = repository_rule(
    implementation = _artifacts_impl,
    attrs = {
        "_build": attr.label(default = "//:BUILD.sdk.bazel"),
        "_patch": attr.label(default = "//:patches/tensorview_clang_lambda.patch"),
    },
)

def _sdk_impl(_ctx):
    _artifacts(name = "spconv_artifacts")

sdk = module_extension(implementation = _sdk_impl)
