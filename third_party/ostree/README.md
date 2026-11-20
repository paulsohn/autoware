# OSTree application repository tools

The root selects Ubuntu noble OSTree 2024.5-1build2 for Linux x86_64.
archives.json pins 73 Debian payloads by version, archive path and SHA-256,
selected from noble/noble-updates main and universe metadata on 2026-11-21.
It includes the matching libc/loader and the transitive shared-library closure.
The selected command scope is local init, commit, show/rev-parse, fsck,
pull-local --untrusted and checkout; signing/key management and remote transport
are not established by this provider. Package maintainer scripts, account
creation tools, the gnupg command suite and package installation metadata are
not executed or required for that scope.

The shared third_party/deb_payloads rule preserves upstream data archives and
uses declared Python 3.14 Zstandard conversion before deterministic rules_pkg
assembly. :runtime supplies that SDK archive. :tool provides Python helpers to
extract and run its CLI with the pinned ELF loader/library directory, independent
of a host OSTree installation. Product repositories, activation, services and
state policies belong to consumers. This provider depends on no Autoware labels.

The [SDK experiment](../../docs/bazel/JOURNAL.md#2026-11-21-phase-724--ostree-sdk)
records the exact validated commands and limitations. The upstream
[commit](https://ostreedev.github.io/ostree/man/ostree-commit.html),
[checkout](https://ostreedev.github.io/ostree/man/ostree-checkout.html) and
[pull-local](https://ostreedev.github.io/ostree/man/ostree-pull-local.html)
references describe tree imports, force-copy checkout and untrusted integrity
verification. An application checkout does not boot or update an OS.

Use `bazel run --config=runtime //third_party/ostree:ostree -- <arguments>` to
run the selected CLI without installing it. It extracts its declared SDK into
an ephemeral directory; use absolute paths for repositories outside Bazel's
working directory. Command output and status are passed through unchanged.
