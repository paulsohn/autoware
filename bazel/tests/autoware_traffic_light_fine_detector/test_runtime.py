# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise fine-detector GPU inference through its original XML launcher."""
import math
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_internal_debug_msgs.msg import Float32Stamped
from sensor_msgs.msg import Image, RegionOfInterest
from tier4_perception_msgs.msg import TrafficLightRoi, TrafficLightRoiArray
import rclpy

LAUNCH = str(Path(sys.argv.pop(1)).resolve())
PACKAGE = 'autoware_traffic_light_fine_detector'


class FineDetector(unittest.TestCase):
    def test_exact_and_approximate_sync(self):
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            model = cache / 'tlr_car_ped_yolox_s_batch_6.onnx'
            shutil.copyfile(models / model.name, model)
            for approximate in [False, True]:
                with self.subTest(approximate=approximate):
                    self.run_profile(approximate, model, models, cache)
            self.assertGreater(model.with_suffix('.engine').stat().st_size, 0)

    def run_profile(self, approximate, model, models, cache):
        rclpy.init()
        peer = rclpy.create_node('fine_detector_peer')
        output, timing = [], []
        subscriptions = [
            peer.create_subscription(TrafficLightRoiArray, '/fine_fixture/output', output.append, 10),
            peer.create_subscription(Float32Stamped, '/traffic_light_fine_detector/debug/exe_time_ms', timing.append, 10),
        ]
        images = peer.create_publisher(Image, '/fine_fixture/image', 1)
        rough = peer.create_publisher(TrafficLightRoiArray, '/fine_fixture/rough', 1)
        expected = peer.create_publisher(TrafficLightRoiArray, '/fine_fixture/expected', 1)
        share = Path(get_package_share_directory(PACKAGE))
        config = cache / 'profile.yaml'
        config.write_text('/**:\n  ros__parameters:\n    precision: fp16\n    score_thresh: 0.3\n    nms_thresh: 0.65\n    gpu_id: 0\n    approximate_sync: ' + str(approximate).lower() + '\n')
        args = [LAUNCH, str((share / 'launch/traffic_light_fine_detector.launch.xml').resolve()),
                'model_path:=' + str(model), 'label_path:=' + str(models / 'tlr_labels.txt'),
                'param_path:=' + str(config), 'input/image:=/fine_fixture/image',
                'input/rois:=/fine_fixture/rough', 'expect/rois:=/fine_fixture/expected',
                'output/rois:=/fine_fixture/output']

        def publish(empty=False):
            image = Image(height=96, width=128, encoding='rgb8', step=384, data=bytes([100, 100, 100]) * (96 * 128))
            image.header.frame_id = 'camera'
            image.header.stamp = peer.get_clock().now().to_msg()
            regions, projected = TrafficLightRoiArray(), TrafficLightRoiArray()
            regions.header = projected.header = image.header
            if not empty:
                for index, kind in [(11, 0), (12, 1)]:
                    regions.rois.append(TrafficLightRoi(traffic_light_id=index, traffic_light_type=kind,
                                                       roi=RegionOfInterest(width=128, height=96)))
                    projected.rois.append(TrafficLightRoi(traffic_light_id=index, traffic_light_type=kind,
                                                         roi=RegionOfInterest(x_offset=48, y_offset=32, width=32, height=32)))
            images.publish(image)
            rough.publish(regions)
            expected.publish(projected)

        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def console():
                    log.seek(0)
                    return log.read()

                def wait(predicate, empty=False):
                    deadline = time.monotonic() + 150
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), console())
                        publish(empty)
                        rclpy.spin_once(peer, timeout_sec=0.05)
                        if predicate():
                            return
                    self.fail('Fine-detector output absent: ' + console())

                try:
                    wait(lambda: bool(output) and len(output[-1].rois) == 2 and bool(timing))
                    self.assertEqual(output[-1].header.frame_id, 'camera')
                    self.assertEqual({(r.traffic_light_id, r.traffic_light_type) for r in output[-1].rois}, {(11, 0), (12, 1)})
                    for roi in output[-1].rois:
                        self.assertLessEqual(roi.roi.x_offset + roi.roi.width, 128)
                        self.assertLessEqual(roi.roi.y_offset + roi.roi.height, 96)
                    self.assertTrue(math.isfinite(timing[-1].data))
                    self.assertGreater(timing[-1].data, 0.0)
                    self.assertIn('GPU 0 is selected for the inference', console())
                    output.clear()
                    wait(lambda: bool(output) and not output[-1].rois, empty=True)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Fine-detector launch shutdown timed out: ' + console())
                    self.assertEqual(process.returncode, 0, console())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
