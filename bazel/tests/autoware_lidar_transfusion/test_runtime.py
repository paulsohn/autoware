# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Qualify the indexed TransFusion FP16 profile through its real XML launcher."""
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
import xml.etree.ElementTree as E

from ament_index_python.packages import get_package_share_directory
from autoware_internal_debug_msgs.msg import Float64Stamped
from autoware_perception_msgs.msg import DetectedObjects
from launch import LaunchContext
from launch.frontend.parse_substitution import parse_substitution
from launch.utilities import perform_substitutions
import launch_ros.substitutions
import rclpy
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import PointCloud2, PointField
import yaml

LAUNCH, PARENT_XML, PARENT_MANIFEST = [str(Path(sys.argv.pop(1)).resolve()) for _ in range(3)]
PACKAGE = 'autoware_lidar_transfusion'


def identity(path):
    with path.open('rb') as stream:
        return path.stat().st_mtime_ns, hashlib.file_digest(stream, 'sha256').hexdigest()


def pointcloud():
    points = [(i * .5 - 24., j * .5 - 24., 0., 100 + i % 50, 1, j)
              for i in range(96) for j in range(96)]
    message = PointCloud2(height=1, width=len(points), point_step=16, is_dense=True)
    message.header.frame_id = 'base_link'
    message.header.stamp.sec = 42
    message.header.stamp.nanosec = 170
    message.fields = [PointField(name=n, offset=o, datatype=t, count=1)
        for n, o, t in [('x', 0, PointField.FLOAT32), ('y', 4, PointField.FLOAT32),
            ('z', 8, PointField.FLOAT32), ('intensity', 12, PointField.UINT8),
            ('return_type', 13, PointField.UINT8), ('channel', 14, PointField.UINT16)]]
    message.row_step = message.width * message.point_step
    message.data = b''.join(struct.pack('<fffBBH', *p) for p in points)
    return message


class TransFusion(unittest.TestCase):
    def test_indexed_defaults_and_manifests(self):
        share = Path(get_package_share_directory(PACKAGE))
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        defaults = {e.get('name'): e.get('default') for e in E.parse(share / 'launch/lidar_transfusion.launch.xml').findall('arg') if e.get('default') is not None}
        self.assertEqual(Path(perform_substitutions(LaunchContext(), parse_substitution(defaults['model_path']))), models)
        parent = next(e.get('default') for e in E.parse(PARENT_XML).findall('arg') if e.get('name') == 'transfusion_model_path')
        self.assertEqual(Path(perform_substitutions(LaunchContext(), parse_substitution(parent))), models)
        context = LaunchContext()
        context.launch_configurations.update(model_name='transfusion', model_path=str(models))
        config = yaml.safe_load((share / 'config/transfusion.param.yaml').read_text())['/**']['ros__parameters']
        for expression in [config['onnx_path'], defaults['ml_package_param_path'], defaults['class_remapper_param_path']]:
            path = Path(perform_substitutions(context, parse_substitution(expression)))
            self.assertTrue(path.is_file() and path.is_relative_to(models))
        for path in [share / 'package.xml', Path(PARENT_MANIFEST)]:
            self.assertIn(PACKAGE + '_models', [e.text for e in E.parse(path).findall('exec_depend')])

    def test_cold_and_warm_fp16(self):
        share = Path(get_package_share_directory(PACKAGE))
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        source = models / 'transfusion.onnx'
        before = identity(source)
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary) / 'model'
            shutil.copytree(models, cache)
            config = yaml.safe_load((share / 'config/transfusion.param.yaml').read_text())
            config['/**']['ros__parameters'].update(trt_precision='fp16', cloud_capacity=12000,
                densification_num_past_frames=0, densification_world_frame_id='base_link')
            profile = Path(temporary) / 'profile.yaml'
            profile.write_text(yaml.safe_dump(config))
            self.run_profile(cache, profile)
            engine = cache / 'transfusion.engine'
            self.assertGreater(engine.stat().st_size, 0)
            prepared = identity(engine)
            self.run_profile(cache, profile)
            self.assertEqual(identity(engine), prepared)
        self.assertEqual(identity(source), before)

    def run_profile(self, cache, profile):
        share = Path(get_package_share_directory(PACKAGE))
        rclpy.init()
        peer = rclpy.create_node('transfusion_fixture_peer')
        output, timing = [], []
        peer.create_subscription(DetectedObjects, '/transfusion_gate/output', output.append, 10)
        peer.create_subscription(Float64Stamped, '/lidar_transfusion/debug/processing_time/total_ms', timing.append, 10)
        publisher = peer.create_publisher(PointCloud2, '/transfusion_gate/input', qos_profile_sensor_data)
        sample = pointcloud()
        command = [LAUNCH, str((share / 'launch/lidar_transfusion.launch.xml').resolve()),
            'model_path:=' + str(cache), 'model_param_path:=' + str(profile),
            'use_pointcloud_container:=false', 'input/pointcloud:=/transfusion_gate/input',
            'output/objects:=/transfusion_gate/output']
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)

                def console():
                    log.seek(0)
                    return log.read()

                try:
                    deadline = time.monotonic() + 300
                    while time.monotonic() < deadline and not (output and timing):
                        self.assertIsNone(process.poll(), console())
                        publisher.publish(sample)
                        rclpy.spin_once(peer, timeout_sec=.1)
                    self.assertTrue(output and timing, console())
                    self.assertEqual(output[-1].header, sample.header)
                    self.assertGreater(timing[-1].data, 0.)
                    self.assertTrue(math.isfinite(timing[-1].data))
                    for obj in output[-1].objects:
                        self.assertTrue(obj.classification)
                        self.assertGreaterEqual(obj.existence_probability, 0.)
                        self.assertLessEqual(obj.existence_probability, 1.001)
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
                        self.fail('TransFusion shutdown timed out: ' + console())
                    self.assertEqual(process.returncode, 0, console())
                    self.assertNotIn('process has died', console())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
