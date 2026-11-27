# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Qualify both indexed BEVFusion modalities through the shipped native launch."""
import copy
import hashlib
import math
import os
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
from sensor_msgs.msg import CameraInfo, Image, PointCloud2, PointField
import yaml

LAUNCH = str(Path(sys.argv.pop(1)).resolve())
PACKAGE = 'autoware_bevfusion'


def identity(path):
    with path.open('rb') as stream:
        return path.stat().st_mtime_ns, hashlib.file_digest(stream, 'sha256').hexdigest()


def pointcloud(stamp=42):
    points = [(i * .5 - 8., j * .5 - 8., 0., 100 + i, 1, j)
              for i in range(32) for j in range(32)]
    msg = PointCloud2(height=1, width=len(points), point_step=16, is_dense=True)
    msg.header.frame_id = 'base_link'
    msg.header.stamp.sec = stamp
    msg.fields = [PointField(name=n, offset=o, datatype=t, count=1) for n, o, t in [
        ('x', 0, PointField.FLOAT32), ('y', 4, PointField.FLOAT32), ('z', 8, PointField.FLOAT32),
        ('intensity', 12, PointField.UINT8), ('return_type', 13, PointField.UINT8),
        ('channel', 14, PointField.UINT16)]]
    msg.row_step = msg.width * msg.point_step
    msg.data = b''.join(struct.pack('<fffBBH', *point) for point in points)
    return msg


