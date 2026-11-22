# s6 container runtime

The root selects s6-overlay 3.2.3.2 through s6.MODULE.bazel. Two SHA-256-pinned
upstream release archives provide the architecture-independent scripts and
static Linux x86_64 binaries, including s6 2.15.1.0 and s6-rc 0.7.0.0.
The runtime target combines their original filesystem layouts; the layer target
exposes an image layer without depending on any application or product label.

Release/provenance: https://github.com/just-containers/s6-overlay/releases/tag/v3.2.3.2

Product service definitions and deployment settings belong in bazel/deployments/.
The service-supervision gate is composed by bazel/tests/; these
archives do not provide application readiness or failure policies themselves.
