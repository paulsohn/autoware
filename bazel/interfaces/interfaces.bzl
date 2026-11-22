"""Consumer-independent analysis-time collection of ROS interface closures."""

load("@rosdistro//cc:defs.bzl", "collect_transitive_dynamic_deps")
load("@rosidl_default_generators//:defs.bzl", "RosInterfaceInfo")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")
load("@rules_cc//cc/common:cc_shared_library_info.bzl", "CcSharedLibraryInfo")
load("@rules_python//python:defs.bzl", "py_library")

RosInterfaceSetInfo = provider(
    "The deterministic ROS interface closure of a product-owned root set.",
    fields = {
        "descriptors": "depset of package/kind/Name strings",
        "srcs": "depset of source interface files",
    },
)

def _interface_descriptor(info):
    src = info.src

    # Source subdirectories do not change the generated ROS namespace.
    kind = src.extension
    name = src.basename.removesuffix("." + src.extension)
    return "{}/{}/{}".format(info.package, kind, name)

def _ros_interface_closure_aspect_impl(target, ctx):
    transitive_descriptors = []
    transitive_srcs = []
    if hasattr(ctx.rule.attr, "deps"):
        for dep in ctx.rule.attr.deps:
            if RosInterfaceSetInfo in dep:
                transitive_descriptors.append(dep[RosInterfaceSetInfo].descriptors)
                transitive_srcs.append(dep[RosInterfaceSetInfo].srcs)

    direct_descriptors = []
    direct_srcs = []
    if RosInterfaceInfo in target:
        info = target[RosInterfaceInfo]
        direct_descriptors.append(_interface_descriptor(info))
        direct_srcs.append(info.src)

    return [RosInterfaceSetInfo(
        descriptors = depset(
            direct = direct_descriptors,
            transitive = transitive_descriptors,
        ),
        srcs = depset(
            direct = direct_srcs,
            transitive = transitive_srcs,
        ),
    )]

_ros_interface_closure_aspect = aspect(
    implementation = _ros_interface_closure_aspect_impl,
    attr_aspects = ["deps"],
)

def _ros_interface_set_impl(ctx):
    transitive_descriptors = [
        root[RosInterfaceSetInfo].descriptors
        for root in ctx.attr.roots
    ]
    transitive_srcs = [
        root[RosInterfaceSetInfo].srcs
        for root in ctx.attr.roots
    ]
    descriptors = depset(transitive = transitive_descriptors)
    srcs = depset(transitive = transitive_srcs)
    sorted_descriptors = sorted(descriptors.to_list())

    manifest = ctx.actions.declare_file(ctx.label.name + ".interfaces")
    ctx.actions.write(manifest, "\n".join(sorted_descriptors) + "\n")
    return [
        DefaultInfo(
            files = depset([manifest]),
            runfiles = ctx.runfiles(files = [manifest]).merge(
                ctx.attr.runtime[DefaultInfo].default_runfiles,
            ),
        ),
        cc_common.merge_cc_infos(cc_infos = [dep[CcInfo] for dep in ctx.attr.cc_deps]),
        ctx.attr.runtime[CcSharedLibraryInfo],
        RosInterfaceSetInfo(
            descriptors = descriptors,
            srcs = srcs,
        ),
    ]

_ros_interface_set = rule(
    implementation = _ros_interface_set_impl,
    provides = [RosInterfaceSetInfo, CcInfo, CcSharedLibraryInfo],
    attrs = {
        "cc_deps": attr.label_list(providers = [CcInfo], mandatory = True),
        "runtime": attr.label(providers = [CcSharedLibraryInfo], mandatory = True),
        "roots": attr.label_list(
            doc = "Build targets whose transitive deps supply ROS interface metadata.",
            aspects = [_ros_interface_closure_aspect],
            mandatory = True,
        ),
    },
)

def ros_interface_package(cc, shared, roots = None, python = None, index = None):
    """Pair declared C++/shared providers with the selected interface roots.

    By default select the package's entire cc interface closure. Explicit roots
    can select individual interfaces without narrowing its compiled provider.
    """
    if not str(cc).endswith(":cc") and (python == None or index == None):
        fail("Nonstandard C++ interface targets must declare Python and index targets explicitly")
    base = str(cc).removesuffix(":cc")
    return struct(
        cc = cc,
        shared = shared,
        roots = [cc] if roots == None else roots,
        python = python if python != None else base + ":py",
        index = index if index != None else base + ":ament_index",
    )

def ros_interface_set(name, packages, roots = None, **kwargs):
    """Expose interface metadata, CcInfo and shared runtime from one declaration.

    Optional roots may select application targets instead; packages still
    declares the providers needed to compile consumers of their interface set.
    No dependency labels are inferred from analysis-time interface names.
    """
    collect_transitive_dynamic_deps(
        name = name + "_runtime",
        dynamic_deps = [package.shared for package in packages],
        visibility = ["//visibility:private"],
    )
    py_library(
        name = name + "_python",
        deps = [package.python for package in packages],
        data = [package.index for package in packages],
        visibility = kwargs.get("visibility"),
    )
    _ros_interface_set(
        name = name,
        roots = [root for package in packages for root in package.roots] if roots == None else roots,
        cc_deps = [package.cc for package in packages],
        runtime = ":" + name + "_runtime",
        **kwargs
    )
