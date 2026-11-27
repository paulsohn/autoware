# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Execute CenterPoint bundle profiles with caller-owned writable engine caches."""
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
from autoware_perception_msgs.msg import DetectedObjects
import rclpy
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import PointCloud2, PointField
import yaml

LAUNCH = str(Path(sys.argv.pop(1)).resolve())
PACKAGE = 'autoware_lidar_centerpoint'


def fingerprint(path):
    return path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest()


def pointcloud():
    points = [(10. + i * .05, j * .05, z, 180, 1, j)
              for i in range(8) for j in range(4) for z in (0., 1.)]
    message = PointCloud2(height=1, width=len(points), point_step=16, is_dense=True)
    message.header.frame_id = 'base_link'
    message.header.stamp.sec = 42
    message.header.stamp.nanosec = 170
    message.fields = [PointField(name=n, offset=o, datatype=t, count=1)
                      for n, o, t in [('x', 0, PointField.FLOAT32), ('y', 4, PointField.FLOAT32),
                          ('z', 8, PointField.FLOAT32), ('intensity', 12, PointField.UINT8),
                          ('return_type', 13, PointField.UINT8), ('channel', 14, PointField.UINT16)]]
    message.row_step = message.point_step * message.width
    message.data = b''.join(struct.pack('<fffBBH', *p) for p in points)
    return message


class CenterPoint(unittest.TestCase):
    def run_bundle(self, bundle):
        source = Path(get_package_share_directory(PACKAGE + '_models')) / bundle
        identities = {p.name: fingerprint(p) for p in source.glob('*.onnx')}
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary) / bundle
            shutil.copytree(source, cache)
            path = cache / 'ml_package.param.yaml'
            parameters = yaml.safe_load(path.read_text())
            parameters['/**']['ros__parameters']['trt_precision'] = 'fp16'
            path.write_text(yaml.safe_dump(parameters))
            common = Path(temporary) / 'common.yaml'
            parameters = yaml.safe_load((Path(get_package_share_directory(PACKAGE)) /
                'config/centerpoint_common.param.yaml').read_text())
            parameters['/**']['ros__parameters'].update(cloud_capacity=1024,
                densification_params={'world_frame_id': 'base_link', 'num_past_frames': 0})
            common.write_text(yaml.safe_dump(parameters))
            self.run_profile(cache, common)
            engines = list(cache.glob('*.engine'))
            self.assertEqual(len(engines), 2)
            self.assertTrue(all(p.stat().st_size > 0 for p in engines))
            prepared = {p.name: fingerprint(p) for p in engines}
            self.run_profile(cache, common)
            self.assertEqual(prepared, {p.name: fingerprint(p) for p in engines})
        self.assertEqual(identities, {p.name: fingerprint(p) for p in source.glob('*.onnx')})

    def run_profile(self, cache, common):
        rclpy.init()
        peer = rclpy.create_node('centerpoint_fixture_peer')
        output, timing = [], []
        peer.create_subscription(DetectedObjects, '/centerpoint_gate/output', output.append, 10)
        peer.create_subscription(Float64Stamped, '/lidar_centerpoint/debug/processing_time_ms', timing.append, 10)
        publisher = peer.create_publisher(PointCloud2, '/centerpoint_gate/input', qos_profile_sensor_data)
        share = Path(get_package_share_directory(PACKAGE))
        command = [LAUNCH, str((share / 'launch/lidar_centerpoint.launch.xml').resolve()),
            'model_path:=' + str(cache), 'common_param_path:=' + str(common),
            'use_pointcloud_container:=false', 'input/pointcloud:=/centerpoint_gate/input',
            'output/objects:=/centerpoint_gate/output']
        sample = pointcloud()
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)

                def console():
                    log.seek(0)
                    return log.read()

                def wait(predicate, publish=False):
                    deadline = time.monotonic() + 240
                    while time.monotonic() < deadline and not predicate():
                        self.assertIsNone(process.poll(), console())
                        if publish:
                            publisher.publish(sample)
                        rclpy.spin_once(peer, timeout_sec=.1)
                    self.assertTrue(predicate(), console())

                try:
                    wait(lambda: publisher.get_subscription_count() > 0)
                    wait(lambda: bool(output) and bool(timing), publish=True)
                    message = output[-1]
                    self.assertEqual(message.header, sample.header)
                    self.assertTrue(math.isfinite(timing[-1].data))
                    self.assertGreater(timing[-1].data, 0.)
                    for obj in message.objects:
                        self.assertTrue(obj.classification)
                        self.assertGreaterEqual(obj.existence_probability, 0.)
                        self.assertLessEqual(obj.existence_probability, 1.)
                        pose = obj.kinematics.pose_with_covariance.pose
                        values = [pose.position.x, pose.position.y, pose.position.z,
                            pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w,
                            obj.shape.dimensions.x, obj.shape.dimensions.y, obj.shape.dimensions.z]
                        self.assertTrue(all(math.isfinite(v) for v in values))
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('CenterPoint shutdown timed out: ' + console())
                    self.assertEqual(process.returncode, 0, console())
                    self.assertNotIn('process has died', console())
        finally:
            peer.destroy_node()
            rclpy.shutdown()

    def test_base(self): self.run_bundle('base')
    def test_short_range(self): self.run_bundle('short_range')
    def test_sigma(self): self.run_bundle('sigma')
    def test_tiny(self): self.run_bundle('tiny')


if __name__ == '__main__':
    unittest.main()
