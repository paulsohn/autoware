# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise a sourceable ROS environment without checkout/cache/host ROS paths."""
import json
from pathlib import Path
import signal
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest

ARCHIVE, MANIFEST, INTERFACES, PEER = [Path(arg).absolute() for arg in sys.argv[1:]]
del sys.argv[1:]


class DebugBundleTest(unittest.TestCase):
    def test_relocated_environment_and_cli(self):
        with tempfile.TemporaryDirectory(prefix='autoware debug ') as temporary:
            root = Path(temporary) / 'distribution'
            root.mkdir()
            with tarfile.open(ARCHIVE) as archive:
                archive.extractall(root, filter='data')
            for path in root.rglob('*'):
                if path.is_symlink():
                    self.assertTrue(path.resolve().is_relative_to(root), str(path))
                    self.assertTrue(path.exists(), str(path))
            runfiles = root / 'planning_simulator_debug.runfiles'
            closure = json.loads(MANIFEST.read_text())
            for path in closure['symlinks']:
                self.assertTrue((runfiles / '_main' / path).exists(), path)
            for path in closure['root_symlinks']:
                self.assertTrue((runfiles / path).exists(), path)
            print(f"Preserved {len(closure['symlinks'])} entrypoint index/resource paths", flush=True)

            home = Path(temporary) / 'home'
            home.mkdir()
            env = {'HOME': str(home), 'PATH': '/usr/bin:/bin', 'ROS_DOMAIN_ID': '116'}

            def run(*args, timeout=40):
                result = subprocess.run(args, cwd=temporary, env=env, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
                self.assertEqual(result.returncode, 0, f'{args}\n{result.stdout}')
                return result.stdout

            activation = run('/bin/bash', '--noprofile', '--norc', '-c', r"""
set -eu
source "$1/install.bash"
first_path=$PATH
source "$1/setup.bash"
test "$PATH" = "$first_path"
test "${RUNFILES_DIR-unset}" = unset
test "$ROS_DOMAIN_ID" = 116
test "$RMW_IMPLEMENTATION" = rmw_cyclonedds_cpp
test "$ENABLE_AGNOCAST" = 0
test "$AMENT_PREFIX_PATH" = "$1/planning_simulator_debug.runfiles/_main"
command -v ros2
command -v python3
ros2 pkg prefix autoware_launch
""", 'activation', str(root))
            self.assertIn(str(root / 'bin' / 'ros2'), activation)
            ros2 = str(root / 'bin' / 'ros2')
            python = str(root / 'bin' / 'python3')
            for command in ('node', 'topic', 'service', 'param', 'pkg', 'interface', 'run',
                            'launch', 'bag', 'component', 'lifecycle', 'action', 'doctor'):
                run(ros2, command, '--help')
            for package in ('autoware_launch', 'autoware_map_asset', 'sample_vehicle_description'):
                self.assertIn(str(runfiles / '_main'), run(ros2, 'pkg', 'prefix', package))
            self.assertIn('component_container', run(ros2, 'pkg', 'executables', 'rclcpp_components'))
            run(ros2, 'run', 'rclcpp_components', 'component_container', '--help')
            self.assertIn('TrajectoryPoint', run(ros2, 'interface', 'show', 'autoware_planning_msgs/msg/Trajectory'))
            interfaces = run(ros2, 'interface', 'list')
            self.assertIn('grid_map_msgs/msg/GridMap', interfaces)
            self.assertNotIn('grid_map_msgs/msg/Time', interfaces)
            self.assertIn('vehicle_model', run(
                ros2, 'launch', 'autoware_launch', 'planning_simulator.launch.xml', '--show-args',
                'vehicle_model:=sample_vehicle', 'sensor_model:=sample_sensor_kit', 'rviz:=false'))
            print(run(python, '-c', """
from pathlib import Path
import sys
import rclpy
from rosidl_runtime_py.utilities import get_message, get_service, get_action
from rosidl_generator_py import import_type_support
root = Path(sys.argv[1])
assert Path(sys.executable).resolve().is_relative_to(root)
assert Path(rclpy.__file__).resolve().is_relative_to(root)
count = 0
for descriptor in Path(sys.argv[2]).read_text().splitlines():
    package, kind, name = descriptor.split('/')
    import_type_support(package)
    {'msg': get_message, 'srv': get_service, 'action': get_action}[kind](descriptor)
    count += 1
print(f'Imported {count} declared ROS interfaces with bundled Python')
""", str(root), str(INTERFACES)), flush=True)

            with (Path(temporary) / 'peer.log').open('w+') as log:
                peer = subprocess.Popen([python, str(PEER)], cwd=temporary, env=env,
                                        stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        log.seek(0)
                        if 'PEER_READY' in log.read():
                            break
                        self.assertIsNone(peer.poll(), 'Peer exited during initialization')
                        time.sleep(0.1)
                    else:
                        self.fail('Peer did not initialize')
                    self.assertIn('/debug_bundle_peer', run(ros2, 'node', 'list', '--no-daemon', '--spin-time', '2'))
                    self.assertIn('42', run(ros2, 'param', 'get', '/debug_bundle_peer', 'debug_value'))
                    run(ros2, 'param', 'set', '/debug_bundle_peer', 'debug_value', '43')
                    self.assertIn('43', run(ros2, 'param', 'get', '/debug_bundle_peer', 'debug_value'))
                    self.assertIn('debug_bundle_frame', run(
                        ros2, 'topic', 'echo', '/debug_bundle/trajectory',
                        'autoware_planning_msgs/msg/Trajectory', '--once'))
                    run(ros2, 'topic', 'pub', '--once', '--wait-matching-subscriptions', '1',
                        '/debug_bundle/input', 'std_msgs/msg/String', '{data: debug_bundle_input}')
                    self.assertIn('debug_bundle_input', run(
                        ros2, 'service', 'call', '/debug_bundle/received', 'std_srvs/srv/Trigger', '{}'))
                    print('Live node, topic, service and parameter CLI checks passed', flush=True)
                finally:
                    peer.send_signal(signal.SIGINT)
                    try:
                        peer.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        peer.kill()
                        peer.wait()
                    log.seek(0)
                    print(log.read(), flush=True)
                    run(ros2, 'daemon', 'stop')
                self.assertEqual(peer.returncode, 0)


if __name__ == '__main__':
    unittest.main()
