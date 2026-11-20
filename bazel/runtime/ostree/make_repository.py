# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Compose two immutable application revisions from the declared native bundle."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import tarfile
import tempfile

from third_party.ostree.tool import extract_sdk, run

parser = argparse.ArgumentParser()
parser.add_argument('--sdk', required=True)
parser.add_argument('--bundle', required=True)
parser.add_argument('--archive', required=True)
parser.add_argument('--manifest', required=True)
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix='core-ostree-') as temp:
    root = Path(temp)
    sdk = extract_sdk(args.sdk, root / 'sdk')
    repo = root / 'repo'
    run(sdk, '--repo=' + str(repo), 'init', '--mode=archive')
    with open(args.bundle, 'rb') as source:
        bundle_hash = hashlib.file_digest(source, 'sha256').hexdigest()
    with open(args.sdk, 'rb') as source:
        sdk_hash = hashlib.file_digest(source, 'sha256').hexdigest()
    revisions = {}
    for release in ('a', 'b'):
        metadata = root / release
        metadata.mkdir()
        metadata.chmod(0o755)
        (metadata / 'release.json').write_text(json.dumps({
            'release': release, 'domain_resource': 'synthetic-domain-' + release,
            'bundle_sha256': bundle_hash,
        }, sort_keys=True) + '\n')
        (metadata / 'release.json').chmod(0o644)
        tree = 'tar=' + str(Path(args.bundle).resolve()) if release == 'a' else 'ref=' + revisions['a']
        result = run(sdk, '--repo=' + str(repo), 'commit', '--branch=core/' + release,
                     '--parent=' + (revisions.get('a') or 'none'), '--tree=' + tree,
                     '--tree=dir=' + str(metadata), '--no-xattrs', '--owner-uid=0', '--owner-gid=0',
                     '--canonical-permissions',
                     '--timestamp=2026-11-21T00:00:00Z', '--subject=Core application ' + release,
                     '--add-metadata-string=autoware.bundle-sha256=' + bundle_hash)
        revisions[release] = result.stdout.strip()
    run(sdk, '--repo=' + str(repo), 'fsck')
    manifest = {'schema': 1, 'scope': 'application-only', 'ostree': '2024.5-1build2',
                'bundle_sha256': bundle_hash, 'sdk_sha256': sdk_hash, 'revisions': revisions}
    manifest_text = json.dumps(manifest, sort_keys=True, indent=2) + '\n'
    (root / 'releases.json').write_text(manifest_text)
    Path(args.manifest).write_text(manifest_text)

    def normalize(info):
        info.uid = info.gid = info.mtime = 0
        info.uname = info.gname = ''
        return info

    with open(args.archive, 'wb') as output:
        with gzip.GzipFile(fileobj=output, filename='', mode='wb', compresslevel=1, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w|') as archive:
                archive.add(repo, arcname='repo', filter=normalize)
                archive.add(root / 'releases.json', arcname='releases.json', filter=normalize)
    print(manifest_text)
