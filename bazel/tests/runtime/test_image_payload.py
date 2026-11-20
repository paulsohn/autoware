# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Verify that installing the native archive adds only its image prefix."""
import hashlib
import posixpath
import sys
import tarfile
import unittest

NATIVE, IMAGE = sys.argv[1:]
del sys.argv[1:]


def contents(filename, prefix):
    result = {}
    with tarfile.open(filename) as archive:
        for member in archive:
            path = posixpath.normpath(member.name)
            if member.isdir():
                continue
            if prefix:
                if not path.startswith(prefix):
                    raise AssertionError('Unexpected file outside application prefix: ' + path)
                path = path[len(prefix):]
            if path in result:
                raise AssertionError('Duplicate payload destination: ' + path)
            digest = None
            if member.isfile():
                digest = hashlib.file_digest(archive.extractfile(member), 'sha256').hexdigest()
            result[path] = (member.type, member.mode, member.size, member.linkname, digest)
    return result


class TestImagePayload(unittest.TestCase):
    def test_exact_native_payload(self):
        native = contents(NATIVE, '')
        self.assertTrue(native)
        self.assertEqual(native, contents(IMAGE, 'opt/autoware/'))
        print(f'{len(native)} native files/links retain bytes, modes and targets in the image layer')


if __name__ == '__main__':
    unittest.main()
