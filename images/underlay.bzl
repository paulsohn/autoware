"""autoware_underlay: the RCR's ament_underlay, with duplicate files replaced by symlinks.

The RCR's ament_package installs, next to each library, every shared library it links (so that a library loaded by
path finds its dependencies). Across hundreds of packages that repeats the same files: libpcl.so and libvtk.so 19
times each, librclcpp.so 93 times. The underlay grew to 4.2 GB. Here, every repeat of a file (the same Bazel file)
becomes a relative symlink to its first occurrence, so each package's lib/ still has every name but the bytes are
stored once.
"""

load("@rosdistro//ament:underlay.bzl", "ament_underlay_files")
load("@rules_pkg//pkg:providers.bzl", "PackageFilegroupInfo", "PackageFilesInfo", "PackageSymlinkInfo")
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")
load("@rules_python//python:defs.bzl", "py_binary")

def _relative(from_dest, to_dest):
    return "../" * from_dest.count("/") + to_dest

def _dedup_impl(ctx):
    src = ctx.attr.src[PackageFilegroupInfo]
    first = {}
    pkg_files = []
    pkg_symlinks = list(src.pkg_symlinks)
    for info, origin in src.pkg_files:
        kept = {}
        for dest in sorted(info.dest_src_map):
            f = info.dest_src_map[dest]
            if f.path in first:
                pkg_symlinks.append((
                    PackageSymlinkInfo(destination = dest, target = _relative(dest, first[f.path]), attributes = {}),
                    origin,
                ))
            else:
                first[f.path] = dest
                kept[dest] = f
        pkg_files.append((PackageFilesInfo(dest_src_map = kept, attributes = info.attributes), origin))
    return [
        PackageFilegroupInfo(pkg_files = pkg_files, pkg_dirs = src.pkg_dirs, pkg_symlinks = pkg_symlinks),
        DefaultInfo(files = ctx.attr.src[DefaultInfo].files),
    ]

_dedup = rule(
    implementation = _dedup_impl,
    attrs = {"src": attr.label(mandatory = True, providers = [PackageFilegroupInfo])},
    provides = [PackageFilegroupInfo],
)

def autoware_underlay(name, deps, visibility = None, **kwargs):
    """<name>.tar: the colcon-style install tree of everything reachable from deps (ament_package targets)."""
    ament_underlay_files(name = name + "_files", deps = deps, visibility = ["//visibility:private"])
    _dedup(name = name + "_dedup", src = ":" + name + "_files", visibility = ["//visibility:private"])
    pkg_tar(name = name + "_paths", srcs = [":" + name + "_dedup"], visibility = ["//visibility:private"], **kwargs)

    # Same content under different Bazel paths (e.g. libX.so, libX.so.N and libX.so.N.M from an archive's symlinks,
    # or one library reached through two repositories): content-based, on the tar.
    native.genrule(
        name = name,
        srcs = [":" + name + "_paths"],
        outs = [name + ".tar"],
        cmd = "$(execpath //images:dedup_tar) $< $@",
        tools = ["//images:dedup_tar"],
        visibility = visibility,
    )

def _python_runtime_files_impl(ctx):
    runtime = ctx.toolchains["@rules_python//python:toolchain_type"].py3_runtime
    interpreter = runtime.interpreter
    # Files of the hermetic runtime repo, at their path relative to its root (bin/python3, lib/python3.12/...).
    root = interpreter.short_path.rsplit("/bin/", 1)[0] + "/"
    dest_src_map = {}
    for f in runtime.files.to_list() + [interpreter]:
        if f.short_path.startswith(root):
            dest_src_map[ctx.attr.prefix + "/" + f.short_path[len(root):]] = f
    return [
        PackageFilegroupInfo(
            pkg_files = [(PackageFilesInfo(dest_src_map = dest_src_map, attributes = {}), ctx.label)],
            pkg_dirs = [],
            pkg_symlinks = [(
                PackageSymlinkInfo(
                    destination = "usr/local/bin/python3",
                    target = "/" + ctx.attr.prefix + "/bin/python3",
                    attributes = {},
                ),
                ctx.label,
            )],
        ),
        DefaultInfo(files = depset(dest_src_map.values())),
    ]

python_runtime_files = rule(
    doc = "The hermetic Python runtime of the target platform (the one Bazel builds against), for an image layer.",
    implementation = _python_runtime_files_impl,
    attrs = {"prefix": attr.string(default = "opt/python3.12")},
    toolchains = ["@rules_python//python:toolchain_type"],
    provides = [PackageFilegroupInfo],
)
