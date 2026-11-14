"""Analysis-time collection of the ROS interfaces available to Agnocast."""

load("@rosidl_default_generators//:defs.bzl", "RosInterfaceInfo")

RosInterfaceSetInfo = provider(
    "The deterministic ROS interface closure of a product-owned root set.",
    fields = {
        "descriptors": "depset of package/kind/Name strings",
        "srcs": "depset of source interface files",
    },
)

def _interface_descriptor(info):
    src = info.src
    kind = src.dirname.rsplit("/", 1)[-1]
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
        for root in ctx.attr.package_roots
    ]
    transitive_srcs = [
        root[RosInterfaceSetInfo].srcs
        for root in ctx.attr.package_roots
    ]
    descriptors = depset(transitive = transitive_descriptors)
    srcs = depset(transitive = transitive_srcs)
    sorted_descriptors = sorted(descriptors.to_list())

    missing = [
        interface
        for interface in ctx.attr.required_interfaces
        if interface not in sorted_descriptors
    ]
    if missing:
        fail("required ROS interfaces are absent: {}".format(", ".join(missing)))
    if ctx.attr.expected_count and len(sorted_descriptors) != ctx.attr.expected_count:
        fail("expected {} ROS interfaces, collected {}".format(
            ctx.attr.expected_count,
            len(sorted_descriptors),
        ))

    manifest = ctx.actions.declare_file(ctx.label.name + ".interfaces")
    ctx.actions.write(manifest, "\n".join(sorted_descriptors) + "\n")
    return [
        DefaultInfo(files = depset([manifest])),
        RosInterfaceSetInfo(
            descriptors = descriptors,
            srcs = srcs,
        ),
    ]

ros_interface_set = rule(
    implementation = _ros_interface_set_impl,
    attrs = {
        "expected_count": attr.int(),
        "package_roots": attr.label_list(
            aspects = [_ros_interface_closure_aspect],
            mandatory = True,
        ),
        "required_interfaces": attr.string_list(),
    },
)