class BEVFusion(unittest.TestCase):
    def test_indexed_defaults(self):
        share = Path(get_package_share_directory(PACKAGE))
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        defaults = {a.get('name'): a.get('default') for a in E.parse(
            share / 'launch/bevfusion.launch.xml').findall('arg') if a.get('default') is not None}
        self.assertNotIn('data_path', defaults)
        self.assertEqual(Path(perform_substitutions(LaunchContext(),
            parse_substitution(defaults['model_path']))), models)
        for variant in ['bevfusion_lidar', 'bevfusion_camera_lidar']:
            context = LaunchContext()
            context.launch_configurations.update(model_name=variant, model_path=str(models))
            config = yaml.safe_load((share / ('config/' + variant + '.param.yaml')).read_text())['/**']['ros__parameters']
            paths = [value for key, value in config.items() if key.endswith('onnx_path') and value]
            paths += [defaults['ml_package_param_path'], defaults['class_remapper_param_path']]
            for expression in paths:
                path = Path(perform_substitutions(context, parse_substitution(expression)))
                self.assertTrue(path.is_file() and path.is_relative_to(models))
        self.assertIn(PACKAGE + '_models', [e.text for e in E.parse(share / 'package.xml').findall('exec_depend')])

    def test_lidar_cold_warm(self):
        self.qualify('bevfusion_lidar', cameras=False)

    def test_camera_lidar_cold_warm(self):
        self.qualify('bevfusion_camera_lidar', cameras=True)

    def qualify(self, variant, cameras):
        share = Path(get_package_share_directory(PACKAGE))
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        inputs = list(models.glob('*.onnx'))
        self.assertEqual(len(inputs), 3)
        before = {p.name: identity(p) for p in inputs}
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            cache = folder / 'models'
            shutil.copytree(models, cache)
            config = yaml.safe_load((share / ('config/' + variant + '.param.yaml')).read_text())
            config['/**']['ros__parameters'].update(trt_precision='fp16', cloud_capacity=8192,
                densification_num_past_frames=1, densification_world_frame_id='base_link')
            profile = folder / 'profile.yaml'
            profile.write_text(yaml.safe_dump(config))
            ml = yaml.safe_load((cache / ('ml_package_' + variant + '.param.yaml')).read_text())
            ml['/**']['ros__parameters']['voxels_num'] = [1, 2048, 4096]
            if cameras:
                # Keep the model's ROI/features dimensions; scale small synthetic sensor images.
                ml['/**']['ros__parameters'].update(raw_image_height=48, raw_image_width=64,
                    img_aug_scale_x=9., img_aug_scale_y=8.)
            ml_profile = folder / 'ml.yaml'
            ml_profile.write_text(yaml.safe_dump(ml))
            for run in range(2):
                self.run_profile(cache, profile, ml_profile, variant, cameras)
                engines = list(cache.glob('*.engine'))
                self.assertEqual(len(engines), 2 if cameras else 1)
                self.assertTrue(all(p.stat().st_size > 0 for p in engines))
                current = {p.name: identity(p) for p in engines}
                if run == 0:
                    prepared = current
                else:
                    self.assertEqual(current, prepared)
        self.assertEqual({p.name: identity(p) for p in inputs}, before)

    def run_profile(self, cache, profile, ml_profile, variant, cameras):
        share = Path(get_package_share_directory(PACKAGE))
        rclpy.init()
        peer = rclpy.create_node('bevfusion_fixture_peer')
        outputs, timings = [], []
        prefix = '/bevfusion_gate/' + variant
        peer.create_subscription(DetectedObjects, prefix + '/output', outputs.append, 10)
        peer.create_subscription(Float64Stamped,
            '/perception/detection/bevfusion/debug/processing_time/inference_ms', timings.append, 10)
        source = peer.create_publisher(PointCloud2, prefix + '/input', qos_profile_sensor_data)
        images = [peer.create_publisher(Image, prefix + '/image' + str(i), qos_profile_sensor_data) for i in range(6)] if cameras else []
        calibrations = [peer.create_publisher(CameraInfo, prefix + '/camera' + str(i), qos_profile_sensor_data) for i in range(6)] if cameras else []
        sample = pointcloud()
        command = [LAUNCH, str(share / 'launch/bevfusion.launch.xml'),
            'model_name:=' + variant, 'model_path:=' + str(cache),
            'model_param_path:=' + str(profile), 'ml_package_param_path:=' + str(ml_profile),
            'input/pointcloud:=' + prefix + '/input', 'output/objects:=' + prefix + '/output',
            'use_pointcloud_container:=false', 'use_compressed_images:=false']
        for i in range(6):
            command += ['bevfusion_image' + str(i) + ':=' + prefix + '/image' + str(i),
                        'bevfusion_camera_info' + str(i) + ':=' + prefix + '/camera' + str(i)]
        process = None
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)

                def console():
                    return os.pread(log.fileno(), os.fstat(log.fileno()).st_size, 0).decode(errors='replace')

                def pump(seconds, publish):
                    deadline = time.monotonic() + seconds
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), console())
                        self.assertNotIn('process has died', console())
                        publish()
                        rclpy.spin_once(peer, timeout_sec=.1)

                def wait(condition, publish, seconds=300):
                    deadline = time.monotonic() + seconds
                    while time.monotonic() < deadline and not condition():
                        pump(.1, publish)
                    self.assertTrue(condition(), console())

                def publish(indices=range(6)):
                    source.publish(sample)
                    for i in indices if cameras else []:
                        info = CameraInfo(width=64, height=48, distortion_model='plumb_bob')
                        info.header = copy.deepcopy(sample.header)
                        info.d = [0.] * 5
                        focal = 40. + i * 2.
                        info.k = [focal, 0., 32., 0., focal, 24., 0., 0., 1.]
                        info.r = [1., 0., 0., 0., 1., 0., 0., 0., 1.]
                        info.p = [focal, 0., 32., 0., 0., focal, 24., 0., 0., 0., 1., 0.]
                        calibrations[i].publish(info)
                        image = Image(height=48, width=64, encoding='rgb8', step=64*3)
                        image.header = copy.deepcopy(sample.header)
                        image.data = bytes([32 + i * 16, 64, 128]) * (48*64)
                        images[i].publish(image)

                try:
                    wait(lambda: source.get_subscription_count() > 0, lambda: None)
                    if cameras:
                        wait(lambda: all(p.get_subscription_count() for p in images + calibrations), lambda: source.publish(sample))
                        pump(.5, lambda: publish(range(5)))
                        self.assertFalse(outputs, console())
                    wait(lambda: bool(outputs and timings), publish)
                    self.assertEqual(outputs[-1].header, sample.header)
                    self.assertGreater(timings[-1].data, 0.)
                    self.assertTrue(math.isfinite(timings[-1].data))
                    for obj in outputs[-1].objects:
                        self.assertTrue(obj.classification)
                        self.assertTrue(0. <= obj.existence_probability <= 1.001)
                        pose = obj.kinematics.pose_with_covariance.pose
                        values = [pose.position.x, pose.position.y, pose.position.z,
                            pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w,
                            obj.shape.dimensions.x, obj.shape.dimensions.y, obj.shape.dimensions.z]
                        self.assertTrue(all(math.isfinite(v) for v in values))
                    outputs.clear()
                    sample.header.stamp.sec = 43
                    wait(lambda: any(msg.header.stamp.sec == 43 for msg in outputs), publish)
                    self.assertEqual(next(msg.header for msg in outputs if msg.header.stamp.sec == 43), sample.header)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('BEVFusion shutdown timed out: ' + console())
                    self.assertEqual(process.returncode, 0, console())
                    self.assertNotIn('process has died', console())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main(verbosity=2)
