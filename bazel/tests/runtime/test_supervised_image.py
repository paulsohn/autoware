# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise readiness, original core behavior and supervised failure/recovery."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import unittest
import uuid
import xml.etree.ElementTree as ET

MANAGER, ARCHIVE, COMPANION = sys.argv[1:]
ARCHIVE, COMPANION = str(Path(ARCHIVE).resolve()), str(Path(COMPANION).resolve())
del sys.argv[1:]


def docker(*args, check=True):
    return subprocess.run(['docker', *args], check=check, capture_output=True, text=True, timeout=180)


class TestSupervisedImage(unittest.TestCase):
    def wait(self, condition, message):
        deadline = time.monotonic() + 70
        while time.monotonic() < deadline:
            value = condition()
            if value:
                return value
            time.sleep(0.1)
        self.fail(message)

    def stopped(self, name):
        state = json.loads(docker('inspect', name).stdout)[0]['State']
        return state if not state['Running'] else None

    def ready(self, state):
        path = state / 'core.log'
        return path.exists() and "StopLineModulePlugin' is loaded." in path.read_text()

    def clean_core(self, state):
        log = (state / 'core.log').read_text()
        self.assertNotIn('process has died', log)
        self.assertNotIn('failed to terminate', log)
        started = set(re.findall(r'\[([^\[\]]+)\]: process started with pid', log))
        stopped = set(re.findall(r'\[([^\[\]]+)\]: process has finished cleanly', log))
        self.assertEqual(len(started), 26)
        self.assertEqual(started, stopped)

    def test_services(self):
        self.assertIn(MANAGER, ['s6', 'systemd'])
        systemd = MANAGER == 'systemd'
        with tarfile.open(ARCHIVE) as archive:
            image = json.load(archive.extractfile('manifest.json'))[0]['RepoTags'][0]
        docker('load', '--input', ARCHIVE)
        info = json.loads(docker('image', 'inspect', image).stdout)[0]
        self.assertEqual(info['Config']['Entrypoint'],
                         ['/usr/lib/autoware-services/init'] if systemd else ['/init'])
        self.assertEqual(info['Config']['User'], '0:0' if systemd else '1000:1000')
        self.assertEqual((info['Os'], info['Architecture']), ('linux', 'amd64'))
        containers = []
        temporary = tempfile.TemporaryDirectory(prefix="supervised-core-", dir=os.environ["TEST_TMPDIR"])
        try:
            temp = temporary.name
            root = Path(temp)
            checks, maps = root / 'checks', root / 'maps'
            checks.mkdir()
            maps.mkdir()
            with tarfile.open(COMPANION) as archive:
                archive.extractall(checks, filter='data')
            prefix = checks / 'core_system_headless_launch_test.runfiles/_main'
            for source, destination in (
                (prefix / 'share/autoware_test_utils/test_map/lanelet2_map.osm', 'lanelet2_map.osm'),
                (prefix / 'bazel/tests/system_context/projector.yaml', 'map_projector_info.yaml'),
                (prefix / 'bazel/tests/system_context/single.pcd', 'pointcloud_map.pcd'),
            ):
                shutil.copyfile(source, maps / destination)

            def start(label, blocked=False):
                state = root / label
                state.mkdir(mode=0o777)
                state.chmod(0o777)
                name = 'autoware-' + MANAGER + '-' + uuid.uuid4().hex[:10]
                containers.append(name)
                flags = ['--env', 'AUTOWARE_READY_GATE=/run/autoware/release'] if blocked else []
                runtime = ['--cgroupns=private', '--tty', '--cap-add=SYS_ADMIN',
                           '--security-opt=apparmor=unconfined'] if systemd else ['--cap-drop=ALL']
                run_mount = '/run:rw,exec' if systemd else '/run:rw,exec,uid=1000,gid=1000,mode=0755'
                docker('run', '--detach', '--name', name, '--pull=never', '--network=none',
                       '--read-only', *runtime, '--security-opt=no-new-privileges',
                       '--tmpfs', '/tmp:rw,exec,nosuid,nodev', '--tmpfs', run_mount,
                       '--mount', f'type=bind,src={state},dst=/run/autoware',
                       '--mount', f'type=bind,src={maps},dst=/maps,readonly',
                       '--mount', f'type=bind,src={checks},dst=/checks,readonly', *flags, image)

                def started():
                    if (state / 'prerequisite.pid').exists():
                        return True
                    logs = docker('logs', name)
                    self.assertIsNone(self.stopped(name), logs.stdout + logs.stderr)
                    return False

                self.wait(started, 'Prerequisite did not start')
                return name, state

            name, state = start('behavior', blocked=True)
            time.sleep(1)
            self.assertFalse((state / 'core.pid').exists(), 'Core started before prerequisite readiness')
            (state / 'release').touch()
            self.wait(lambda: self.ready(state), 'Core did not initialize after readiness')
            if systemd:
                self.assertEqual(docker('exec', name, '/bin/cat', '/proc/1/comm').stdout.strip(), 'systemd')
                pid = (state / 'core.pid').read_text().strip()
                group = docker('exec', name, '/bin/cat', '/proc/' + pid + '/cgroup').stdout
                self.assertIn('/autoware-core.service', group)
            events = (state / 'events').read_text().splitlines()
            self.assertLess(events.index('prerequisite:ready'), events.index('core:starting'))

            errors = []
            finished = threading.Event()

            def stop_services():
                while not finished.wait(0.1):
                    if (state / 'stop-request').exists():
                        try:
                            command = ['/usr/bin/systemctl', 'stop', 'autoware.target',
                                       'autoware-core.service', 'autoware-prerequisite.service'] if systemd else [
                                '/command/s6-rc', '-d', 'change', 'prerequisite']
                            docker('exec', name, *command)
                        except Exception as error:
                            errors.append(error)
                            print('Supervisor stop failed:', error, getattr(error, 'stderr', ''), flush=True)
                        finally:
                            (state / 'stop-done').touch()
                        return

            controller = threading.Thread(target=stop_services)
            controller.start()
            try:
                peer = docker('exec', '--user', '1000:1000', name, '/bin/sh', '-ec',
                              'mkdir -p /tmp/peer; exec env -i HOME=/tmp LOGNAME=autoware '
                              'PATH=/usr/bin:/bin TEST_TMPDIR=/tmp/peer '
                              'XML_OUTPUT_FILE=/run/autoware/core.xml '
                              'CORE_SYSTEM_SERVICE_LAUNCHER=/checks/runtime/service_launcher '
                              'CORE_SYSTEM_SERVICE_PID_FILE=/run/autoware/core.pid '
                              '/checks/runtime/verify /opt/autoware/run', check=False)
                print(peer.stdout, peer.stderr, flush=True)
                self.assertEqual(peer.returncode, 0, 'Original core suite failed against managed services')
            finally:
                finished.set()
                controller.join(timeout=30)
            self.assertFalse(controller.is_alive())
            self.assertFalse(errors, str(errors))
            cases = list(ET.parse(state / 'core.xml').getroot().iter('testcase'))
            self.assertEqual(len(cases), 2)
            self.assertFalse(any(c.findall('failure') + c.findall('error') + c.findall('skipped') for c in cases))
            self.clean_core(state)
            events = (state / 'events').read_text().splitlines()
            self.assertLess(events.index('core:stopped'), events.index('prerequisite:stopped'))
            docker('stop', '--time', '30', name)
            self.assertEqual(self.stopped(name)['ExitCode'], 0)

            for failure_signal in ['-KILL', '-TERM']:
                name, state = start('failure-' + failure_signal)
                self.wait(lambda: self.ready(state), 'Core did not initialize before failure injection')
                pid = (state / 'prerequisite.pid').read_text().strip()
                docker('exec', name, '/bin/kill', failure_signal, pid)
                result = self.wait(lambda: self.stopped(name), 'Dependency failure did not stop product')
                self.assertNotEqual(result['ExitCode'], 0)
                self.assertIn('prerequisite:failed', (state / 'events').read_text())
                self.assertEqual((state / 'events').read_text().splitlines().count('prerequisite:starting'), 1,
                                 'A failed prerequisite must not be independently restarted')
                self.clean_core(state)
                for path in state.iterdir():
                    path.unlink()
                docker('start', name)
                self.wait(lambda: self.ready(state), 'Explicit product restart did not recover')
                docker('stop', '--time', '30', name)
                self.assertEqual(self.stopped(name)['ExitCode'], 0)
                self.clean_core(state)
                events = (state / 'events').read_text().splitlines()
                self.assertLess(events.index('core:stopped'), events.index('prerequisite:stopped'))

            name, state = start('startup-failure', blocked=True)
            docker('exec', name, '/bin/kill', '-KILL', (state / 'prerequisite.pid').read_text().strip())
            self.assertNotEqual(self.wait(lambda: self.stopped(name), 'Startup failure was hidden')['ExitCode'], 0)
            self.assertFalse((state / 'core.pid').exists())
            print('Delayed readiness, 2 original core cases, reverse shutdown, dependency loss, '
                  'explicit restart and startup failure passed', flush=True)
        finally:
            for name in containers:
                logs = docker('logs', name, check=False)
                print(name, logs.stdout, logs.stderr, flush=True)
                docker('rm', '--force', name, check=False)
            temporary.cleanup()


if __name__ == '__main__':
    unittest.main()
