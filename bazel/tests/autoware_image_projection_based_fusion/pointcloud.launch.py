# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Supply the container required by the unchanged shipped component launch."""
from pathlib import Path
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import AnyLaunchDescriptionSource
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        Node(package='rclcpp_components', executable='component_container',
             name='pointcloud_container', output='screen'),
        IncludeLaunchDescription(AnyLaunchDescriptionSource(str(Path(
            get_package_share_directory('autoware_image_projection_based_fusion')) /
            'launch/roi_pointcloud_fusion.launch.xml'))),
    ])
