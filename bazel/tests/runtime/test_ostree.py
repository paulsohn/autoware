# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Verify immutable application delivery, interrupted preparation and rollback."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest

from third_party.ostree.tool import command, environment, extract_sdk, run

SDK, REPOSITORY, IMAGE, COMPANION, BUNDLE = (str(Path(p).resolve()) for p in sys.argv[1:])
del sys.argv[1:]


class TestOSTree(unittest.TestCase):
    def test_application_deployments(self):
        with tempfile.TemporaryDirectory(prefix='ostree-gate-', dir=os.environ['TEST_TMPDIR']) as temp:
            root = Path(temp)
            sdk = extract_sdk(SDK, root / 'sdk')
            distribution = root / 'distribution'
            with tarfile.open(REPOSITORY) as archive:
                archive.extractall(distribution, filter='data')
            manifest = json.loads((distribution / 'releases.json').read_text())
            self.assertEqual(manifest['scope'], 'application-only')
            for name, path in [('bundle_sha256', BUNDLE), ('sdk_sha256', SDK)]:
                with open(path, 'rb') as source:
                    self.assertEqual(manifest[name], hashlib.file_digest(source, 'sha256').hexdigest())
            a, b = (manifest['revisions'][x] for x in ('a', 'b'))
            self.assertNotEqual(a, b)
            source, local = distribution / 'repo', root / 'local'
            run(sdk, '--repo=' + str(source), 'fsck')
            # Exercise integrity rejection, not just the success exit status of fsck.
            object_path = next((source / 'objects').rglob('*.filez'))
            original = object_path.read_bytes()
            object_path.chmod(0o644)
            object_path.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
            result = subprocess.run(command(sdk, '--repo=' + str(source), 'fsck'),
                                    env=environment(sdk), capture_output=True)
            self.assertNotEqual(result.returncode, 0, 'Corruption was accepted')
            object_path.write_bytes(original)
            run(sdk, '--repo=' + str(source), 'fsck')
            run(sdk, '--repo=' + str(local), 'init', '--mode=archive')
            result = subprocess.run(command(sdk, '--repo=' + str(local), 'pull-local', '--untrusted',
                                            str(source), 'core/a', 'core/b'),
                                    env=environment(sdk), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            run(sdk, '--repo=' + str(local), 'fsck')
            for release, commit in manifest['revisions'].items():
                self.assertEqual(run(sdk, '--repo=' + str(local), 'rev-parse', 'core/' + release).stdout.strip(), commit)
                provenance = run(sdk, '--repo=' + str(local), 'show',
                                 '--print-metadata-key=autoware.bundle-sha256', commit).stdout
                self.assertIn(manifest['bundle_sha256'], provenance)
            deployments, vehicle = root / 'deployments', root / 'vehicle'
            deployments.mkdir()
            vehicle.mkdir()
            records = {'identity.json': '{"vehicle_id":"synthetic-vehicle-1"}\n',
                       'calibration.yaml': 'synthetic: true\nwheelbase: 2.79\n'}
            for name, value in records.items():
                (vehicle / name).write_text(value)
            active = root / 'active'

            def checkout(commit, destination):
                run(sdk, '--repo=' + str(local), 'checkout', '--user-mode', '--force-copy', commit, destination)

            def activate(commit):
                temporary = root / 'active.next'
                temporary.symlink_to('deployments/' + commit)
                os.replace(temporary, active)
                descriptor = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
                try:
                    os.fsync(descriptor)
                finally:
                    os.close(descriptor)

            def verify(release, commit):
                self.assertEqual(active.resolve(), deployments / commit)
                metadata = json.loads((active / 'release.json').read_text())
                self.assertEqual(metadata['release'], release)
                self.assertEqual(metadata['domain_resource'], 'synthetic-domain-' + release)
                for name, value in records.items():
                    self.assertEqual((vehicle / name).read_text(), value)
                env = os.environ.copy()
                env.update(CORE_IMAGE_BUNDLE=str(active), CORE_IMAGE_VEHICLE_STATE=str(vehicle))
                result = subprocess.run([sys.executable, str(Path(__file__).with_name('test_supervised_image.py')),
                                         'systemd', IMAGE, COMPANION], env=env, capture_output=True,
                                        text=True, timeout=300)
                if result.returncode:
                    print(result.stdout, result.stderr, flush=True)
                self.assertEqual(result.returncode, 0, 'Complete managed core gate failed for ' + release)
                print('Complete systemd/core gate passed for', release, commit, flush=True)

            checkout(a, deployments / a)
            expected = set()
            with tarfile.open(BUNDLE, 'r|gz') as archive:
                for member in archive:
                    destination = deployments / a / member.name
                    if member.isdir():
                        self.assertTrue(destination.is_dir())
                        continue
                    expected.add(str(Path(member.name)))
                    if member.issym():
                        self.assertEqual(os.readlink(destination), member.linkname)
                    elif member.isfile():
                        with archive.extractfile(member) as original, destination.open('rb') as actual:
                            self.assertEqual(hashlib.file_digest(original, 'sha256').digest(),
                                             hashlib.file_digest(actual, 'sha256').digest(), member.name)
                        self.assertEqual(bool(destination.stat().st_mode & 0o111), bool(member.mode & 0o111))
                        self.assertFalse(destination.stat().st_mode & 0o002)
                    else:
                        self.assertTrue(member.islnk() and destination.is_file(), member.name)
            actual = {str(p.relative_to(deployments / a)) for p in (deployments / a).rglob('*')
                      if not p.is_dir() or p.is_symlink()}
            self.assertEqual(actual, expected | {'release.json'})
            print(len(expected), 'native files/links preserve bytes, targets and executable status', flush=True)
            activate(a)
            partial = deployments / 'incomplete'
            process = subprocess.Popen(command(sdk, '--repo=' + str(local), 'checkout', '--user-mode',
                                               '--force-copy', b, partial), env=environment(sdk),
                                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            try:
                deadline = time.monotonic() + 30
                while process.poll() is None and time.monotonic() < deadline:
                    if partial.exists() and any(p.is_file() and p.stat().st_size for p in partial.rglob('*')):
                        break
                    time.sleep(0.002)
                self.assertIsNone(process.poll(), 'Preparation completed before interruption')
                self.assertTrue(partial.exists() and any(partial.iterdir()), 'No partial preparation was observed')
                process.kill()
                process.wait(timeout=10)
                self.assertEqual(process.returncode, -9)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                process.stderr.close()
            self.assertEqual(active.resolve(), deployments / a)
            verify('a', a)
            shutil.rmtree(partial)
            checkout(b, deployments / b)
            activate(b)
            verify('b', b)
            activate(a)
            verify('a', a)
            print('Integrity rejection, provenance, interrupted preparation, A→B→A activation '
                  'and preserved synthetic vehicle state passed', flush=True)


if __name__ == '__main__':
    unittest.main()
