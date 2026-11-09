# Dev/CI image: no ROS and no compiler. Bazel fetches the hermetic toolchain and Lyrical.
# The image never runs Bazel; Bazel runs in a container, reusing the host caches:
#   docker build -f images/dev.Dockerfile -t autoware-bazel-dev images
#   docker run --rm -it -v "$PWD":/workspace \
#     -v ~/.cache/bazel-disk:/home/ubuntu/.cache/bazel-disk \
#     -v ~/.cache/bazel-repo:/home/ubuntu/.cache/bazel-repo \
#     autoware-bazel-dev bazel build //bazel/smoke:talker
# ubuntu:24.04 multi-arch index digest, 2026-11-09.
FROM ubuntu:24.04@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852

ARG BAZELISK_VERSION=v1.30.0
ARG TARGETARCH

# libegl1 libgl1 libopengl0: the OpenGL loader (glvnd) that RCR binaries linking PCL -> VTK -> Qt need, as in the runtime
# image (bazel/runtime_apt.MODULE.bazel).
RUN apt-get update \
  && apt-get install -y --no-install-recommends ca-certificates curl git iproute2 unzip zip \
    libegl1 libgl1 libopengl0 \
  && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /usr/local/bin/bazel \
    "https://github.com/bazelbuild/bazelisk/releases/download/${BAZELISK_VERSION}/bazelisk-linux-${TARGETARCH}" \
  && curl -fsSL "https://github.com/bazelbuild/bazelisk/releases/download/${BAZELISK_VERSION}/bazelisk-linux-${TARGETARCH}.sha256" \
    | sed 's|$|  /usr/local/bin/bazel|' | sha256sum -c - \
  && chmod 755 /usr/local/bin/bazel

# ubuntu:24.04 ships user "ubuntu" with uid/gid 1000, matching the usual host user.
# Pre-create ~/.cache: otherwise Docker creates it as root for the cache mounts.
RUN install -d -o ubuntu -g ubuntu /home/ubuntu/.cache
USER ubuntu
WORKDIR /workspace
