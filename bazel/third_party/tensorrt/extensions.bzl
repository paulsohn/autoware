"""One repository per CPU with the pinned TensorRT .debs unpacked (lazy: only fetched for the targeted CPU)."""

load("//:pins.bzl", "TENSORRT_DEBS", "TENSORRT_VERSION")

_TRIPLES = {"aarch64": "aarch64-linux-gnu", "x86_64": "x86_64-linux-gnu"}

def _tensorrt_debs_impl(rctx):
    for i, (url, sha256) in enumerate(TENSORRT_DEBS[rctx.attr.cpu]):
        # A .deb is an ar archive around data.tar.*; Bazel's "deb" type only unpacks the ar layer.
        deb = "_deb%d" % i
        rctx.download_and_extract(url = url, sha256 = sha256, type = "ar", output = deb)
        data = [f for f in rctx.path(deb).readdir() if f.basename.startswith("data.tar")]
        if len(data) != 1:
            fail("{}: expected one data.tar.* in the .deb".format(url))
        rctx.extract(data[0], output = ".")
        rctx.delete(deb)
    rctx.template("BUILD.bazel", rctx.attr._build_tpl, {
        "{triple}": _TRIPLES[rctx.attr.cpu],
        "{version}": TENSORRT_VERSION,
    })

_tensorrt_debs = repository_rule(
    implementation = _tensorrt_debs_impl,
    attrs = {
        "cpu": attr.string(mandatory = True, values = _TRIPLES.keys()),
        "_build_tpl": attr.label(default = "//:BUILD.debs.tpl"),
    },
)

def _tensorrt_paths_impl(rctx):
    # Runfiles-relative (from a test's working directory, <runfiles>/_main) library directory of each per-CPU repo.
    # Computed from this repo's canonical name (same extension, same prefix) so that loading it fetches no .debs.
    prefix = rctx.name.removesuffix("tensorrt_paths")
    rctx.file("BUILD.bazel", "")
    rctx.file("defs.bzl", "".join([
        'LIB_DIR_{} = "../{}tensorrt_{}/usr/lib/{}"\n'.format(cpu.upper(), prefix, cpu, triple)
        for cpu, triple in _TRIPLES.items()
    ]))

_tensorrt_paths = repository_rule(implementation = _tensorrt_paths_impl)

def _tensorrt_impl(_mctx):
    for cpu in _TRIPLES:
        _tensorrt_debs(name = "tensorrt_" + cpu, cpu = cpu)
    _tensorrt_paths(name = "tensorrt_paths")

tensorrt = module_extension(implementation = _tensorrt_impl)
