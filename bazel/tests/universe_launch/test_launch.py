# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise original subsystem launches and the headless planning simulator."""
import os
from pathlib import Path
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_adapi_version_msgs.srv import InterfaceVersion
from autoware_map_msgs.msg import LaneletMapBin
from composition_interfaces.srv import ListNodes
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
from sensor_msgs.msg import PointCloud2
from std_msgs.msg import String
from tier4_system_msgs.msg import DiagGraphStruct
import yaml

MODE = os.environ['UNIVERSE_LAUNCH_MODE']


def include(package, path, arguments):
    source = Path(get_package_share_directory(package)) / 'launch' / path
    return IncludeLaunchDescription(AnyLaunchDescriptionSource(str(source)), launch_arguments=arguments.items())


@pytest.mark.launch_test
def generate_test_description():
    arguments = {'vehicle_model': 'sample_vehicle', 'sensor_model': 'sample_sensor_kit', 'use_sim_time': 'false'}
    entries = []
    if os.environ.get('PLANNING_SIMULATOR_BUNDLE_LAUNCHER'):
        if MODE != 'full':
            raise ValueError('The packaged simulator gate requires full launch mode')
        entries.append(launch.actions.ExecuteProcess(
            cmd=[os.environ['PLANNING_SIMULATOR_BUNDLE_LAUNCHER'], 'rviz:=false'],
            env={
                'HOME': os.environ['HOME'], 'PATH': '/usr/bin:/bin',
                'LOGNAME': os.environ.get('LOGNAME', 'runtime'),
                'ROS_DOMAIN_ID': os.environ.get('ROS_DOMAIN_ID', '0'),
            },
            output='screen', sigterm_timeout='30', sigkill_timeout='10'))
    elif MODE in ('planning', 'control'):
        entries.append(include('autoware_global_parameter_loader', 'global_params.launch.py', arguments))
        entries.append(include('autoware_' + MODE + '_launch', MODE + '.launch.xml', arguments))
    elif MODE == 'vehicle':
        entries.append(include('tier4_vehicle_launch', 'vehicle.launch.xml', dict(arguments, launch_vehicle_interface='false')))
    elif MODE == 'map':
        entries.append(include('autoware_launch', 'components/component_map.launch.xml', {}))
    elif MODE == 'api':
        entries.append(include('autoware_launch', 'components/component_map.launch.xml', {}))
        entries.append(include('autoware_launch', 'components/tier4_autoware_api_component.launch.xml', arguments))
    elif MODE == 'simulator':
        vehicle = str(Path(get_package_share_directory('sample_vehicle_description')) / 'config/vehicle_info.param.yaml')
        entries.append(include('autoware_launch', 'components/tier4_simulator_component.launch.xml', dict(
            arguments, launch_dummy_vehicle='true', launch_fault_injection='false', localization_sim_mode='api',
            launch_dummy_doors='true', launch_scenario_simulator_v2_adapter='false', initial_engage_state='true',
            vehicle_info_param_file=vehicle, raw_vehicle_cmd_converter_param_path=str(
                Path(get_package_share_directory('autoware_launch')) / 'config/vehicle/raw_vehicle_cmd_converter/raw_vehicle_cmd_converter.param.yaml'),
            scenario_simulation='false')))
    elif MODE == 'system':
        entries.append(include('autoware_launch', 'components/tier4_system_component.launch.xml', dict(
            arguments, planning_setting='rule_based', system_run_mode='planning_simulation',
            launch_system_monitor='false', launch_dummy_diag_publisher='true')))
    else:
        entries.append(include('autoware_launch', 'planning_simulator.launch.xml', dict(arguments, rviz='false')))
    return launch.LaunchDescription(entries + [launch_testing.actions.ReadyToTest()])


