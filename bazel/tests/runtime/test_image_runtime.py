# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the loaded OCI image and its normal entrypoint on a local engine."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
import uuid
import xml.etree.ElementTree as ET

IMAGE_ARCHIVE, COMPANION = (str(Path(p).resolve()) for p in sys.argv[1:])
del sys.argv[1:]


def docker(*args, check=True):
    return subprocess.run(['docker', *args], check=check, capture_output=True, text=True)


class TestImageRuntime(unittest.TestCase):
    def test_behavior_and_entrypoint_shutdown(self):
        with tarfile.open(IMAGE_ARCHIVE) as archive:
            entries = json.load(archive.extractfile('manifest.json'))
        self.assertEqual(len(entries), 1)
        self.assertEqual(len(entries[0]['RepoTags']), 1)
        image = entries[0]['RepoTags'][0]
        docker('load', '--input', IMAGE_ARCHIVE)
        config = json.loads(docker('image', 'inspect', image).stdout)[0]
        self.assertEqual(config['Config']['Entrypoint'], ['/opt/autoware/run'])
        self.assertFalse(config['Config'].get('Cmd'))
        self.assertEqual(config['Config']['User'], '1000:1000')
        self.assertEqual(config['Config']['WorkingDir'], '/')
        self.assertEqual(config['Config']['StopSignal'], 'SIGINT')
        self.assertEqual((config['Os'], config['Architecture']), ('linux', 'amd64'))
        self.assertEqual(len(config['RootFS']['Layers']), 2, 'Expected Ubuntu plus one application layer')

        with tempfile.TemporaryDirectory(prefix='core-image-', dir=os.environ['TEST_TMPDIR']) as temporary:
            root = Path(temporary)
            checks, results, maps = (root / name for name in ('checks', 'results', 'maps'))
            for path in (checks, results, maps):
                path.mkdir()
            with tarfile.open(COMPANION) as archive:
                archive.extractall(checks, filter='data')
            common = ['--pull=never', '--network=none', '--read-only', '--cap-drop=ALL',
                      '--security-opt=no-new-privileges', '--tmpfs', '/tmp:rw,exec,nosuid,nodev']
            command = [
                'run', '--rm', *common,
                '--mount', f'type=bind,src={checks},dst=/checks,readonly',
                '--mount', f'type=bind,src={results},dst=/results',
                '--entrypoint', '/bin/sh', image, '-ec',
                'test ! -d /opt/ros; if command -v python3; then exit 1; fi; '
                'mkdir /tmp/home /tmp/state; '
                'exec env -i HOME=/tmp/home LOGNAME=runtime PATH=/usr/bin:/bin '
                'TEST_TMPDIR=/tmp/state XML_OUTPUT_FILE=/results/core.xml '
                '/checks/runtime/verify /opt/autoware/run',
            ]
            behavior = docker(*command, check=False)
            print(behavior.stdout, behavior.stderr, flush=True)
            self.assertEqual(behavior.returncode, 0, 'Packaged core behavior/shutdown gate failed')
            cases = list(ET.parse(results / 'core.xml').getroot().iter('testcase'))
            self.assertEqual(len(cases), 2)
            for case in cases:
                self.assertFalse(case.findall('failure') + case.findall('error') + case.findall('skipped'))

            prefix = checks / 'core_system_headless_launch_test.runfiles/_main'
            for source, destination in (
                (prefix / 'share/autoware_test_utils/test_map/lanelet2_map.osm', 'lanelet2_map.osm'),
                (prefix / 'bazel/tests/system_context/projector.yaml', 'map_projector_info.yaml'),
                (prefix / 'bazel/tests/system_context/single.pcd', 'pointcloud_map.pcd'),
            ):
                shutil.copyfile(source, maps / destination)
            name = 'autoware-core-test-' + uuid.uuid4().hex[:12]
            try:
                docker('run', '--detach', '--name', name, *common,
                       '--mount', f'type=bind,src={maps},dst=/maps,readonly',
                       image, 'map_path:=/maps')
                deadline = time.monotonic() + 60
                while time.monotonic() < deadline:
                    logs = docker('logs', name)
                    log = logs.stdout + logs.stderr
                    state = json.loads(docker('inspect', name).stdout)[0]['State']
                    self.assertTrue(state['Running'], log)
                    if "StopLineModulePlugin' is loaded." in log:
                        break
                    time.sleep(0.2)
                else:
                    self.fail('Normal image entrypoint did not initialize its planning plugin: ' + log)
                docker('stop', '--time', '30', name)
                state = json.loads(docker('inspect', name).stdout)[0]['State']
                logs = docker('logs', name)
                log = logs.stdout + logs.stderr
                print('Normal entrypoint shutdown:\n' + log, flush=True)
                self.assertEqual(state['ExitCode'], 0)
                self.assertFalse(state['OOMKilled'])
                self.assertNotIn('process has died', log)
                self.assertNotIn('failed to terminate', log)
                started = set(re.findall(r'\[([^\[\]]+)\]: process started with pid', log))
                stopped = set(re.findall(r'\[([^\[\]]+)\]: process has finished cleanly', log))
                self.assertTrue(started)
                self.assertEqual(started, stopped)
            finally:
                docker('rm', '--force', name, check=False)


if __name__ == '__main__':
    unittest.main()
