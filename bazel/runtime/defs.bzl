"""Build executable product launchers with the selected runtime configuration."""

def _executable_template_impl(ctx):
    ctx.actions.expand_template(
        template = ctx.file.template,
        output = ctx.outputs.out,
        substitutions = ctx.attr.substitutions,
        is_executable = True,
    )
    return [DefaultInfo(files = depset([ctx.outputs.out]))]

executable_template = rule(
    implementation = _executable_template_impl,
    attrs = {
        "template": attr.label(mandatory = True, allow_single_file = True),
        "substitutions": attr.string_dict(mandatory = True),
        "out": attr.output(mandatory = True),
    },
)
