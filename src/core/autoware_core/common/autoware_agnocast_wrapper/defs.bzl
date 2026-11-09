"""autoware_agnocast_wrapper_register_node: Bazel counterpart of the CMake macro of the same name.

cmake/autoware_agnocast_wrapper_register_node.cmake, in its ENABLE_AGNOCAST=1 branch (Agnocast is always compiled
in, see docs/bazel/PLAN.md): generates the node executable's main() from templates/node_main_switchable.cpp.in,
registers the component with rclcpp_components, and registers the ament resource
`autoware_node_plugins/<package>__<executable>` = "<plugin>;1" that the AutowareNode launch action reads.

Targets created for executable E:
  E                          the node executable
  E_component_record         rclcpp_components registration (put it in ament_index(components = [...]))
  E_autoware_node_plugin     the ament resource (put it in ament_index(deps = [...]))
"""

load("@rclcpp_components//:defs.bzl", "rclcpp_components_register_node")
load("@rosdistro//ament:defs.bzl", "register_ament_resource")
load("@rosdistro//cc:defs.bzl", "collect_transitive_dynamic_deps")
load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@rules_cc//cc/common:cc_shared_library_info.bzl", "CcSharedLibraryInfo")

_ROS2_EXECUTORS = {
    "MultiThreadedExecutor": "rclcpp::executors::MultiThreadedExecutor",
    "SingleThreadedExecutor": "rclcpp::executors::SingleThreadedExecutor",
}

# AGNOCAST_EXECUTOR -> (include, type, add_node expression, agnocast_only), as in the CMake macro.
_AGNOCAST_EXECUTORS = {
    "AgnocastOnlyCallbackIsolatedExecutor": (
        "agnocast/node/agnocast_only_callback_isolated_executor.hpp",
        "agnocast::AgnocastOnlyCallbackIsolatedExecutor",
        "get_agnocast_node()",
        "true",
    ),
    "AgnocastOnlyMultiThreadedExecutor": (
        "agnocast/node/agnocast_only_multi_threaded_executor.hpp",
        "agnocast::AgnocastOnlyMultiThreadedExecutor",
        "get_agnocast_node()",
        "true",
    ),
    "AgnocastOnlySingleThreadedExecutor": (
        "agnocast/node/agnocast_only_single_threaded_executor.hpp",
        "agnocast::AgnocastOnlySingleThreadedExecutor",
        "get_agnocast_node()",
        "true",
    ),
    "CallbackIsolatedAgnocastExecutor": (
        "agnocast/agnocast_callback_isolated_executor.hpp",
        "agnocast::CallbackIsolatedAgnocastExecutor",
        "get_node_base_interface()",
        "false",
    ),
    "MultiThreadedAgnocastExecutor": (
        "agnocast/agnocast_multi_threaded_executor.hpp",
        "agnocast::MultiThreadedAgnocastExecutor",
        "get_node_base_interface()",
        "false",
    ),
    "SingleThreadedAgnocastExecutor": (
        "agnocast/agnocast_single_threaded_executor.hpp",
        "agnocast::SingleThreadedAgnocastExecutor",
        "get_node_base_interface()",
        "false",
    ),
}

def _node_main_impl(ctx):
    library = ctx.attr.component[CcSharedLibraryInfo].linker_input.libraries[0].dynamic_library
    include, executor_type, add_node_expr, agnocast_only = _AGNOCAST_EXECUTORS[ctx.attr.agnocast_executor]
    ctx.actions.expand_template(
        template = ctx.file._template,
        output = ctx.outputs.out,
        substitutions = {
            "@_AWR_agnocast_add_node_expr@": add_node_expr,
            "@_AWR_agnocast_executor_include@": include,
            "@_AWR_agnocast_executor_type@": executor_type,
            "@_AWR_agnocast_only@": agnocast_only,
            "@_AWR_component@": ctx.attr.plugin,
            "@_AWR_library_name@": library.basename,
            "@_AWR_node@": ctx.attr.executable,
            "@_AWR_ros2_executor_type@": _ROS2_EXECUTORS[ctx.attr.ros2_executor],
        },
    )

_node_main = rule(
    implementation = _node_main_impl,
    attrs = {
        "agnocast_executor": attr.string(mandatory = True, values = _AGNOCAST_EXECUTORS.keys()),
        "component": attr.label(mandatory = True, providers = [CcSharedLibraryInfo]),
        "executable": attr.string(mandatory = True),
        "out": attr.output(mandatory = True),
        "plugin": attr.string(mandatory = True),
        "ros2_executor": attr.string(mandatory = True, values = _ROS2_EXECUTORS.keys()),
        "_template": attr.label(
            default = Label("//:templates/node_main_switchable.cpp.in"),
            allow_single_file = True,
        ),
    },
)

def autoware_agnocast_wrapper_register_node(
        component,
        plugin,
        executable,
        ros2_executor = "SingleThreadedExecutor",
        agnocast_executor = "SingleThreadedAgnocastExecutor",
        data = [],
        visibility = None):
    """See the module docstring. `component` is the package's cc_shared_library holding `plugin`."""
    _node_main(
        name = executable + "_main",
        out = executable + "_main.cpp",
        agnocast_executor = agnocast_executor,
        component = component,
        executable = executable,
        plugin = plugin,
        ros2_executor = ros2_executor,
    )
    collect_transitive_dynamic_deps(
        name = executable + "_dynamic_deps",
        dynamic_deps = [
            component,
            Label("//:autoware_agnocast_wrapper"),
            Label("@agnocastlib//:agnocast"),
            Label("@class_loader//:class_loader"),
            Label("@rclcpp//:rclcpp"),
            Label("@rclcpp_components//:rclcpp_components"),
            Label("@rosdistro//cc:glog"),
        ],
    )
    cc_binary(
        name = executable,
        srcs = [executable + "_main.cpp"],
        # Same C++ standard as the Autoware packages that use this macro (autoware_package()).
        cxxopts = ["-std=c++17"],
        data = data,
        dynamic_deps = [executable + "_dynamic_deps"],
        visibility = visibility,
        deps = [
            Label("//:autoware_agnocast_wrapper_library"),
            Label("@agnocastlib//:agnocast_library"),
            Label("@class_loader//:class_loader_library"),
            Label("@rclcpp//:rclcpp_library"),
            Label("@rclcpp_components//:rclcpp_components_library"),
            Label("@rosdistro//cc:glog_library"),
        ],
    )

    # rclcpp_components_register_nodes(<target> <plugin>) in CMake: the component record only, no executable.
    rclcpp_components_register_node(
        name = executable + "_component",
        component = component,
        plugin = plugin,
    )
    register_ament_resource(
        name = executable + "_autoware_node_plugin",
        category = "autoware_node_plugins",
        content = plugin + ";1",
        package_name = native.module_name() + "__" + executable,
    )
