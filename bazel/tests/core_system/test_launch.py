# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Launch every original core group together without a graphical display."""
from pathlib import Path
import os
import re
import time
import unittest
import xml.etree.ElementTree as ET

from ament_index_python.packages import get_package_share_directory
from autoware_adapi_version_msgs.srv import InterfaceVersion
from autoware_map_msgs.msg import LaneletMapBin
from autoware_vehicle_msgs.msg import VelocityReport
from geometry_msgs.msg import TwistWithCovarianceStamped
import launch
from launch.actions import ExecuteProcess, IncludeLaunchDescription
from launch.launch_description_sources import AnyLaunchDescriptionSource
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import psutil
import pytest
from rcl_interfaces.srv import GetParameters
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile
from std_msgs.msg import String

_BUNDLE_CHILDREN = []


@pytest.mark.launch_test
def generate_test_description():
    source = Path(get_package_share_directory('autoware_core')) / 'launch/autoware_core.launch.xml'
    # Preserve the default GUI contract while explicitly exercising headless mode.
    assert ET.parse(source).find("arg[@name='launch_rviz']").get('default') == 'true'
    launcher = os.environ.get('CORE_SYSTEM_BUNDLE_LAUNCHER')
    service_launcher = os.environ.get('CORE_SYSTEM_SERVICE_LAUNCHER', launcher)
    system = ExecuteProcess(cmd=[service_launcher], output='screen') if launcher else (
        IncludeLaunchDescription(AnyLaunchDescriptionSource(str(source)), launch_arguments={
            'vehicle_model': 'autoware_sample_vehicle',
            'sensor_model': 'autoware_sample_sensor_kit', 'launch_rviz': 'false',
        }.items()))
    return launch.LaunchDescription([
        system,
        launch_testing.actions.ReadyToTest(),
    ]), {'bundle_process': system if launcher else None}


class TestCoreSystem(unittest.TestCase):
    def test_all_groups_maps_parameters_relay_and_api(self, bundle_process):
        rclpy.init()
        node = rclpy.create_node('core_system_peer')
        try:
            def wait(condition, publish=None):
                deadline = time.monotonic() + 45
                while time.monotonic() < deadline:
                    if condition():
                        return
                    if publish:
                        publish()
                    rclpy.spin_once(node, timeout_sec=0.05)
                self.fail('Full core launch did not reach its declared behavior gate')

            def call(client, request):
                wait(client.service_is_ready)
                result = client.call_async(request)
                wait(result.done)
                self.assertIsNone(result.exception())
                return result.result()

            expected = {
                '/map/pointcloud_map_loader',
                '/perception/object_recognition/euclidean_cluster',
                '/localization/pose_estimator/ndt_scan_matcher',
                '/planning/mission_planning/mission_planner',
                '/planning/scenario_planning/lane_driving/behavior_planning/path_generator',
                '/control/simple_pure_pursuit', '/robot_state_publisher',
                '/adapi/node/interface',
            }

            def names():
                return {ns.rstrip('/') + '/' + n for n, ns in node.get_node_names_and_namespaces()}
            wait(lambda: expected <= names())
            self.assertNotIn('/rviz2', names())
            for name in ['/control/simple_pure_pursuit',
                         '/planning/scenario_planning/lane_driving/behavior_planning/path_generator']:
                client = node.create_client(GetParameters, name + '/get_parameters')
                values = call(client, GetParameters.Request(names=['wheel_base', 'use_sim_time'])).values
                self.assertAlmostEqual(values[0].double_value, 2.79)
                self.assertFalse(values[1].bool_value)
            maps, descriptions, twists = [], [], []
            qos = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
            node.create_subscription(LaneletMapBin, '/map/vector_map', maps.append, qos)
            node.create_subscription(String, '/robot_description', descriptions.append, qos)
            node.create_subscription(TwistWithCovarianceStamped,
                                     '/localization/twist_estimator/twist_with_covariance', twists.append, 10)
            wait(lambda: maps and descriptions)
            self.assertTrue(maps[-1].data)
            self.assertIn('sensor_kit_base_link', descriptions[-1].data)
            publisher = node.create_publisher(VelocityReport, '/vehicle/status/velocity_status', 10)
            report = VelocityReport()
            report.header.frame_id = 'base_link'
            report.longitudinal_velocity = 2.0
            wait(lambda: twists, lambda: publisher.publish(report))
            self.assertAlmostEqual(twists[-1].twist.twist.linear.x, 2.0)
            version = call(node.create_client(InterfaceVersion, '/api/interface/version'), InterfaceVersion.Request())
            self.assertEqual((version.major, version.minor, version.patch), (1, 9, 1))
            wait(lambda: node.count_publishers('/planning/trajectory') > 0 and
                 node.count_subscribers('/planning/trajectory') > 0 and
                 node.count_publishers('/control/command/control_cmd') > 0)
            if bundle_process is not None:
                bundle = Path(os.environ['CORE_SYSTEM_BUNDLE_LAUNCHER']).resolve().parent
                baseline = {'ld-linux-x86-64.so.2', 'libc.so.6', 'libm.so.6', 'libdl.so.2',
                            'libpthread.so.0', 'librt.so.1', 'libresolv.so.2', 'libutil.so.1'}
                system_libraries = set()
                service_pid = os.environ.get('CORE_SYSTEM_SERVICE_PID_FILE')
                parent = psutil.Process(int(Path(service_pid).read_text()) if service_pid else
                                        bundle_process.process_details['pid'])
                processes = parent.children(recursive=True)
                self.assertTrue(processes, 'No packaged application processes to audit')
                for process in [parent] + processes:
                    _BUNDLE_CHILDREN.append((process.pid, process.create_time()))
                    executable = Path(process.exe()).resolve()
                    self.assertTrue(executable.is_relative_to(bundle), str(executable))
                    for mapping in process.memory_maps():
                        library = Path(mapping.path)
                        if '.so' not in library.name:
                            continue
                        library = library.resolve()
                        if library.is_relative_to(bundle):
                            continue
                        self.assertEqual(library.parent, Path('/usr/lib/x86_64-linux-gnu'))
                        self.assertIn(library.name, baseline, str(library))
                        system_libraries.add(str(library))
                print('Runtime OS libraries:', sorted(system_libraries), flush=True)
        finally:
            node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_exit_codes(self, proc_info, proc_output, bundle_process):
        launch_testing.asserts.assertExitCodes(proc_info)
        if bundle_process is not None:
            # The outer launch sees one process; inspect the nested launch's
            # reports too, so a successful parent cannot hide a failed child.
            output = ''.join(event.text.decode() for event in proc_output[bundle_process])
            self.assertNotIn('process has died', output)
            self.assertNotIn('failed to terminate', output)
            started = set(re.findall(r'\[([^\[\]]+)\]: process started with pid', output))
            stopped = set(re.findall(r'\[([^\[\]]+)\]: process has finished cleanly', output))
            self.assertTrue(started, 'No packaged child startup reports')
            self.assertEqual(started, stopped, 'Packaged children did not all exit cleanly')
            for pid, created in _BUNDLE_CHILDREN:
                try:
                    process = psutil.Process(pid)
                    self.assertNotEqual(process.create_time(), created, 'Packaged child survived shutdown')
                except psutil.NoSuchProcess:
                    pass
