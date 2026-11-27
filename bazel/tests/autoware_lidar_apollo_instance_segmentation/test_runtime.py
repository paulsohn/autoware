# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run all Apollo model choices through their indexed XML, with writable caches."""
import hashlib
import math
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_internal_debug_msgs.msg import Float64Stamped
import rclpy
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import PointCloud2, PointField
from tier4_perception_msgs.msg import DetectedObjectsWithFeature

LAUNCH = str(Path(sys.argv.pop(1)).resolve())
PACKAGE = 'autoware_lidar_apollo_instance_segmentation'


def sample():
    points = [(10. + i * .05, j * .05, z, 180)
              for i in range(8) for j in range(4) for z in (0., 1.)]
    message = PointCloud2(height=1, width=len(points), is_dense=True, point_step=16)
    message.header.frame_id = 'base_link'
    message.header.stamp.sec = 42
    message.header.stamp.nanosec = 170
    message.fields = [PointField(name=name, offset=offset, datatype=datatype, count=1)
                      for name, offset, datatype in [('x', 0, PointField.FLOAT32),
                          ('y', 4, PointField.FLOAT32), ('z', 8, PointField.FLOAT32),
                          ('intensity', 12, PointField.UINT8)]]
    message.row_step = message.width * message.point_step
    message.data = b''.join(struct.pack('<fffB3x', *point) for point in points)
    return message


class Apollo(unittest.TestCase):
    def run_model(self, choice, basename):
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        source = models / (basename + '.onnx')
        source_identity = (source.stat().st_mtime_ns, hashlib.sha256(source.read_bytes()).hexdigest())
        with tempfile.TemporaryDirectory() as temporary:
            model = Path(temporary) / source.name
            shutil.copyfile(source, model)
            self.run_profile(choice, model)
            engine = model.with_suffix('.engine')
            self.assertGreater(engine.stat().st_size, 0)
            identity = (engine.stat().st_mtime_ns, hashlib.sha256(engine.read_bytes()).hexdigest())
            self.run_profile(choice, model)
            self.assertEqual(identity, (engine.stat().st_mtime_ns, hashlib.sha256(engine.read_bytes()).hexdigest()))
        self.assertEqual(source_identity, (source.stat().st_mtime_ns, hashlib.sha256(source.read_bytes()).hexdigest()))

    def run_profile(self, choice, model):
        rclpy.init()
        peer = rclpy.create_node('apollo_fixture_peer')
        output, timing = [], []
        peer.create_subscription(DetectedObjectsWithFeature, '/apollo_gate/output', output.append, 10)
        peer.create_subscription(Float64Stamped, '/lidar_apollo_instance_segmentation/debug/processing_time_ms', timing.append, 10)
        publisher = peer.create_publisher(PointCloud2, '/apollo_gate/input', qos_profile_sensor_data)
        share = Path(get_package_share_directory(PACKAGE))
        args = [LAUNCH, str((share / 'launch/lidar_apollo_instance_segmentation.launch.xml').resolve()),
                'model:=' + choice, 'onnx_file:=' + str(model), 'precision:=fp32',
                'target_frame:=base_link', 'input/pointcloud:=/apollo_gate/input',
                'output/objects:=/apollo_gate/output']
        cloud = sample()
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def console():
                    log.seek(0)
                    return log.read()

                def wait(predicate, publish=False):
                    deadline = time.monotonic() + 240
                    while not predicate() and time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), console())
                        if publish:
                            publisher.publish(cloud)
                        rclpy.spin_once(peer, timeout_sec=.1)
                    self.assertTrue(predicate(), console())

                try:
                    wait(lambda: publisher.get_subscription_count() > 0)
                    empty = PointCloud2()
                    empty.header = cloud.header
                    for _ in range(4):
                        publisher.publish(empty)
                        rclpy.spin_once(peer, timeout_sec=.05)
                    self.assertFalse(output)
                    wait(lambda: bool(output) and bool(timing), publish=True)
                    result = output[-1]
                    self.assertEqual(result.header, cloud.header)
                    self.assertTrue(math.isfinite(timing[-1].data))
                    self.assertGreater(timing[-1].data, 0.)
                    for feature in result.feature_objects:
                        obj = feature.object
                        self.assertTrue(math.isfinite(obj.existence_probability))
                        self.assertGreaterEqual(obj.existence_probability, 0.)
                        self.assertLessEqual(obj.existence_probability, 1.)
                        self.assertTrue(obj.classification)
                        self.assertEqual(feature.feature.cluster.header, cloud.header)
                        pose = obj.kinematics.pose_with_covariance.pose
                        for value in (pose.position.x, pose.position.y, pose.position.z,
                                      pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w):
                            self.assertTrue(math.isfinite(value))
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Apollo shutdown timed out: ' + console())
                    self.assertEqual(process.returncode, 0, console())
                    self.assertNotIn('process has died', console())
        finally:
            peer.destroy_node()
            rclpy.shutdown()

    def test_model_16(self): self.run_model('model_16', 'vlp-16')
    def test_model_64(self): self.run_model('model_64', 'hdl-64')
    def test_model_128(self): self.run_model('model_128', 'vls-128')


if __name__ == '__main__':
    unittest.main()
