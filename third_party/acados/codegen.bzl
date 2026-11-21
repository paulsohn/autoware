"""Run an acados Python generator with declared templates and renderer inputs."""

def _generate_impl(ctx):
    outputs = [ctx.actions.declare_file(ctx.label.name + "/" + path) for path in ctx.attr.outputs]
    args = ctx.actions.args()
    args.add(ctx.executable.generator.path)
    args.add(ctx.file._source_marker.dirname)
    args.add(ctx.file._renderer.path)
    args.add(ctx.files._libstdcxx[0].dirname)
    args.add(ctx.files._libgcc[0].dirname)
    for path, output in zip(ctx.attr.outputs, outputs):
        args.add(path + "=" + output.path)
    ctx.actions.run(
        executable = ctx.executable._runner,
        arguments = [args],
        inputs = depset([ctx.file._source_marker, ctx.file._renderer] + ctx.files._libstdcxx + ctx.files._libgcc, transitive = [ctx.attr._templates[DefaultInfo].files]),
        tools = [ctx.attr.generator[DefaultInfo].files_to_run],
        outputs = outputs,
        mnemonic = "AcadosGenerate",
        progress_message = "Generating acados solver %{label}",
    )
    return [DefaultInfo(files = depset(outputs))]

_acados_codegen = rule(
    implementation = _generate_impl,
    attrs = {
        "generator": attr.label(executable = True, cfg = "exec", mandatory = True),
        "outputs": attr.string_list(mandatory = True),
        "_runner": attr.label(default = Label(":codegen_runner"), executable = True, cfg = "exec"),
        "_source_marker": attr.label(default = Label("@acados_source//:README.md"), allow_single_file = True),
        "_templates": attr.label(default = Label("@acados_source//:templates")),
        "_libstdcxx": attr.label(default = Label("@acados_libstdcxx//:runtime"), cfg = "exec"),
        "_libgcc": attr.label(default = Label("@acados_libgcc//:runtime"), cfg = "exec"),
        "_renderer": attr.label(default = Label("@tera_renderer//file"), allow_single_file = True, cfg = "exec"),
    },
)

def acados_codegen(name, **kwargs):
    """Generate C on the SDK's supported Linux x86_64/Python 3.14 builder."""
    _acados_codegen(
        name = name,
        exec_compatible_with = [Label("@platforms//os:linux"), Label("@platforms//cpu:x86_64")],
        **kwargs
    )