class Composition(unittest.TestCase):
    def test_selected_graph(self):
        rclpy.init()
        peer = rclpy.create_node('universe_launch_peer')
        try:
            def names():
                return {namespace.rstrip('/') + '/' + name for name, namespace in peer.get_node_names_and_namespaces()}

            def wait(condition):
                deadline = time.monotonic() + 60
                while time.monotonic() < deadline:
                    if condition():
                        return
                    rclpy.spin_once(peer, timeout_sec=0.05)
                self.fail('Expected ' + MODE + ' launch contract; discovered nodes: ' + str(sorted(names())))

            def call(client, request):
                wait(client.service_is_ready)
                future = client.call_async(request)
                wait(future.done)
                self.assertIsNone(future.exception())
                return future.result()

            def parameter(node, key):
                client = peer.create_client(GetParameters, node + '/get_parameters')
                return call(client, GetParameters.Request(names=[key])).values[0]

            def components(container, expected):
                client = peer.create_client(ListNodes, container + '/_container/list_nodes')
                deadline = time.monotonic() + 60
                loaded = set()
                while time.monotonic() < deadline:
                    response = call(client, ListNodes.Request())
                    loaded = set(response.full_node_names)
                    if isinstance(expected, int):
                        if len(loaded) == expected:
                            return
                    elif set(expected) <= {name.rsplit('/', 1)[-1] for name in loaded}:
                        return
                    rclpy.spin_once(peer, timeout_sec=0.05)
                self.fail('Incomplete component loading in ' + container + ': ' + str(sorted(loaded)))

            if MODE in ('api', 'full'):
                components('/adapi/container', 19)
                for helper in ['initial_pose_adaptor', 'routing_adaptor']:
                    parameter('/default_adapi/helpers/autoware_' + helper, 'use_sim_time')
            if MODE in ('system', 'full'):
                topics = Path(get_package_share_directory('autoware_launch')) / 'config/system/component_state_monitor/topics.yaml'
                rows = yaml.safe_load(topics.read_text())
                selected = ['topic_state_monitor_' + row['args']['node_name_suffix']
                            for row in rows if 'planning_simulation' in row['mode']]
                components('/system/component_state_monitor/container', selected)
                graphs = []
                peer.create_subscription(DiagGraphStruct, '/diagnostics_graph/struct', graphs.append,
                                         QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
                wait(lambda: graphs)
                self.assertIn('/autoware/modes/autonomous', {node.path for node in graphs[-1].nodes})
            if MODE in ('control', 'full'):
                components('/control/control_container', [
                    'controller_node_exe', 'vehicle_cmd_gate', 'autoware_operation_mode_transition_manager'])
            if MODE in ('planning', 'full'):
                base = '/planning/scenario_planning/'
                components(base + 'lane_driving/behavior_planning/behavior_planning_container', [
                    'behavior_path_planner', 'behavior_velocity_planner'])
                components(base + 'lane_driving/motion_planning/motion_planning_container', [
                    'elastic_band_smoother', 'path_optimizer', 'motion_velocity_planner'])
                components(base + 'parking/parking_container', ['costmap_generator', 'freespace_planner'])

            if MODE in ('map', 'full'):
                vectors, clouds = [], []
                qos = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
                peer.create_subscription(LaneletMapBin, '/map/vector_map', vectors.append, qos)
                peer.create_subscription(PointCloud2, '/map/pointcloud_map', clouds.append, qos)
                wait(lambda: vectors and clouds)
                self.assertEqual(vectors[-1].header.frame_id, 'map')
                self.assertEqual(clouds[-1].width * clouds[-1].height, 1757841)
            if MODE in ('vehicle', 'full'):
                descriptions = []
                peer.create_subscription(String, '/robot_description', descriptions.append,
                                         QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
                wait(lambda: descriptions)
                self.assertIn('base_link', descriptions[-1].data)
                self.assertNotIn('xacro:', descriptions[-1].data)
            if MODE in ('api', 'full'):
                client = peer.create_client(InterfaceVersion, '/api/interface/version')
                version = call(client, InterfaceVersion.Request())
                self.assertEqual((version.major, version.minor, version.patch), (1, 9, 1))
            expected = {
                'simulator': ['simple_planning_simulator', 'pose_initializer', 'vehicle_door_simulator_node'],
                'system': ['mrm_handler', 'processing_time_checker', 'pipeline_latency_monitor', 'aggregator'],
                'control': ['controller_node_exe', 'vehicle_cmd_gate', 'control_validator', 'collision_detector', 'autonomous_emergency_braking', 'lane_departure_checker_node'],
                'planning': ['behavior_path_planner', 'behavior_velocity_planner', 'motion_velocity_planner', 'planning_validator'],
            }
            for mode in (list(expected) if MODE == 'full' else [MODE]):
                for name in expected.get(mode, []):
                    wait(lambda: any(node.endswith('/' + name) for node in names()))
                    node = next(node for node in names() if node.endswith('/' + name))
                    parameter(node, 'use_sim_time')
            if MODE in ('planning', 'full'):
                node = '/planning/scenario_planning/lane_driving/behavior_planning/behavior_path_planner'
                self.assertFalse(parameter(node, 'use_sim_time').bool_value)
            if MODE == 'full':
                for node in ['/perception/object_recognition/tracking/multi_object_tracker',
                             '/perception/object_recognition/prediction/map_based_prediction']:
                    parameter(node, 'use_sim_time')
                node = '/perception/object_recognition/prediction/map_based_prediction'
                self.assertTrue(parameter(node, 'lane_change_path_suppression.enable').bool_value)
                self.assertTrue(parameter(node, 'lane_change_path_suppression.keep_when_lane_ends').bool_value)
                self.assertEqual(parameter(node, 'lane_change_path_suppression.lane_end_rescue_time_horizon').double_value, 5.0)
                self.assertNotIn('/rviz2', names())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class Shutdown(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
