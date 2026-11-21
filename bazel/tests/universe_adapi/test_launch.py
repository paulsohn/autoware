# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the original Universe AD API launch and installed web entrypoint."""
import json
from pathlib import Path
import time
import unittest
from urllib.error import URLError
from urllib.request import urlopen

from ament_index_python.packages import get_package_share_directory
from autoware_adapi_v1_msgs.msg import Heartbeat
from autoware_adapi_v1_msgs.srv import GetVehicleDimensions, ListMrmDescription
from autoware_adapi_version_msgs.srv import InterfaceVersion
import launch
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import AnyLaunchDescriptionSource
from launch_ros.actions import Node
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import pytest
from rcl_interfaces.srv import GetParameters
import rclpy
from rclpy.qos import qos_profile_sensor_data

PACKAGE = 'autoware_default_adapi_universe'


@pytest.mark.launch_test
def generate_test_description():
    source = str(Path(get_package_share_directory(PACKAGE)) / 'launch/test_default_adapi.launch.xml')
    return launch.LaunchDescription([
        IncludeLaunchDescription(AnyLaunchDescriptionSource(source), launch_arguments={
            'vehicle_model': 'sample_vehicle', 'use_sim_time': 'false',
        }.items()),
        Node(package=PACKAGE, executable='web_server.py', output='screen'),
        launch_testing.actions.ReadyToTest(),
    ])


class TestApi(unittest.TestCase):
    def test_composed_contract(self, proc_info):
        rclpy.init()
        node = rclpy.create_node('universe_api_peer')
        try:
            def wait(condition):
                deadline = time.monotonic() + 35
                while time.monotonic() < deadline:
                    if condition():
                        return
                    rclpy.spin_once(node, timeout_sec=0.05)
                self.fail('AD API did not reach the expected ROS/HTTP contract')

            def call(message_type, name, request):
                client = node.create_client(message_type, name)
                wait(client.service_is_ready)
                future = client.call_async(request)
                wait(future.done)
                self.assertIsNone(future.exception())
                return future.result()

            names = ['interface', 'localization', 'routing', 'autoware_state', 'diagnostics', 'fail_safe',
                     'heartbeat', 'manual/local', 'manual/remote', 'motion', 'mrm_request', 'operation_mode',
                     'perception', 'planning', 'vehicle_command', 'vehicle_door', 'vehicle_info',
                     'vehicle_metrics', 'vehicle_status']
            for name in names:
                result = call(GetParameters, '/adapi/node/' + name + '/get_parameters',
                              GetParameters.Request(names=['use_sim_time']))
                self.assertFalse(result.values[0].bool_value, name)
            version = call(InterfaceVersion, '/api/interface/version', InterfaceVersion.Request())
            expected = (version.major, version.minor, version.patch)
            self.assertEqual(expected, (1, 9, 1))
            dimensions = call(GetVehicleDimensions, '/api/vehicle/dimensions', GetVehicleDimensions.Request())
            self.assertTrue(dimensions.status.success)
            self.assertAlmostEqual(dimensions.dimensions.wheel_base, 2.79)
            self.assertEqual(len(dimensions.dimensions.footprint.points), 4)
            descriptions = call(ListMrmDescription, '/api/fail_safe/list_mrm_description', ListMrmDescription.Request())
            self.assertTrue(descriptions.status.success)
            self.assertEqual({item.name for item in descriptions.descriptions}, {'emergency_stop', 'comfortable_stop'})
            heartbeats = []
            node.create_subscription(Heartbeat, '/api/system/heartbeat', heartbeats.append, qos_profile_sensor_data)
            wait(lambda: len(heartbeats) >= 2)
            self.assertEqual((heartbeats[-1].seq - heartbeats[-2].seq) % 65536, 1)

            def http_ready():
                try:
                    with urlopen('http://localhost:8888/interface/version', timeout=1) as response:
                        data = json.load(response)
                    self.assertEqual(tuple(data[key] for key in ('major', 'minor', 'patch')), expected)
                    return True
                except URLError:
                    return False

            wait(http_ready)
            processes = proc_info.processes()
            self.assertEqual(len(processes), 2)
            container_maps = [Path('/proc', str(p.process_details['pid']), 'maps').read_text() for p in processes]
            self.assertTrue(any('libautoware_default_adapi_universe_component.so' in maps for maps in container_maps))
            self.assertTrue(all('libagnocast_heaphook.so' not in maps for maps in container_maps))
        finally:
            node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
