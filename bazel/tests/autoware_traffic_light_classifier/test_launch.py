# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run both original XML entrypoints with the supported HSV backend."""
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from sensor_msgs.msg import Image, RegionOfInterest
from tier4_perception_msgs.msg import TrafficLightArray, TrafficLightRoi, TrafficLightRoiArray
import rclpy

LAUNCH = str(Path(sys.argv.pop(1)).resolve())


class Launchers(unittest.TestCase):
    def test_hsv_car_and_pedestrian(self):
        share = Path(get_package_share_directory('autoware_traffic_light_classifier'))
        for kind, filename in enumerate(['car_traffic_light_classifier.launch.xml', 'pedestrian_traffic_light_classifier.launch.xml']):
            with self.subTest(launcher=filename):
                rclpy.init()
                peer = rclpy.create_node('traffic_launch_peer')
                images = peer.create_publisher(Image, '/launch_fixture/image', 1)
                rois = peer.create_publisher(TrafficLightRoiArray, '/launch_fixture/rois', 1)
                observed = []
                sub = peer.create_subscription(TrafficLightArray, '/launch_fixture/signals', observed.append, 10)
                try:
                    with tempfile.TemporaryFile(mode='w+') as log:
                        args = [LAUNCH, str((share / 'launch' / filename).resolve()), 'classifier_type:=0',
                                'input/image:=/launch_fixture/image', 'input/rois:=/launch_fixture/rois',
                                'output/traffic_signals:=/launch_fixture/signals']
                        process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                        def output():
                            log.seek(0)
                            return log.read()

                        try:
                            deadline = time.monotonic() + 15
                            while time.monotonic() < deadline and not observed:
                                self.assertIsNone(process.poll(), output())
                                image = Image(height=16, width=16, encoding='rgb8', step=48, data=bytes([100, 100, 100]) * 256)
                                image.header.frame_id = 'camera'
                                image.header.stamp = peer.get_clock().now().to_msg()
                                message = TrafficLightRoiArray()
                                message.header = image.header
                                message.rois = [TrafficLightRoi(traffic_light_id=27, traffic_light_type=kind,
                                                                roi=RegionOfInterest(width=16, height=16))]
                                images.publish(image)
                                rois.publish(message)
                                rclpy.spin_once(peer, timeout_sec=0.05)
                            self.assertTrue(observed, output())
                            self.assertEqual(observed[-1].signals[0].traffic_light_id, 27)
                            self.assertEqual(observed[-1].signals[0].traffic_light_type, kind)
                        finally:
                            if process.poll() is None:
                                process.send_signal(signal.SIGINT)
                            try:
                                process.wait(timeout=10)
                            except subprocess.TimeoutExpired:
                                process.kill()
                                process.wait()
                                self.fail('XML launch shutdown timed out: ' + output())
                            self.assertEqual(process.returncode, 0, output())
                finally:
                    peer.destroy_node()
                    rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
