# Development environment for checking Bazel builds independently of the host.
FROM ubuntu:24.04@sha256:2198e4c65076e9cb0b3d695219df667c0e3b0072da64ca3c2648aca17dcecaf5

ARG BAZELISK_VERSION=v1.29.0
ARG TARGETARCH
ARG DEBIAN_FRONTEND=noninteractive

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update \
  && apt-get install -y --no-install-recommends ca-certificates curl git python3 unzip zip \
  && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /usr/local/bin/bazel \
    "https://github.com/bazelbuild/bazelisk/releases/download/${BAZELISK_VERSION}/bazelisk-linux-${TARGETARCH}" \
  && curl -fsSL \
    "https://github.com/bazelbuild/bazelisk/releases/download/${BAZELISK_VERSION}/bazelisk-linux-${TARGETARCH}.sha256" \
    | sed 's|$|  /usr/local/bin/bazel|' \
    | sha256sum -c - \
  && chmod 755 /usr/local/bin/bazel

# Ubuntu 24.04 provides this uid/gid, matching the common Linux host user.
RUN install -d -o ubuntu -g ubuntu /home/ubuntu/.cache

USER ubuntu
WORKDIR /workspace
