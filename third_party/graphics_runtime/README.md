# Linux graphics dispatch runtime

The registry's libglvnd 1.7.0.rcr.0 supplies link-time stubs. Applications that
load PCL/VTK/OpenCV libraries still need real GL dispatch DSOs, even in a launch
that creates no graphical context. This provider selects GLVND 1.7.0 and its
X11/XCB load dependencies for Ubuntu 24.04 amd64.

archives.json pins eleven binary archives by package version, URL-relative
filename and SHA-256 from Ubuntu's official
[noble main amd64 metadata](https://archive.ubuntu.com/ubuntu/dists/noble/main/binary-amd64/Packages.xz),
inspected 2026-11-20. Bazel downloads and extracts these payloads with its native
[repository archive API](https://bazel.build/rules/lib/builtins/repository_ctx).
No apt/dpkg resolver, maintainer script, host package install or host-file copy
participates. Their per-package copyright notices remain declared resources.
The root selects this runtime alongside its other pinned SDK inputs.

This is a binary runtime provider, not a replacement for registry build headers
or an implementation of a GPU driver. The complete provider gate is
//third_party/graphics_runtime:dispatch_test: load all five real dispatch DSOs
and query EGL's display-independent client extensions. Product acceptance also
requires the selected application's complete relocated behavior/library audit.
No display server, Mesa/NVIDIA vendor backend, rendering, ARM build or full
upstream graphics conformance is established by that gate. Add those resources
and tests only when a selected graphical product needs them.
