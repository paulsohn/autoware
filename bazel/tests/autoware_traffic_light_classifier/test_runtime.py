# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise real CNN/lamp node adapters with car/pedestrian models over DDS."""
import math
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_prefix, get_package_share_directory
from sensor_msgs.msg import Image, RegionOfInterest
from tier4_perception_msgs.msg import TrafficLightArray, TrafficLightRoi, TrafficLightRoiArray
import rclpy

PACKAGE = 'autoware_traffic_light_classifier'


class ClassifierNodes(unittest.TestCase):
    def test_car_and_pedestrian_backends(self):
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            for name in ['traffic_light_classifier_mobilenetv2_batch_6.onnx',
                         'ped_traffic_light_classifier_mobilenetv2_batch_6.onnx',
                         'traffic_light_lamp_recognizer_comlops.onnx']:
                shutil.copyfile(models / name, cache / name)
            for kind, pedestrian in [(1, False), (1, True), (2, False), (2, True)]:
                with self.subTest(backend=kind, pedestrian=pedestrian):
                    self.run_backend(kind, pedestrian, models, cache)
            self.assertEqual(len(list(cache.glob('*.engine'))), 3)

    def run_backend(self, kind, pedestrian, models, cache):
        rclpy.init()
        peer = rclpy.create_node('traffic_classifier_peer')
        prefix = '/traffic_gpu_fixture'
        signals = []
        sub = peer.create_subscription(TrafficLightArray, prefix + '/output/traffic_signals', signals.append, 10)
        images = peer.create_publisher(Image, prefix + '/input/image', 1)
        rois = peer.create_publisher(TrafficLightRoiArray, prefix + '/input/rois', 1)
        share = Path(get_package_share_directory(PACKAGE))
        executable = Path(get_package_prefix(PACKAGE)).resolve() / 'lib' / PACKAGE / 'traffic_light_classifier_node'
        model = ('ped_' if pedestrian else '') + 'traffic_light_classifier_mobilenetv2_batch_6.onnx'
        if kind == 2:
            model = 'traffic_light_lamp_recognizer_comlops.onnx'
        config = 'pedestrian' if pedestrian else 'car'
        args = [str(executable), '--ros-args', '-r', '__node:=traffic_gpu_fixture',
                '--params-file', str(share / 'config' / (config + '_traffic_light_classifier.param.yaml')),
                '--params-file', str(models / 'lamp_recognizer_ml.param.yaml')]
        params = {'build_only': 'false', 'classifier_type': str(kind), 'model_path': str(cache / model),
                  'label_path': str(models / ('lamp_labels_ped.txt' if pedestrian else 'lamp_labels.txt')),
                  'over_exposure_threshold': '2.0', 'under_exposure_threshold': '-2.0'}
        for name, value in params.items():
            args += ['-p', name + ':=' + value]

        def publish(empty=False):
            image = Image(height=32, width=32, encoding='rgb8', step=96, data=bytes([100, 100, 100]) * 1024)
            image.header.stamp = peer.get_clock().now().to_msg()
            image.header.frame_id = 'camera'
            message = TrafficLightRoiArray()
            message.header = image.header
            if not empty:
                message.rois = [TrafficLightRoi(traffic_light_id=17, traffic_light_type=int(pedestrian),
                                               roi=RegionOfInterest(x_offset=0, y_offset=0, width=32, height=32))]
            images.publish(image)
            rois.publish(message)

        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def wait(predicate, send=False, empty=False):
                    deadline = time.monotonic() + 120
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if send:
                            publish(empty)
                        rclpy.spin_once(peer, timeout_sec=0.05)
                        if predicate():
                            return
                    self.fail('Traffic classifier output absent: ' + output())

                try:
                    wait(lambda: bool(signals) and bool(signals[-1].signals), send=True)
                    result = signals[-1]
                    self.assertEqual(result.header.frame_id, 'camera')
                    self.assertEqual(len(result.signals), 1)
                    self.assertEqual(result.signals[0].traffic_light_id, 17)
                    self.assertEqual(result.signals[0].traffic_light_type, int(pedestrian))
                    self.assertTrue(result.signals[0].elements)
                    for element in result.signals[0].elements:
                        self.assertTrue(math.isfinite(element.confidence))
                    signals.clear()
                    wait(lambda: bool(signals) and not signals[-1].signals, send=True, empty=True)
                    self.assertEqual(signals[-1].header.frame_id, 'camera')
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Traffic classifier shutdown timed out: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
