"""Verify C application typesupport and serialized publish/take on the Universe wire types."""
import sys
import time

import rclpy
from autoware_sensing_msgs.msg import ConcatenatedPointCloudInfo
from rclpy.serialization import deserialize_message, serialize_message
from sensor_msgs.msg import PointCloud2


def check(node, value):
    cls = type(value)
    topic = '/check_' + cls.__name__
    plain, raw = [], []
    pub = node.create_publisher(cls, topic, 4)
    sub = node.create_subscription(cls, topic, plain.append, 4)
    encoded_sub = node.create_subscription(cls, topic, raw.append, 4, raw=True)
    try:
        for message in (value, serialize_message(value)):
            pub.publish(message)
            deadline = time.monotonic() + 3
            while (not plain or not raw) and time.monotonic() < deadline:
                rclpy.spin_once(node, timeout_sec=0.1)
            assert len(plain) == len(raw) == 1
            assert plain.pop() == deserialize_message(raw.pop(), cls) == value
    finally:
        node.destroy_subscription(encoded_sub)
        node.destroy_subscription(sub)
        node.destroy_publisher(pub)


rclpy.init()
node = rclpy.create_node('cloud_wire_check')
try:
    check(node, PointCloud2(height=1, width=4, point_step=1, row_step=4, data=[0, 42, 255, 9]))
    check(node, ConcatenatedPointCloudInfo(concatenation_success=True, matching_strategy_config=[1, 42, 0]))
    print('typed and serialized publish/take passed for both Universe wire types')
    if '--nested' in sys.argv:
        from tier4_perception_msgs.msg import DetectedObjectWithFeature, DetectedObjectsWithFeature

        check(node, DetectedObjectsWithFeature())
        nested = DetectedObjectsWithFeature()
        for payload in ([], [0, 42, 255, 9], [8, 0, 77]):
            obj = DetectedObjectWithFeature()
            obj.feature.cluster = PointCloud2(height=1, width=len(payload), point_step=1,
                                             row_step=len(payload), data=payload)
            nested.feature_objects.append(obj)
        check(node, nested)
        print('typed and serialized publish/take passed for cloud, metadata, empty and populated nested clusters')
finally:
    node.destroy_node()
    rclpy.shutdown()
