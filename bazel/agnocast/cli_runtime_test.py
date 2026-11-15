# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the packaged CLI against an actual Agnocast sample publisher."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import time
import unittest

import yaml
from agnocast_sample_interfaces.msg import DynamicSizeArray
from rclpy.serialization import deserialize_message
from rosbag2_py import ConverterOptions, SequentialReader, StorageOptions
from rcr_launch_guard import launch_process_guard

CLI, TALKER = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
del sys.argv[1:3]


class CliRuntime(unittest.TestCase):
    def run_cli(self, *args):
        result = subprocess.run([CLI, *args], env=self.environment,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_discovery_echo_and_recording(self):
        self.environment = dict(os.environ, ROS_DOMAIN_ID='73', ROS_DISTRO='lyrical')
        self.environment.pop('LD_PRELOAD', None)
        self.assertIn('agnocast', self.run_cli('--help'))
        versions = self.run_cli('agnocast', 'version')
        for component in ('agnocastlib', 'agnocast_heaphook', 'ros2agnocast'):
            self.assertRegex(versions, component + r':\s+2\.4\.1')
        native_environment = dict(self.environment, LD_PRELOAD=os.environ['AGNOCAST_TEST_HEAPHOOK'])
        with subprocess.Popen([TALKER], env=native_environment,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) as talker:
            try:
                time.sleep(3)
                self.assertIsNone(talker.poll(), 'sample publisher exited during startup')
                self.assertIn('/my_topic', self.run_cli('topic', 'list_agnocast', '--spin-time', '2'))
                self.run_cli('agnocast', 'discovery-daemon-status')
                value = self.run_cli('topic', 'echo_agnocast', '/my_topic', '--once', '--field', 'id')
                self.assertRegex(value, r'(?m)^\d+$')
                bag = Path(os.environ['TEST_TMPDIR']) / 'recording'
                log_path = Path(os.environ['TEST_UNDECLARED_OUTPUTS_DIR']) / 'recording.log'
                with log_path.open('w') as log, subprocess.Popen(
                        [CLI, 'bag', 'record_agnocast', '-o', str(bag), '--topics', '/my_topic'],
                        env=self.environment, stdout=log, stderr=subprocess.STDOUT) as recorder:
                    try:
                        time.sleep(5)
                        self.assertIsNone(recorder.poll(), log_path.read_text())
                    finally:
                        if recorder.poll() is None:
                            recorder.send_signal(signal.SIGINT)
                        try:
                            recorder.wait(timeout=15)
                        except subprocess.TimeoutExpired:
                            recorder.kill()
                            recorder.wait()
                            self.fail('recorder did not stop after SIGINT')
                self.assertIn(recorder.returncode, (0, signal.SIGINT), log_path.read_text())
                metadata = yaml.safe_load((bag / 'metadata.yaml').read_text())['rosbag2_bagfile_information']
                self.assertGreater(metadata['message_count'], 0, metadata)
                self.assertEqual(metadata['ros_distro'], 'lyrical')
                topic = next(t for t in metadata['topics_with_message_count']
                             if t['topic_metadata']['name'] == '/my_topic')
                self.assertGreater(topic['message_count'], 0, metadata)
                reader = SequentialReader()
                reader.open(StorageOptions(uri=str(bag), storage_id=metadata['storage_identifier']),
                            ConverterOptions('', ''))
                definitions = reader.get_all_message_definitions()
                self.assertTrue(any(d.topic_type == 'agnocast_sample_interfaces/msg/DynamicSizeArray'
                                    and d.encoded_message_definition for d in definitions), definitions)
                self.assertTrue(reader.has_next())
                name, payload, *_ = reader.read_next()
                self.assertEqual(name, '/my_topic')
                message = deserialize_message(payload, DynamicSizeArray)
                self.assertGreater(len(message.data), 0)
                self.assertEqual(message.data[0], message.id)
                self.assertEqual(message.data[-1], message.id + len(message.data) - 1)
            finally:
                if talker.poll() is None:
                    talker.send_signal(signal.SIGINT)
                try:
                    talker.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    talker.kill()
                    talker.wait()
                    self.fail('sample publisher did not stop after SIGINT')


if __name__ == '__main__':
    with launch_process_guard(target=os.environ.get('TEST_TARGET')):
        unittest.main()
