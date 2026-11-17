# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the original loader-based API launch with a local vehicle model."""

import os
from pathlib import Path
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_adapi_version_msgs.srv import InterfaceVersion
import launch
from launch.actions import IncludeLaunchDescription, SetLaunchConfiguration
from launch.launch_description_sources import AnyLaunchDescriptionSource
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import pytest
from rcl_interfaces.srv import GetParameters
import rclpy


@pytest.mark.launch_test
def generate_test_description():
    source = str(Path(get_package_share_directory('autoware_default_adapi')) /
                 'launch/test_default_adapi.launch.xml')
    return launch.LaunchDescription([
        SetLaunchConfiguration('agnocast_heaphook_path', os.environ.get('AGNOCAST_TEST_HEAPHOOK', '')),
        IncludeLaunchDescription(AnyLaunchDescriptionSource(source), launch_arguments={
            'vehicle_model': 'autoware_sample_vehicle', 'use_sim_time': 'true',
        }.items()),
        launch_testing.actions.ReadyToTest(),
    ])


class TestApiLoader(unittest.TestCase):
    def test_global_context_and_version(self, proc_info):
        rclpy.init()
        node = rclpy.create_node('api_global_context_peer')
        try:
            def call(client, request):
                deadline = time.monotonic() + 25
                while not client.service_is_ready() and time.monotonic() < deadline:
                    rclpy.spin_once(node, timeout_sec=0.05)
                self.assertTrue(client.service_is_ready())
                future = client.call_async(request)
                while not future.done() and time.monotonic() < deadline:
                    rclpy.spin_once(node, timeout_sec=0.05)
                self.assertTrue(future.done())
                self.assertIsNone(future.exception())
                return future.result()

            for name in ['interface', 'localization', 'routing']:
                client = node.create_client(GetParameters, '/adapi/node/' + name + '/get_parameters')
                result = call(client, GetParameters.Request(names=['use_sim_time']))
                self.assertTrue(result.values[0].bool_value)
            client = node.create_client(InterfaceVersion, '/api/interface/version')
            version = call(client, InterfaceVersion.Request())
            self.assertEqual((version.major, version.minor, version.patch), (1, 9, 1))
            enabled = os.environ['ENABLE_AGNOCAST'] == '1'
            processes = proc_info.processes()
            self.assertEqual(len(processes), 3 if enabled else 1)
            for process in processes:
                maps = Path('/proc', str(process.process_details['pid']), 'maps').read_text()
                self.assertEqual('libagnocast_heaphook.so' in maps, enabled)
        finally:
            node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
