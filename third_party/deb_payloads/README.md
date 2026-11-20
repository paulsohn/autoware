# Pinned Debian payload assembly

SDK owners provide an explicit JSON list containing Package, Version, Filename
and SHA256. deb_runtime downloads those archives from pinned Ubuntu archive
paths, extracts their data tarballs and emits declared Python 3.14 conversion
and rules_pkg assembly actions. No dpkg, apt, maintainer scripts or host zstd
are invoked. Filesystem links, executable modes and copyright resources remain
in their original payload layout. This handles the selected Zstandard-compressed
Ubuntu payloads, not arbitrary Debian installation semantics.

Owners keep their package selection/provenance and public targets alongside
their own manifests. Consumers must validate the commands/services they need;
package metadata closure alone does not establish runtime behavior.
