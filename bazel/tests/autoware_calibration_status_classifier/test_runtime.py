# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run the real GPU node through its manual service and preview/diagnostic outputs."""
import math
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_prefix, get_package_share_directory
from diagnostic_msgs.msg import DiagnosticArray
from sensor_msgs.msg import CameraInfo, Image, PointCloud2, PointField
from std_srvs.srv import Trigger
import rclpy
from rclpy.qos import qos_profile_sensor_data

PACKAGE = 'autoware_calibration_status_classifier'


class CalibrationNode(unittest.TestCase):
    def test_manual_inference_and_consumed_input(self):
        rclpy.init()
        peer = rclpy.create_node('calibration_node_peer')
        info_pub = peer.create_publisher(CameraInfo, '/fixture/camera/camera_info', qos_profile_sensor_data)
        image_pub = peer.create_publisher(Image, '/fixture/camera/image', qos_profile_sensor_data)
        cloud_pub = peer.create_publisher(PointCloud2, '/fixture/lidar/points', qos_profile_sensor_data)
        diagnostics, previews = [], []
        subscriptions = [
            peer.create_subscription(DiagnosticArray, '/diagnostics', diagnostics.append, 10),
            peer.create_subscription(Image, '/fixture/camera/points_projected', previews.append, qos_profile_sensor_data),
        ]
        client = peer.create_client(Trigger, '/calibration_status_classifier/input/validate_calibration_srv')
        share = Path(get_package_share_directory(PACKAGE))
        model = Path(get_package_share_directory(PACKAGE + '_models')) / 'calibration_status_classifier.onnx'
        executable = Path(get_package_prefix(PACKAGE)).resolve() / 'lib' / PACKAGE / 'calibration_status_classifier_node'

        def publish():
            stamp = peer.get_clock().now().to_msg()
            image = Image(height=64, width=96, encoding='bgr8', step=288, data=bytes([80, 100, 120]) * (64 * 96))
            image.header.frame_id, image.header.stamp = 'camera', stamp
            info = CameraInfo(height=64, width=96, distortion_model='rational_polynomial', d=[0.0] * 8,
                              k=[40.0, 0.0, 48.0, 0.0, 40.0, 32.0, 0.0, 0.0, 1.0],
                              r=[1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0],
                              p=[40.0, 0.0, 48.0, 0.0, 0.0, 40.0, 32.0, 0.0, 0.0, 0.0, 1.0, 0.0])
            info.header = image.header
            points = b''.join(struct.pack('<fffBBH', x, y, 5.0, 128, 1, 0)
                              for x in [-1.0, 0.0, 1.0] for y in [-1.0, 0.0, 1.0])
            cloud = PointCloud2(height=1, width=9, point_step=16, row_step=len(points), data=points, is_dense=True,
                                fields=[PointField(name=name, offset=offset, datatype=kind, count=1)
                                        for name, offset, kind in [('x', 0, 7), ('y', 4, 7), ('z', 8, 7),
                                                                   ('intensity', 12, 2), ('return_type', 13, 2), ('channel', 14, 4)]])
            cloud.header = image.header
            info_pub.publish(info)
            image_pub.publish(image)
            cloud_pub.publish(cloud)

        try:
            with tempfile.TemporaryDirectory() as temporary, tempfile.TemporaryFile(mode='w+') as log:
                cached_model = Path(temporary) / model.name
                shutil.copyfile(model, cached_model)
                args = [str(executable), '--ros-args', '--params-file', str(share / 'config/calibration_status_classifier.param.yaml')]
                params = {'onnx_path': str(cached_model), 'max_depth': '128.0', 'dilation_size': '1',
                          'height': '[63,64,65]', 'width': '[95,96,97]', 'cloud_capacity': '100',
                          'runtime_mode': 'manual', 'ego_box': '[0.0,0.0,0.0,0.0,0.0,0.0]', 'input.cloud_topics': '[/fixture/lidar/points]',
                          'input.image_topics': '[/fixture/camera/image]', 'input.already_rectified': '[true]',
                          'prerequisite.linear_velocity_check.enabled': 'false',
                          'prerequisite.angular_velocity_check.enabled': 'false',
                          'prerequisite.objects_check.enabled': 'false'}
                for name, value in params.items():
                    args += ['-p', name + ':=' + value]
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def wait(predicate, send=False):
                    deadline = time.monotonic() + 120
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if send:
                            publish()
                        rclpy.spin_once(peer, timeout_sec=0.05)
                        if predicate():
                            return
                    self.fail('Calibration node output absent: ' + output())

                def request():
                    future = client.call_async(Trigger.Request())
                    wait(future.done)
                    return future.result()

                try:
                    wait(client.service_is_ready, send=True)
                    # ApproximateTime receives two fresh samples to complete a pair.
                    for _ in range(5):
                        publish()
                        rclpy.spin_once(peer, timeout_sec=0.1)
                    response = request()
                    self.assertTrue(response.success, response.message + output())
                    wait(lambda: bool(diagnostics) and bool(previews))
                    values = {v.key: v.value for m in diagnostics for status in m.status
                              if 'Calibration' in status.message for v in status.values}
                    self.assertIn('Inference time (ms)', values, output())
                    for key in ['Calibration confidence', 'Miscalibration confidence', 'Inference time (ms)']:
                        self.assertTrue(math.isfinite(float(values[key])), key)
                    expected = float(values['Calibration confidence']) > float(values['Miscalibration confidence'])
                    self.assertEqual(values['Is calibrated'].lower() in ['true', '1'], expected)
                    self.assertGreater(int(values['Number of points projected']), 0)
                    self.assertEqual((previews[-1].width, previews[-1].height), (96, 64))
                    self.assertGreater(cached_model.with_suffix('.engine').stat().st_size, 0)
                    self.assertFalse(request().success, 'A consumed sensor pair must not be processed twice')
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Calibration node shutdown timed out: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
