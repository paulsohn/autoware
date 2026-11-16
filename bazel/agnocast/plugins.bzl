"""Generate and link Agnocast bridge plugins from an analysis-time interface set."""

load("@rosdistro//cc:defs.bzl", "collect_transitive_dynamic_deps", "ros_local_defines")
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_shared_library")
load("//bazel/interfaces:interfaces.bzl", "RosInterfaceSetInfo")

def _header_path(descriptor):
    package, kind, name = descriptor.split("/")

    # ROS service-event messages are declared by the service's own header.
    name = name.removesuffix("_Event")
    snake = ""
    for index in range(len(name)):
        char = name[index]
        if index and char.isupper() and (
            name[index - 1].islower() or name[index - 1].isdigit() or
            (name[index - 1].isupper() and index + 1 < len(name) and name[index + 1].islower())
        ):
            snake += "_"
        snake += char.lower()
    return package + "/" + kind + "/" + snake + ".hpp"

def _template_tests_impl(ctx):
    for descriptor, expected in {
        "std_msgs/msg/String": "std_msgs/msg/string.hpp",
        "autoware_common_msgs/msg/ResponseStatus": "autoware_common_msgs/msg/response_status.hpp",
        "demo/msg/GNSSFix": "demo/msg/gnss_fix.hpp",
        "demo/msg/UInt16": "demo/msg/u_int16.hpp",
        "std_srvs/srv/SetBool_Event": "std_srvs/srv/set_bool.hpp",
    }.items():
        actual = _header_path(descriptor)
        if actual != expected:
            fail("{}: expected {}, got {}".format(descriptor, expected, actual))
    executable = ctx.actions.declare_file(ctx.label.name + ".sh")
    ctx.actions.write(executable, "#!/bin/sh\nexit 0\n", is_executable = True)
    return [DefaultInfo(executable = executable)]

bridge_template_test = rule(implementation = _template_tests_impl, test = True)

def _bridge_sources_impl(ctx):
    info = ctx.attr.interface_set[RosInterfaceSetInfo]
    descriptors = sorted(info.descriptors.to_list())
    if not descriptors:
        fail("bridge interface set must not be empty")
    expanded = {}
    for descriptor in descriptors:
        kind = descriptor.split("/")[1]
        if kind not in ["msg", "srv"]:
            fail("unsupported bridge interface: " + descriptor)
        expanded[descriptor] = True
        if kind == "srv":
            expanded[descriptor + "_Event"] = True
    directory = ctx.label.name + "_generated"
    outputs = []
    for descriptor in sorted(expanded):
        is_service = descriptor.split("/")[1] == "srv" and not descriptor.endswith("_Event")
        prefix = "service" if is_service else "pubsub"
        output = ctx.actions.declare_file(
            directory + "/" + prefix + "_bridge_plugin_" + descriptor.replace("/", "_") + ".cpp",
        )
        ctx.actions.expand_template(
            template = ctx.file._service_template if is_service else ctx.file._message_template,
            output = output,
            substitutions = {
                "@(msg_type)": descriptor,
                "@(srv_type)": descriptor,
                "@(cpp_type)": descriptor.replace("/", "::"),
                "@(header_path)": _header_path(descriptor),
                "@(snake_type_name)": descriptor.replace("/", "_"),
            },
        )
        outputs.append(output)
    for source in [ctx.file._generic_cpp, ctx.file._generic_hpp]:
        output = ctx.actions.declare_file(directory + "/" + source.basename)
        ctx.actions.symlink(output = output, target_file = source)
        outputs.append(output)
    return [DefaultInfo(files = depset(outputs))]

_bridge_sources = rule(
    implementation = _bridge_sources_impl,
    attrs = {
        "interface_set": attr.label(providers = [RosInterfaceSetInfo], mandatory = True),
        "_message_template": attr.label(default = Label("@ros2agnocast//:ros2agnocast/templates/pubsub_bridge_plugin.cpp.em"), allow_single_file = True),
        "_service_template": attr.label(default = Label("@ros2agnocast//:ros2agnocast/templates/service_bridge_plugin.cpp.em"), allow_single_file = True),
        "_generic_cpp": attr.label(default = Label("@ros2agnocast//:ros2agnocast/static_sources/generic_functions.cpp"), allow_single_file = True),
        "_generic_hpp": attr.label(default = Label("@ros2agnocast//:ros2agnocast/static_sources/generic_functions.hpp"), allow_single_file = True),
    },
)

def agnocast_bridge_plugins(name, interface_set):
    """Compile collected interfaces using the set's C++ and shared providers."""
    _bridge_sources(name = name + "_sources", interface_set = interface_set)
    cc_library(
        name = name + "_library",
        srcs = [":" + name + "_sources"],
        local_defines = ros_local_defines(),
        deps = [interface_set, Label("@agnocastlib//:agnocast_library"), Label("@rclcpp//:rclcpp_library")],
    )
    collect_transitive_dynamic_deps(
        name = name + "_dynamic_deps",
        dynamic_deps = [interface_set, Label("@agnocastlib//:agnocast"), Label("@rclcpp//:rclcpp")],
    )
    cc_shared_library(
        name = name,
        shared_lib_name = "libagnocast_bridge_plugins.so",
        deps = [":" + name + "_library"],
        dynamic_deps = [":" + name + "_dynamic_deps"],
    )
