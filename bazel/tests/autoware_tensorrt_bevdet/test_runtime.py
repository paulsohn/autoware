# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Qualify the indexed six-camera launch and native engine/cache lifecycle."""
import copy
import hashlib
import math
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
import xml.etree.ElementTree as E

from ament_index_python.packages import get_package_share_directory
from autoware_perception_msgs.msg import DetectedObjects
from geometry_msgs.msg import TransformStamped
from launch import LaunchContext
from launch.frontend.parse_substitution import parse_substitution
from launch.utilities import perform_substitutions
import launch_ros.substitutions
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile, qos_profile_sensor_data
from sensor_msgs.msg import CameraInfo, Image
from tf2_msgs.msg import TFMessage
from visualization_msgs.msg import Marker, MarkerArray
import yaml

LAUNCH = str(Path(sys.argv.pop(1)).resolve())
PACKAGE = 'autoware_tensorrt_bevdet'
CAMERAS = ['front_left', 'front', 'front_right', 'back_left', 'back', 'back_right']


def identity(path):
    with path.open('rb') as stream:
        return path.stat().st_mtime_ns, hashlib.file_digest(stream, 'sha256').hexdigest()


class BEVDet(unittest.TestCase):
    def test_indexed_defaults(self):
        share = Path(get_package_share_directory(PACKAGE))
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        defaults = {a.get('name'): a.get('default') for a in E.parse(
            share / 'launch/tensorrt_bevdet.launch.xml').findall('arg') if a.get('default') is not None}
        context = LaunchContext()
        resolve = lambda text: Path(perform_substitutions(context, parse_substitution(text)))
        self.assertNotIn('data_path', defaults)
        self.assertEqual(resolve(defaults['model_path']), models)
        self.assertTrue(resolve(defaults['model_config']).is_file())
        profile = yaml.safe_load(resolve(defaults['param_file']).read_text())['/**']['ros__parameters']
        context.launch_configurations.update(model_path=str(models), model_name=defaults['model_name'])
        self.assertTrue(resolve(profile['onnx_path']).is_file())
        self.assertIn(PACKAGE + '_models', [e.text for e in E.parse(share / 'package.xml').findall('exec_depend')])

    def test_native_six_camera_cold_warm(self):
        share = Path(get_package_share_directory(PACKAGE))
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        inputs = list(models.glob('*.onnx'))
        self.assertEqual(len(inputs), 1)
        before = {p.name: identity(p) for p in inputs}
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            cache = folder / 'models'
            shutil.copytree(models, cache)
            config = yaml.safe_load((share / 'config/bevdet.param.yaml').read_text())
            config['/**']['ros__parameters'].update(precision='fp16', debug_mode=True)
            profile = folder / 'profile.yaml'
            profile.write_text(yaml.safe_dump(config))
            for run in range(2):
                self.run_profile(cache, profile)
                engines = list(cache.glob('*.engine'))
                self.assertEqual(len(engines), 1)
                self.assertTrue(engines[0].stat().st_size > 0)
                current = {p.name: identity(p) for p in engines}
                if run == 0:
                    prepared = current
                else:
                    self.assertEqual(current, prepared)
        self.assertEqual({p.name: identity(p) for p in inputs}, before)

    def run_profile(self, cache, profile):
        share = Path(get_package_share_directory(PACKAGE))
        rclpy.init()
        peer = rclpy.create_node('bevdet_fixture_peer')
        outputs, markers = [], []
        peer.create_subscription(DetectedObjects, '/bevdet_gate/objects', outputs.append, 10)
        peer.create_subscription(MarkerArray, '/bevdet_gate/markers', markers.append, 10)
        images = [peer.create_publisher(Image, '/bevdet_gate/image' + str(i), qos_profile_sensor_data) for i in range(6)]
        infos = [peer.create_publisher(CameraInfo, '/bevdet_gate/info' + str(i), qos_profile_sensor_data) for i in range(6)]
        transforms = peer.create_publisher(TFMessage, '/tf_static',
            QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
        extrinsics = []
        for i, degrees in enumerate([55, 0, -55, 110, 180, -110]):
            angle = math.radians(degrees) / 2
            c, s = math.cos(angle), math.sin(angle)
            transform = TransformStamped(child_frame_id='bevdet_gate_camera' + str(i))
            transform.header.frame_id = 'base_link'
            transform.transform.translation.z = 1.5
            q = transform.transform.rotation
            q.x, q.y, q.z, q.w = -.5 * (c + s), .5 * (c - s), .5 * (s - c), .5 * (c + s)
            extrinsics.append(transform)
        static = TFMessage(transforms=extrinsics)
        command = [LAUNCH, str(share / 'launch/tensorrt_bevdet.launch.xml'),
                   'model_path:=' + str(cache), 'param_file:=' + str(profile),
                   'output/boxes:=/bevdet_gate/objects', 'output/bboxes:=/bevdet_gate/markers']
        for i, camera in enumerate(CAMERAS):
            command += ['input/img_' + camera + ':=/bevdet_gate/image' + str(i),
                        'input/img_' + camera + '/camera_info:=/bevdet_gate/info' + str(i)]
        sample = Image(width=64, height=36, encoding='bgr8', step=64 * 3)
        sample.header.frame_id = 'base_link'
        sample.header.stamp.sec = 42
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)

                def console():
                    return os.pread(log.fileno(), os.fstat(log.fileno()).st_size, 0).decode(errors='replace')

                def pump(seconds, publish=lambda: None):
                    until = time.monotonic() + seconds
                    while time.monotonic() < until:
                        self.assertIsNone(process.poll(), console())
                        self.assertNotIn('process has died', console())
                        self.assertNotIn('Error Code', console())
                        self.assertNotIn('BEVDet forward failing!', console())
                        publish()
                        rclpy.spin_once(peer, timeout_sec=.05)

                def wait(condition, publish=lambda: None, seconds=600):
                    until = time.monotonic() + seconds
                    while time.monotonic() < until and not condition():
                        pump(.1, publish)
                    self.assertTrue(condition(), console())

                def calibration(indices=range(6)):
                    transforms.publish(static)
                    for i in indices:
                        focal = 800. + i * 10.
                        info = CameraInfo(width=1600, height=900, distortion_model='plumb_bob')
                        info.header = copy.deepcopy(sample.header)
                        info.header.frame_id = 'bevdet_gate_camera' + str(i)
                        info.d = [0.] * 5
                        info.k = [focal, 0., 800., 0., focal, 450., 0., 0., 1.]
                        info.r = [1., 0., 0., 0., 1., 0., 0., 0., 1.]
                        info.p = [focal, 0., 800., 0., 0., focal, 450., 0., 0., 0., 1., 0.]
                        infos[i].publish(info)

                def publish(indices=range(6)):
                    for i in indices:
                        image = copy.deepcopy(sample)
                        image.data = bytes([32 + i * 16, 64, 128]) * (36 * 64)
                        images[i].publish(image)

                try:
                    wait(lambda: all(p.get_subscription_count() for p in infos))
                    pump(.5, lambda: calibration(range(5)))
                    self.assertFalse(any(p.get_subscription_count() for p in images), console())
                    wait(lambda: all(p.get_subscription_count() for p in images), calibration)
                    pump(.3, lambda: publish(range(5)))
                    self.assertFalse(outputs, console())
                    wait(lambda: bool(outputs and markers), publish)
                    self.assertEqual(outputs[-1].header, sample.header)
                    self.assertEqual(markers[-1].markers[0].action, Marker.DELETEALL)
                    self.assertEqual(markers[-1].markers[0].header, sample.header)
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
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=20)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('BEVDet shutdown timed out: ' + console())
                    self.assertEqual(process.returncode, 0, console())
                    self.assertNotIn('process has died', console())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main(verbosity=2)
