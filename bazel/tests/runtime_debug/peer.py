# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Small live peer for the extracted distribution's ROS CLI."""
import rclpy
from autoware_planning_msgs.msg import Trajectory
from rclpy.node import Node
from std_msgs.msg import String
from std_srvs.srv import Trigger


class Peer(Node):
    def __init__(self):
        super().__init__('debug_bundle_peer')
        self.declare_parameter('debug_value', 42)
        self.last_input = ''
        self.publisher = self.create_publisher(Trajectory, '/debug_bundle/trajectory', 10)
        self.subscription = self.create_subscription(String, '/debug_bundle/input', self.receive, 10)
        self.service = self.create_service(Trigger, '/debug_bundle/received', self.received)
        self.timer = self.create_timer(0.1, self.publish)
        print('PEER_READY', flush=True)

    def receive(self, message):
        self.last_input = message.data

    def received(self, request, response):
        response.success = bool(self.last_input)
        response.message = self.last_input
        return response

    def publish(self):
        message = Trajectory()
        message.header.frame_id = 'debug_bundle_frame'
        self.publisher.publish(message)


rclpy.init()
node = Peer()
try:
    rclpy.spin(node)
except KeyboardInterrupt:
    pass
finally:
    node.destroy_node()
    if rclpy.ok():
        rclpy.shutdown()
