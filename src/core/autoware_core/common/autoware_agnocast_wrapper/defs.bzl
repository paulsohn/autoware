"""Bazel entry point for callback-isolated nodes with a ROS executor fallback."""

load("@rclcpp_components//:defs.bzl", "rclcpp_components_register_nodes")
load("@rosdistro//ament:defs.bzl", "register_ament_resource")
load("@rosdistro//cc:defs.bzl", "collect_transitive_dynamic_deps")
load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@rules_cc//cc/common:cc_shared_library_info.bzl", "CcSharedLibraryInfo")

def _main_impl(ctx):
    library = ctx.attr.component[CcSharedLibraryInfo].linker_input.libraries[0].dynamic_library
    output = ctx.actions.declare_file(ctx.label.name + ".cpp")
    if ctx.attr.agnocast_only:
        executor_include = "agnocast/node/agnocast_only_callback_isolated_executor.hpp"
        executor_type = "agnocast::AgnocastOnlyCallbackIsolatedExecutor"
    else:
        executor_include = "agnocast/agnocast_callback_isolated_executor.hpp"
        executor_type = "agnocast::CallbackIsolatedAgnocastExecutor"
    ctx.actions.expand_template(
        template = ctx.file._template,
        output = output,
        substitutions = {
            "@_AWR_node@": ctx.attr.node_name,
            "@_AWR_component@": ctx.attr.plugin,
            "@_AWR_library_name@": library.basename,
            "@_AWR_agnocast_only@": "true" if ctx.attr.agnocast_only else "false",
            "@_AWR_agnocast_executor_include@": executor_include,
            "@_AWR_agnocast_executor_type@": executor_type,
            "@_AWR_agnocast_add_node_expr@": "get_agnocast_node()" if ctx.attr.agnocast_only else "get_node_base_interface()",
            "@_AWR_ros2_executor_type@": "rclcpp::executors::" + ctx.attr.ros2_executor,
        },
    )
    return [DefaultInfo(files = depset([output]))]

_node_main = rule(
    implementation = _main_impl,
    attrs = {
        "component": attr.label(mandatory = True, providers = [CcSharedLibraryInfo]),
        "agnocast_only": attr.bool(default = True),
        "plugin": attr.string(mandatory = True),
        "node_name": attr.string(mandatory = True),
        "ros2_executor": attr.string(
            default = "SingleThreadedExecutor",
            values = ["SingleThreadedExecutor", "MultiThreadedExecutor"],
        ),
        "_template": attr.label(
            default = Label(":templates/node_main_switchable.cpp.in"),
            allow_single_file = True,
        ),
    },
)

def autoware_agnocast_node(name, component, plugin, package_name, data = [], ros2_executor = "SingleThreadedExecutor", agnocast_only = True, binary_rule = cc_binary):
    """Use callback-isolated execution with the selected ROS executor fallback.

    Set agnocast_only=False for rclcpp::Node components, matching the ROS-aware
    CallbackIsolatedAgnocastExecutor. The default requires wrapper::Node.
    Set binary_rule for a command that needs a declared native SDK launcher.
    Emits <name>_components and <name>_plugin for the caller's ament index.
    """
    rclcpp_components_register_nodes(
        name = name + "_components",
        component = component,
        plugins = [plugin],
    )
    register_ament_resource(
        name = name + "_plugin",
        category = "autoware_node_plugins",
        package_name = package_name + "__" + name,
        content = plugin + ";1",
    )
    _node_main(
        name = name + "_main",
        node_name = name,
        ros2_executor = ros2_executor,
        agnocast_only = agnocast_only,
        component = component,
        plugin = plugin,
    )
    collect_transitive_dynamic_deps(
        name = name + "_dynamic_deps",
        dynamic_deps = [component, Label("@rosdistro//cc:glog")],
    )
    binary_rule(
        name = name,
        srcs = [":" + name + "_main"],
        data = data,
        dynamic_deps = [":" + name + "_dynamic_deps"],
        deps = [
            Label(":autoware_agnocast_wrapper_library"),
            Label("@agnocastlib//:agnocast_library"),
            Label("@class_loader//:class_loader_library"),
            Label("@rclcpp_components//:rclcpp_components_library"),
            Label("@rosdistro//cc:glog_library"),
        ],
    )
