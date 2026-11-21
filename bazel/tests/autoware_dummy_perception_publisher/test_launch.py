# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise simulated-object lifecycle and both perception executables."""
from pathlib import Path
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_perception_msgs.msg import DetectedObjects, ObjectClassification, PredictedObjects, Shape, TrackedObjects
from autoware_simulation_msgs.msg import SimulatedObject
from geometry_msgs.msg import TransformStamped
import launch
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import AnyLaunchDescriptionSource
from launch_ros.actions import Node
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import pytest
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile
from sensor_msgs.msg import PointCloud2
from tf2_msgs.msg import TFMessage


@pytest.mark.launch_test
def generate_test_description():
    source = Path(get_package_share_directory('autoware_dummy_perception_publisher'))
    return launch.LaunchDescription([
        IncludeLaunchDescription(AnyLaunchDescriptionSource(str(source / 'launch/dummy_perception_publisher.launch.xml')),
                                 launch_arguments={'visible_range': '30.0'}.items()),
        Node(package='autoware_dummy_perception_publisher', executable='empty_objects_publisher', name='empty_probe'),
        launch_testing.actions.ReadyToTest(),
    ])


class TestPerception(unittest.TestCase):
    def test_objects_points_and_empty_publisher(self):
        rclpy.init()
        node = rclpy.create_node('dummy_perception_integration_peer')
        try:
            detected, truth, points, empty = [], [], [], []
            latched = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
            node.create_subscription(DetectedObjects, '/perception/object_recognition/detection/objects', detected.append, latched)
            node.create_subscription(TrackedObjects, '/simulation/dummy_perception_publisher/output/debug/ground_truth_objects',
                                     truth.append, latched)
            node.create_subscription(PointCloud2, '/perception/obstacle_segmentation/pointcloud', points.append, latched)
            node.create_subscription(PredictedObjects, '/empty_probe/output/objects', empty.append, 10)
            objects = node.create_publisher(SimulatedObject, '/simulation/dummy_perception_publisher/object_info', 10)
            transforms = node.create_publisher(TFMessage, '/tf_static', latched)
            transform = TransformStamped()
            transform.header.frame_id = 'map'
            transform.child_frame_id = 'base_link'
            transform.transform.rotation.w = 1.0
            transform.header.stamp = node.get_clock().now().to_msg()
            transforms.publish(TFMessage(transforms=[transform]))

            def wait(condition):
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline:
                    if condition():
                        return
                    rclpy.spin_once(node, timeout_sec=0.05)
                self.fail('Dummy perception did not reach its object/pointcloud gate')

            # Empty output proves the native node's TF listener is ready before ADD.
            wait(lambda: detected and truth and points and empty and objects.get_subscription_count())
            self.assertEqual(empty[-1].header.frame_id, 'map')
            self.assertFalse(empty[-1].objects)
            self.assertGreater(empty[-1].header.stamp.sec, 0)
            obj = SimulatedObject()
            obj.header.frame_id = 'map'
            obj.header.stamp = node.get_clock().now().to_msg()
            obj.id.uuid = [7] * 16
            obj.initial_state.pose_covariance.pose.position.x = 10.0
            obj.initial_state.pose_covariance.pose.orientation.w = 1.0
            obj.classification.label = ObjectClassification.CAR
            obj.classification.probability = 1.0
            obj.shape.type = Shape.BOUNDING_BOX
            obj.shape.dimensions.x, obj.shape.dimensions.y, obj.shape.dimensions.z = 4.0, 2.0, 1.5
            obj.action = SimulatedObject.ADD
            obj.movement_model = SimulatedObject.STRAIGHT_LINE
            objects.publish(obj)
            wait(lambda: len(truth[-1].objects) == 1 and len(detected[-1].objects) == 1 and points[-1].width > 0)
            self.assertEqual(list(truth[-1].objects[0].object_id.uuid), [7] * 16)
            self.assertEqual(truth[-1].header.frame_id, 'map')
            self.assertEqual(detected[-1].header.frame_id, 'base_link')
            self.assertEqual(points[-1].header.frame_id, 'base_link')
            self.assertEqual(detected[-1].objects[0].classification[0].label, ObjectClassification.CAR)
            self.assertAlmostEqual(truth[-1].objects[0].kinematics.pose_with_covariance.pose.position.x, 10.0)
            obj.action = SimulatedObject.MODIFY
            obj.header.stamp = node.get_clock().now().to_msg()
            obj.initial_state.pose_covariance.pose.position.x = 12.0
            objects.publish(obj)
            wait(lambda: abs(truth[-1].objects[0].kinematics.pose_with_covariance.pose.position.x - 12.0) < 1e-6)
            obj.action = SimulatedObject.DELETE
            objects.publish(obj)
            wait(lambda: not truth[-1].objects and not detected[-1].objects and points[-1].width == 0)
        finally:
            node.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_clean_exit(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
