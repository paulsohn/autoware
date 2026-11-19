"""Classify the pinned SDK's headers without weakening consumer warnings."""

load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

def _headers_impl(ctx):
    return [CcInfo(compilation_context = cc_common.create_compilation_context(
        headers = depset(ctx.files.hdrs),
        system_includes = depset([ctx.label.workspace_root + "/usr/local/include"]),
    ))]

spconv_headers = rule(
    implementation = _headers_impl,
    attrs = {"hdrs": attr.label_list(allow_files = True)},
)
