# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Track detections and visualize synchronized raw/compressed images over DDS."""
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import time
import unittest
import zlib

from ament_index_python.packages import get_package_prefix
from autoware_perception_msgs.msg import ObjectClassification
from sensor_msgs.msg import CompressedImage, Image
from tier4_perception_msgs.msg import DetectedObjectWithFeature, DetectedObjectsWithFeature, DynamicObjectArray
import rclpy

PACKAGE = 'autoware_bytetrack'


def black_png(width, height):
    def chunk(kind, value):
        return struct.pack('!I', len(value)) + kind + value + struct.pack('!I', zlib.crc32(kind + value))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress((b'\0' + bytes(width * 3)) * height)) + chunk(b'IEND', b''))


class Tracking(unittest.TestCase):
    def test_raw_and_compressed_visualization(self):
        for raw in [True, False]:
            with self.subTest(raw=raw):
                self.run_transport(raw)

    def run_transport(self, raw):
        rclpy.init()
        peer = rclpy.create_node('bytetrack_peer')
        base = Path(get_package_prefix(PACKAGE)).resolve() / 'lib' / PACKAGE
        tracked, ids, rendered = [], [], []
        subscriptions = [
            peer.create_subscription(DetectedObjectsWithFeature, '/bytetrack/out/objects', tracked.append, 10),
            peer.create_subscription(DynamicObjectArray, '/bytetrack/out/objects/debug/uuid', ids.append, 10),
            peer.create_subscription(Image, '/bytetrack_visualizer/out/image', rendered.append, 10),
        ]
        detections = peer.create_publisher(DetectedObjectsWithFeature, '/bytetrack/in/rect', 1)
        image_type = Image if raw else CompressedImage
        image_topic = '/bytetrack_visualizer/in/image' + ('' if raw else '/compressed')
        images = peer.create_publisher(image_type, image_topic, 1)
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                commands = [
                    [str(base / 'bytetrack_node_exe')],
                    [str(base / 'bytetrack_visualizer_node_exe'), '--ros-args', '-p', 'use_raw:=' + str(raw).lower(),
                     '-r', '~/in/rect:=/bytetrack/out/objects',
                     '-r', '~/in/uuid:=/bytetrack/out/objects/debug/uuid'],
                ]
                processes = []
                try:
                    for command in commands:
                        processes.append(subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT))

                    def output():
                        log.seek(0)
                        return log.read()

                    def wait(predicate, publish=None):
                        deadline = time.monotonic() + 15
                        while time.monotonic() < deadline:
                            for process in processes:
                                self.assertIsNone(process.poll(), output())
                            if publish:
                                publish()
                            rclpy.spin_once(peer, timeout_sec=0.03)
                            if predicate():
                                return
                        self.fail('Tracking/visualization output absent: ' + output())

                    wait(lambda: detections.get_subscription_count() > 0 and images.get_subscription_count() > 0)

                    def publish(score=0.9, empty=False):
                        message = DetectedObjectsWithFeature()
                        message.header.stamp = peer.get_clock().now().to_msg()
                        message.header.frame_id = 'camera'
                        if not empty:
                            obj = DetectedObjectWithFeature()
                            obj.feature.roi.x_offset, obj.feature.roi.y_offset = 30, 30
                            obj.feature.roi.width, obj.feature.roi.height = 40, 30
                            obj.object.existence_probability = score
                            obj.object.classification = [ObjectClassification(label=ObjectClassification.CAR, probability=1.0)]
                            message.feature_objects = [obj]
                        if raw:
                            image = Image(height=100, width=120, encoding='bgr8', step=360, data=bytes(36000))
                        else:
                            image = CompressedImage(format='png', data=black_png(120, 100))
                        image.header = message.header
                        images.publish(image)
                        detections.publish(message)

                    wait(lambda: bool(tracked) and bool(ids) and bool(rendered)
                         and bool(tracked[-1].feature_objects) and bool(ids[-1].objects), publish)
                    identity = bytes(ids[-1].objects[0].id.uuid)
                    self.assertNotEqual(identity, bytes(16))
                    self.assertEqual(tracked[-1].header.frame_id, 'camera')
                    self.assertEqual(tracked[-1].feature_objects[0].object.classification[0].label, ObjectClassification.CAR)
                    self.assertEqual((rendered[-1].height, rendered[-1].width, rendered[-1].encoding), (100, 120, 'bgr8'))
                    self.assertTrue(any(rendered[-1].data), 'Bounding box must change black image pixels')
                    tracked.clear()
                    ids.clear()
                    wait(lambda: bool(tracked) and bool(ids) and bool(ids[-1].objects), lambda: publish(0.3))
                    self.assertEqual(bytes(ids[-1].objects[0].id.uuid), identity)
                    tracked.clear()
                    ids.clear()
                    wait(lambda: bool(tracked) and bool(ids) and not tracked[-1].feature_objects and not ids[-1].objects,
                         lambda: publish(empty=True))
                finally:
                    for process in processes:
                        if process.poll() is None:
                            process.send_signal(signal.SIGINT)
                    for process in processes:
                        try:
                            process.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                            self.fail('Tracking process shutdown timed out')
                        log.seek(0)
                        self.assertEqual(process.returncode, 0, log.read())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
