#!/usr/bin/env bash
# Offline installation from the two images retained on the laptop.
set -eu
base=ros@sha256:c69a7a39142b0a19c8cc5e80a35e9087e2ae46080dfa0a59961ff72287fdc5f6
cache=local/autoware-migration@sha256:bce1042b25e9c2fa2c4566e35d2176e111abddb2aed801ca2d852aa358a7e95f
dir=$(mktemp -d)
donor=$(docker create --pull=never "$cache")
trap 'docker rm "$donor" >/dev/null; rm -rf "$dir"' EXIT
docker cp "$donor:/tmp/cyclone-debs/." "$dir"
chmod -R u+rX "$dir"
cat > "$dir/Dockerfile" <<DOCKER
FROM $base
COPY *.deb /tmp/peer-debs/
RUN dpkg -i /tmp/peer-debs/*.deb && rm -rf /tmp/peer-debs
DOCKER
# Exclude the donor's apt cache directories.
printf 'partial\nlock\n' >"$dir/.dockerignore"
DOCKER_BUILDKIT=0 docker build --network none --memory 384m --memory-swap 384m -t local/lyrical-cyclone-peer:v1 "$dir"
