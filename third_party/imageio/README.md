# ImageIO

This module exposes the upstream ImageIO 2.38.2 universal wheel through Bazel.
The wheel URL and SHA-256 come from the [PyPI release metadata](https://pypi.org/pypi/imageio/2.38.2/json).
The module reuses NumPy and Pillow from the selected RCR Python dependency set.
It does not install packages into the host Python environment.

The public `@imageio//:imageio` target supplies the Python package and metadata.
`bazel test @imageio//:gif_roundtrip_test` checks the GIF writer and reader with
both existing dependencies. Optional ImageIO plugins need their own declared
inputs when a consumer requires them.
