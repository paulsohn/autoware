"""Rewrite a tar so that every regular file whose content already appeared becomes a relative symlink to it.

Usage: dedup_tar.py <in.tar> <out.tar>

Deterministic: entries keep their order, metadata and mtime; the first occurrence (in tar order) keeps the bytes.
"""

import hashlib
import posixpath
import sys
import tarfile


def main(src, dst):
    first = {}
    saved = 0
    with tarfile.open(src) as tin, tarfile.open(dst, "w", format=tarfile.GNU_FORMAT) as tout:
        for m in tin:
            if not m.isfile():
                tout.addfile(m)
                continue
            data = tin.extractfile(m).read()
            key = (len(data), hashlib.sha256(data).digest())
            name = m.name.removeprefix("./")
            if key in first and m.size > 0:
                link = tarfile.TarInfo(m.name)
                link.type = tarfile.SYMTYPE
                link.linkname = posixpath.relpath(first[key], posixpath.dirname(name) or ".")
                link.mtime, link.mode, link.uid, link.gid = m.mtime, 0o777, m.uid, m.gid
                link.uname, link.gname = m.uname, m.gname
                tout.addfile(link)
                saved += m.size
            else:
                first.setdefault(key, name)
                tout.addfile(m, fileobj=__import__("io").BytesIO(data))
    print(f"dedup_tar: {saved / 1e6:.1f} MB replaced by symlinks", file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
