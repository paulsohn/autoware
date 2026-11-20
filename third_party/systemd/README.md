# Ubuntu systemd runtime

The root selects systemd 255.4-1ubuntu8.19 for Ubuntu 24.04 Linux x86_64.
archives.json pins the 32 manager/library/helper payloads by package version,
archive path and SHA-256. They were selected from Ubuntu noble/noble-updates
metadata on 2026-11-21. The root OS image supplies glibc and its loader.

The shared third_party/deb_payloads rule downloads Debian archives, preserves their data tarballs
and emits declared Python 3.14 Zstandard conversion actions. rules_pkg then
combines those uncompressed tars, preserving links, executable permissions and
upstream copyright resources. Package maintainer scripts are not executed.
Build/install dependencies such as debconf and systemd-dev are not runtime
providers. The package list is a runtime input selection, not an installed dpkg
package database or a complete general-purpose Ubuntu system.

Use :runtime for the filesystem archive and :layer for OCI composition. Product
units, users, writable state and container settings belong to the consumer.
This provider has no dependency on Autoware components or product labels.
The selected service behavior is validated by the product's complete image gate;
see the [product gate](../../docs/bazel/JOURNAL.md#2026-11-21-phase-723--systemd-core-service-supervision)
for supported runtime configurations and evidence.

Source metadata:
https://archive.ubuntu.com/ubuntu/dists/noble/main/binary-amd64/Packages.xz
https://archive.ubuntu.com/ubuntu/dists/noble-updates/main/binary-amd64/Packages.xz

The main archive returned HTTP 403 for the selected payloads during inspection;
verified copies with identical SHA-256 were available from the kernel.org Ubuntu
mirror and security.ubuntu.com. Both are configured download locations.
