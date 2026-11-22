# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run the packaged simulator's original launch and closed-loop driving gate."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile
import unittest
import xml.etree.ElementTree as ET

MODE, PRODUCT, COMPANION = sys.argv[1:]
PRODUCT, COMPANION = str(Path(PRODUCT).resolve()), str(Path(COMPANION).resolve())
del sys.argv[1:]


def docker(*args):
    return subprocess.run(['docker', *args], check=True, capture_output=True, text=True)


class SimulatorDistribution(unittest.TestCase):
    def test_original_launch_drives_and_stops(self):
        with tempfile.TemporaryDirectory(prefix='simulator-distribution-', dir=os.environ['TEST_TMPDIR']) as temporary:
            root = Path(temporary)
            checks, results, work = (root / name for name in ('checks', 'results', 'work'))
            for path in (checks, results, work):
                path.mkdir()
            with tarfile.open(COMPANION) as archive:
                archive.extractall(checks, filter='data')
            if MODE == 'native':
                application = root / 'application'
                application.mkdir()
                with tarfile.open(PRODUCT) as archive:
                    archive.extractall(application, filter='data')
                state = root / 'state'
                state.mkdir()
                home = root / 'home'
                home.mkdir()
                environment = {
                    'HOME': str(home), 'LOGNAME': 'runtime', 'PATH': '/usr/bin:/bin',
                    'TEST_TMPDIR': str(state), 'XML_OUTPUT_FILE': str(results / 'driving.xml'),
                }
                command = [str(checks / 'runtime/verify_simulator'), str(application / 'run_simulator')]
                result = subprocess.run(command, cwd=work, env=environment, capture_output=True, text=True, timeout=300)
            elif MODE == 'oci':
                with tarfile.open(PRODUCT) as archive:
                    manifest = json.load(archive.extractfile('manifest.json'))
                self.assertEqual(len(manifest), 1)
                self.assertEqual(len(manifest[0]['RepoTags']), 1)
                image = manifest[0]['RepoTags'][0]
                docker('load', '--input', PRODUCT)
                config = json.loads(docker('image', 'inspect', image).stdout)[0]
                self.assertEqual(config['Config']['Entrypoint'], ['/opt/autoware/run_simulator'])
                self.assertFalse(config['Config'].get('Cmd'))
                self.assertEqual(config['Config']['User'], '1000:1000')
                self.assertEqual(config['Config']['WorkingDir'], '/')
                self.assertEqual(config['Config']['StopSignal'], 'SIGINT')
                self.assertEqual((config['Os'], config['Architecture']), ('linux', 'amd64'))
                self.assertEqual(len(config['RootFS']['Layers']), 2)
                result = subprocess.run([
                    'docker', 'run', '--rm', '--pull=never', '--network=none', '--read-only', '--cap-drop=ALL',
                    '--security-opt=no-new-privileges', '--tmpfs', '/tmp:rw,exec,nosuid,nodev',
                    '--mount', f'type=bind,src={checks},dst=/checks,readonly',
                    '--mount', f'type=bind,src={results},dst=/results',
                    '--entrypoint', '/bin/sh', image, '-ec',
                    'test ! -d /opt/ros; test ! -d /workspace; test ! -d /root/.cache/bazel; '
                    'if command -v bazel || command -v python3; then exit 1; fi; '
                    'mkdir /tmp/home /tmp/state; '
                    'exec env -i HOME=/tmp/home LOGNAME=runtime PATH=/usr/bin:/bin '
                    'TEST_TMPDIR=/tmp/state XML_OUTPUT_FILE=/results/driving.xml '
                    '/checks/runtime/verify_simulator /opt/autoware/run_simulator',
                ], capture_output=True, text=True, timeout=300)
            else:
                self.fail('Unknown distribution profile: ' + MODE)
            output = result.stdout + result.stderr
            if result.returncode:
                print(output, flush=True)
            self.assertEqual(result.returncode, 0, MODE + ' packaged driving gate failed')
            cases = list(ET.parse(results / 'driving.xml').getroot().iter('testcase'))
            self.assertEqual(len(cases), 3)
            for case in cases:
                self.assertFalse(case.findall('failure') + case.findall('error') + case.findall('skipped'))
            self.assertNotIn('process has died', output)
            self.assertNotIn('failed to terminate', output)
            started = set(re.findall(r'\[([^\[\]]+)\]: process started with pid', output))
            stopped = set(re.findall(r'\[([^\[\]]+)\]: process has finished cleanly', output))
            self.assertEqual(len(started), 57)
            self.assertEqual(started, stopped)
            metrics = re.search(r'Arrived: displacement=.*', output)
            self.assertIsNotNone(metrics)
            print(f'{MODE}: {len(cases)} driving/shutdown cases; {len(started)} clean processes; {metrics[0]}', flush=True)


if __name__ == '__main__':
    unittest.main()
