"""Expose Python runtime providers reachable through a product's runtime edges."""

load("@rules_python//python:py_info.bzl", "PyInfo")

RuntimePythonInfo = provider(fields = ["python"])

_RUNTIME_EDGES = ["deps", "data", "srcs", "libraries", "executables", "components", "dynamic_deps"]
_PYTHON_DEPSETS = [
    "transitive_sources",
    "imports",
    "transitive_pyc_files",
    "transitive_implicit_pyc_files",
    "transitive_implicit_pyc_source_files",
    "transitive_original_sources",
    "transitive_pyi_files",
    "venv_symlinks",
]

def _merge_python(infos):
    return PyInfo(
        uses_shared_libraries = any([info.uses_shared_libraries for info in infos]),
        has_py2_only_sources = any([info.has_py2_only_sources for info in infos]),
        has_py3_only_sources = any([info.has_py3_only_sources for info in infos]),
        **{
            field: depset(transitive = [getattr(info, field, depset()) for info in infos])
            for field in _PYTHON_DEPSETS
        }
    )

def _runtime_python_aspect_impl(target, ctx):
    infos = [target[PyInfo]] if PyInfo in target else []
    if ctx.rule:
        for name in _RUNTIME_EDGES:
            value = getattr(ctx.rule.attr, name, [])
            for dep in value if type(value) == "list" else [value]:
                if type(dep) == "Target" and RuntimePythonInfo in dep:
                    info = dep[RuntimePythonInfo].python
                    if info != None:
                        infos.append(info)
    return [RuntimePythonInfo(python = _merge_python(infos) if infos else None)]

_runtime_python_aspect = aspect(
    implementation = _runtime_python_aspect_impl,
    attr_aspects = _RUNTIME_EDGES,
)

def _runtime_python_impl(ctx):
    info = _merge_python([
        root[RuntimePythonInfo].python
        for root in ctx.attr.roots
        if root[RuntimePythonInfo].python != None
    ])
    runfiles = ctx.runfiles(transitive_files = depset(
        transitive = [info.transitive_sources] + [root[DefaultInfo].files for root in ctx.attr.roots],
    )).merge_all([root[DefaultInfo].default_runfiles for root in ctx.attr.roots])

    manifest = ctx.actions.declare_file(ctx.label.name + ".runtime.json")
    ctx.actions.write(manifest, json.encode_indent({
        "roots": [str(root.label) for root in ctx.attr.roots],
        "symlinks": sorted({entry.path: True for root in ctx.attr.roots for entry in root[DefaultInfo].default_runfiles.symlinks.to_list()}.keys()),
        "root_symlinks": sorted({entry.path: True for root in ctx.attr.roots for entry in root[DefaultInfo].default_runfiles.root_symlinks.to_list()}.keys()),
    }) + "\n")
    return [
        info,
        DefaultInfo(runfiles = runfiles),
        OutputGroupInfo(manifest = depset([manifest])),
    ]

runtime_python = rule(
    implementation = _runtime_python_impl,
    attrs = {"roots": attr.label_list(mandatory = True, aspects = [_runtime_python_aspect])},
    provides = [PyInfo],
)
