# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise global parameters and the original core sensing/map/context launches."""

import os
from pathlib import Path
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_map_msgs.msg import LaneletMapBin, MapProjectorInfo
from autoware_vehicle_msgs.msg import VelocityReport
from geometry_msgs.msg import TwistWithCovarianceStamped
import launch
from launch.actions import GroupAction, IncludeLaunchDescription
from launch.launch_description_sources import AnyLaunchDescriptionSource
from launch_ros.actions import Node
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import pytest
from rcl_interfaces.srv import GetParameters
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile
from sensor_msgs.msg import PointCloud2

SIMULATED = os.environ['CORE_CONTEXT_SIM_TIME'] == 'true'


def share(package):
    return Path(get_package_share_directory(package))


def include(package, filename, arguments=None):
    return IncludeLaunchDescription(
        AnyLaunchDescriptionSource(str(share(package) / 'launch' / filename)),
        launch_arguments=(arguments or {}).items(),
    )


@pytest.mark.launch_test
def generate_test_description():
    fixture = Path(__file__).parent
    loader = include('autoware_global_parameter_loader', 'global_params.launch.py', {
        'vehicle_model': 'autoware_sample_vehicle',
        'use_sim_time': 'true' if SIMULATED else 'false',
    })
    maps = include('autoware_core_map', 'autoware_core_map.launch.xml', {
        'lanelet2_map_path': str(share('autoware_test_utils') / 'test_map/lanelet2_map.osm'),
        'map_projector_info_path': str(fixture / 'projector.yaml'),
        'pointcloud_map_path': str(fixture / 'single.pcd'),
        # Single-file loading synthesizes metadata when this path is absent.
        'pointcloud_map_metadata_path': str(fixture / 'absent.yaml'),
    })
    # This consumer requires vehicle parameters but receives no explicit vehicle YAML.
    probe = Node(package='autoware_path_generator', executable='path_generator_node',
                 name='parameter_probe', namespace='bazel_context', parameters=[
                     str(share('autoware_path_generator') / 'config/path_generator.param.yaml'),
                     str(share('autoware_core_planning') / 'config/nearest_search.param.yaml'),
                 ])
    return launch.LaunchDescription([
        GroupAction(scoped=False, actions=[loader]),
        maps,
        include('autoware_core_sensing', 'autoware_core_sensing.launch.xml'),
        include('autoware_core_perception', 'autoware_core_perception.launch.xml'),
        include('autoware_core_localization', 'autoware_core_localization.launch.xml'),
        probe,
        launch_testing.actions.ReadyToTest(),
    ])


class TestContext(unittest.TestCase):
    def test_parameters_maps_and_relay(self):
        rclpy.init()
        node = rclpy.create_node('system_context_peer')
        try:
            def wait(condition, publish=None):
                deadline = time.monotonic() + 40
                while time.monotonic() < deadline:
                    if condition():
                        return
                    if publish:
                        publish()
                    rclpy.spin_once(node, timeout_sec=0.05)
                self.fail('Core context did not reach its declared configuration/data gate')

            def parameters(name, keys):
                client = node.create_client(GetParameters, name + '/get_parameters')
                wait(client.service_is_ready)
                future = client.call_async(GetParameters.Request(names=keys))
                wait(future.done)
                self.assertIsNone(future.exception())
                return future.result().values

            values = parameters('/bazel_context/parameter_probe',
                                ['use_sim_time', 'wheel_base', 'wheel_radius', 'vehicle_height'])
            self.assertEqual(values[0].bool_value, SIMULATED)
            self.assertEqual([v.double_value for v in values[1:]], [2.79, 0.383, 2.5])
            for name in ['/map/pointcloud_map_loader', '/perception/ground_filter_node',
                         '/localization/twist_estimator/twist_estimator_relay']:
                self.assertEqual(parameters(name, ['use_sim_time'])[0].bool_value, SIMULATED)
            projectors, lanelets, clouds, relayed = [], [], [], []
            durable = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
            node.create_subscription(MapProjectorInfo, '/map/map_projector_info', projectors.append, durable)
            node.create_subscription(LaneletMapBin, '/map/vector_map', lanelets.append, durable)
            node.create_subscription(PointCloud2, '/map/pointcloud_map', clouds.append, durable)
            node.create_subscription(TwistWithCovarianceStamped,
                                     '/localization/twist_estimator/twist_with_covariance',
                                     relayed.append, 10)
            wait(lambda: projectors and lanelets and clouds)
            self.assertEqual(projectors[-1].projector_type, MapProjectorInfo.MGRS)
            self.assertGreater(len(lanelets[-1].data), 0)
            self.assertEqual(clouds[-1].width * clouds[-1].height, 3)
            publisher = node.create_publisher(VelocityReport, '/vehicle/status/velocity_status', 10)
            report = VelocityReport()
            report.header.frame_id = 'base_link'
            report.longitudinal_velocity = 2.0
            report.lateral_velocity = 0.125
            report.heading_rate = 0.25
            wait(lambda: relayed, lambda: publisher.publish(report))
            self.assertEqual(relayed[-1].twist.twist.linear.x, 2.0)
            self.assertEqual(relayed[-1].twist.twist.linear.y, 0.125)
            self.assertEqual(relayed[-1].twist.twist.angular.z, 0.25)
            expected = {'/perception/object_recognition/euclidean_cluster',
                        '/perception/object_recognition/detected_to_predicted_objects_converter_node',
                        '/localization/pose_estimator/ndt_scan_matcher'}
            wait(lambda: expected <= {ns.rstrip('/') + '/' + n
                                      for n, ns in node.get_node_names_and_namespaces()})
        finally:
            node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
