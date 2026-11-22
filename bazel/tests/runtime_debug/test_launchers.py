# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check launcher activation and argument/exit forwarding without starting ROS."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

RUN, SETUP, SIM_RUN, SIM_SETUP = [Path(arg).absolute() for arg in sys.argv[1:5]]
SIM_DISCOVERY = sys.argv[5]
del sys.argv[1:]


class LaunchersTest(unittest.TestCase):
    def test_relocated_launcher_environment(self):
        for entry, wrapper, setup, installed_setup in (
                ('core_headless', RUN, SETUP, 'core_setup.bash'),
                ('planning_simulator', SIM_RUN, SIM_SETUP, 'simulator_setup.bash')):
            with self.subTest(entrypoint=entry):
                self.check_launcher(entry, wrapper, setup, installed_setup)

    def check_launcher(self, entry, wrapper, setup, installed_setup):
        with tempfile.TemporaryDirectory(prefix='core launcher ') as directory:
            root = Path(directory)
            shutil.copy(wrapper, root / 'run')
            shutil.copy(setup, root / installed_setup)
            binary = root / entry
            binary.write_text('#!/bin/bash\n/usr/bin/env\nprintf "\\nARGS\\n"\nprintf "%s\\n" "$@"\nexit "${FAKE_STATUS:-0}"\n')
            binary.chmod(0o755)
            environment = dict.fromkeys((
                'AMENT_PREFIX_PATH', 'COLCON_PREFIX_PATH', 'CMAKE_PREFIX_PATH',
                'PYTHONPATH', 'PYTHONHOME', 'LD_LIBRARY_PATH', 'RUNFILES_DIR',
                'RUNFILES_MANIFEST_FILE', 'JAVA_RUNFILES'), '/unrelated/environment')
            environment.update(PATH='/usr/bin:/bin', ROS_DOMAIN_ID='117',
                               ENABLE_AGNOCAST='1', OMP_NUM_THREADS='8',
                               CYCLONEDDS_URI='custom-discovery', LD_PRELOAD='')
            for status in (0, 7):
                environment['FAKE_STATUS'] = str(status)
                result = subprocess.run([str(root / 'run'), 'use_sim_time:=true', 'argument with spaces'],
                                        cwd='/', env=environment, text=True, capture_output=True)
                self.assertEqual(result.returncode, status, result.stderr)
                env_text, arguments = result.stdout.split('\nARGS\n')
                observed = dict(line.split('=', 1) for line in env_text.splitlines())
                prefix = str(root / (entry + '.runfiles') / '_main')
                self.assertEqual(observed['AMENT_PREFIX_PATH'], prefix)
                self.assertEqual(observed['LD_LIBRARY_PATH'], prefix + '/lib')
                self.assertEqual(observed['RUNFILES_DIR'], str(root / (entry + '.runfiles')))
                self.assertEqual(observed['PATH'], str(root / 'bin') + ':/usr/bin:/bin')
                self.assertEqual(observed['ENABLE_AGNOCAST'], '0')
                self.assertEqual(observed['RMW_IMPLEMENTATION'], 'rmw_cyclonedds_cpp')
                self.assertEqual(observed['ROS_DISTRO'], 'lyrical')
                self.assertEqual(observed['OMP_NUM_THREADS'], '1')
                self.assertEqual(observed['ROS_DOMAIN_ID'], '117')
                self.assertEqual(observed['CYCLONEDDS_URI'], 'custom-discovery' if entry == 'core_headless' else SIM_DISCOVERY)
                for name in ('COLCON_PREFIX_PATH', 'CMAKE_PREFIX_PATH', 'PYTHONPATH', 'PYTHONHOME',
                             'LD_PRELOAD', 'RUNFILES_MANIFEST_FILE', 'JAVA_RUNFILES'):
                    self.assertNotIn(name, observed)
                expected = [
                    prefix + '/share/autoware_core/launch/autoware_core.launch.xml',
                    'use_sim_time:=true', 'argument with spaces',
                    'vehicle_model:=autoware_sample_vehicle',
                    'sensor_model:=autoware_sample_sensor_kit', 'launch_rviz:=false'] if entry == 'core_headless' else [
                        prefix + '/share/autoware_launch/launch/planning_simulator.launch.xml',
                        'rviz:=false', 'use_sim_time:=true', 'argument with spaces']
                self.assertEqual(arguments.splitlines(), expected)


if __name__ == '__main__':
    unittest.main()
