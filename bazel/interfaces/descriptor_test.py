# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Declared interfaces retain their canonical ROS package and type namespaces."""
from pathlib import Path
import sys
import unittest

MANIFEST = Path(sys.argv.pop(1))


class TestDescriptors(unittest.TestCase):
    def test_nested_message_namespace(self):
        descriptors = MANIFEST.read_text().splitlines()
        self.assertIn('tier4_perception_msgs/msg/DetectedObjectWithFeature', descriptors)
        self.assertIn('tier4_perception_msgs/msg/TrafficLight', descriptors)
        self.assertNotIn('tier4_perception_msgs/object_recognition/DetectedObjectWithFeature', descriptors)
        self.assertNotIn('tier4_perception_msgs/traffic_light/TrafficLight', descriptors)

    def test_mixed_component_package(self):
        descriptors = MANIFEST.read_text().splitlines()
        for name in ['autoware_control_performance_analysis/msg/Error',
                     'autoware_control_performance_analysis/msg/ErrorStamped',
                     'autoware_control_performance_analysis/msg/FloatStamped',
                     'autoware_control_performance_analysis/msg/DrivingMonitorStamped',
                     'autoware_planning_validator/msg/PlanningValidatorStatus',
                     'autoware_control_validator/msg/ControlValidatorStatus',
                     'autoware_vehicle_cmd_gate/msg/IsFilterActivated',
                     'autoware_operation_mode_transition_manager/msg/OperationModeTransitionManagerDebug',
                     'autoware_adapi_version_msgs/srv/InterfaceVersion',
                     'autoware_adapi_v1_msgs/msg/Heartbeat',
                     'autoware_adapi_v1_msgs/srv/GetVehicleDimensions',
                     'autoware_adapi_v1_msgs/srv/ListMrmDescription',
                     'sensor_msgs/msg/LaserScan',
                     'yabloc_particle_filter/msg/Particle',
                     'yabloc_particle_filter/msg/ParticleArray',
                     'tier4_system_msgs/msg/TrajectorySourceStatus',
                     'tier4_system_msgs/srv/ChangeTrajectorySource',
                     'autoware_trajectory_validator/msg/RiskLevel',
                     'autoware_trajectory_validator/msg/MetricReport',
                     'autoware_trajectory_validator/msg/ValidationReport',
                     'autoware_trajectory_validator/msg/ValidationReportArray']:
            self.assertIn(name, descriptors)

    def test_dependency_repository_package_identity(self):
        descriptors = MANIFEST.read_text().splitlines()
        for interface in ['msg/GridMap', 'msg/GridMapInfo', 'srv/GetGridMap',
                          'srv/GetGridMapInfo', 'srv/SetGridMap', 'srv/ProcessFile']:
            self.assertIn('grid_map_msgs/' + interface, descriptors)
            self.assertNotIn('grid_map/' + interface, descriptors)


if __name__ == '__main__':
    unittest.main()
