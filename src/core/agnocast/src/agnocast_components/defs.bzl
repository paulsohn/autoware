"""Register Agnocast components and build their upstream standalone mains."""

load("@rclcpp_components//:defs.bzl", "rclcpp_components_register_node")
load("@rosdistro//cc:defs.bzl", "collect_transitive_dynamic_deps")
load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@rules_cc//cc/common:cc_shared_library_info.bzl", "CcSharedLibraryInfo")

def _main_impl(ctx):
    library = ctx.attr.component[CcSharedLibraryInfo].linker_input.libraries[0].dynamic_library
    output = ctx.actions.declare_file(ctx.label.name + ".cpp")
    ctx.actions.expand_template(
        template = ctx.file.template,
        output = output,
        substitutions = {
            "@node@": ctx.attr.node_name,
            "@executor@": ctx.attr.executor,
            "@component@": ctx.attr.plugin,
            "@library_name@": library.basename,
        },
    )
    return [DefaultInfo(files = depset([output]))]

_main = rule(
    implementation = _main_impl,
    attrs = {
        "component": attr.label(providers = [CcSharedLibraryInfo], mandatory = True),
        "node_name": attr.string(mandatory = True),
        "plugin": attr.string(mandatory = True),
        "executor": attr.string(mandatory = True),
        "template": attr.label(allow_single_file = True, mandatory = True),
    },
)

def agnocast_components_register_node(name, component, plugin, executor = "SingleThreadedAgnocastExecutor", agnocast_only = False):
    """Expose the standard component record and an Agnocast standalone executable."""
    rclcpp_components_register_node(name = name, component = component, plugin = plugin)
    _main(
        name = name + "_main",
        component = component,
        node_name = name,
        plugin = plugin,
        executor = executor,
        template = Label("//:src/node_main_agnocast_only.cpp.in" if agnocast_only else "//:src/node_main.cpp.in"),
    )
    collect_transitive_dynamic_deps(name = name + "_dynamic_deps", dynamic_deps = [component, Label("@agnocastlib//:agnocast")])
    cc_binary(
        name = name,
        srcs = [":" + name + "_main"],
        data = [":" + name + "_record"],
        dynamic_deps = [":" + name + "_dynamic_deps"],
        deps = [
            Label("@agnocastlib//:agnocast_library"),
            Label("@rclcpp_components//:rclcpp_components_library"),
            Label("@glog//:glog"),
        ],
    )
