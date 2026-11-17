# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Launch every original core group together without a graphical display."""
from pathlib import Path
import shutil
import tempfile
import time
import unittest
import xml.etree.ElementTree as ET

from ament_index_python.packages import get_package_share_directory
from autoware_adapi_version_msgs.srv import InterfaceVersion
from autoware_map_msgs.msg import LaneletMapBin
from autoware_vehicle_msgs.msg import VelocityReport
from geometry_msgs.msg import TwistWithCovarianceStamped
import launch
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import AnyLaunchDescriptionSource
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import pytest
from rcl_interfaces.srv import GetParameters
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile
from std_msgs.msg import String

_MAP = None


@pytest.mark.launch_test
def generate_test_description():
    global _MAP
    _MAP = tempfile.TemporaryDirectory(prefix='core-system-')
    directory = Path(_MAP.name)
    helper = Path(get_package_share_directory('autoware_test_utils'))
    fixtures = Path(__file__).parent.parent / 'system_context'
    shutil.copyfile(helper / 'test_map/lanelet2_map.osm', directory / 'lanelet2_map.osm')
    shutil.copyfile(fixtures / 'projector.yaml', directory / 'map_projector_info.yaml')
    shutil.copyfile(fixtures / 'single.pcd', directory / 'pointcloud_map.pcd')
    source = Path(get_package_share_directory('autoware_core')) / 'launch/autoware_core.launch.xml'
    # Preserve the default GUI contract while explicitly exercising headless mode.
    assert ET.parse(source).find("arg[@name='launch_rviz']").get('default') == 'true'
    return launch.LaunchDescription([
        IncludeLaunchDescription(AnyLaunchDescriptionSource(str(source)), launch_arguments={
            'map_path': str(directory), 'vehicle_model': 'autoware_sample_vehicle',
            'sensor_model': 'autoware_sample_sensor_kit', 'launch_rviz': 'false',
        }.items()),
        launch_testing.actions.ReadyToTest(),
    ])


class TestCoreSystem(unittest.TestCase):
    def test_all_groups_maps_parameters_relay_and_api(self):
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
        finally:
            node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
