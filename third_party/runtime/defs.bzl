"""Publish declared native runtime libraries at executable runfiles' lib prefix."""

def _runtime_libraries_impl(ctx):
    symlinks = {}
    files = []
    for target in ctx.attr.libraries:
        for file in target[DefaultInfo].files.to_list():
            names = [file.basename] + ctx.attr.sonames.get(file.basename, [])
            for name in names:
                path = "lib/" + name
                if path in symlinks and symlinks[path] != file:
                    fail("Conflicting runtime library: " + path)
                symlinks[path] = file
            files.append(file)
    return [DefaultInfo(
        files = depset(files),
        runfiles = ctx.runfiles(symlinks = symlinks),
    )]

runtime_libraries = rule(
    implementation = _runtime_libraries_impl,
    attrs = {
        "libraries": attr.label_list(allow_files = True),
        "sonames": attr.string_list_dict(),
    },
)
