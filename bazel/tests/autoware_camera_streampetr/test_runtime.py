# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the indexed five-camera native launch, temporal state and engine reuse."""
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
from autoware_internal_debug_msgs.msg import Float64Stamped
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
import yaml

LAUNCH = str(Path(sys.argv.pop(1)).resolve())
PACKAGE = 'autoware_camera_streampetr'


def identity(path):
    with path.open('rb') as stream:
        return path.stat().st_mtime_ns, hashlib.file_digest(stream, 'sha256').hexdigest()


class StreamPetr(unittest.TestCase):
    def test_indexed_defaults(self):
        share = Path(get_package_share_directory(PACKAGE))
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        defaults = {a.get('name'): a.get('default') for a in E.parse(
            share / 'launch/streampetr.launch.xml').findall('arg') if a.get('default') is not None}
        context = LaunchContext()
        resolve = lambda text: perform_substitutions(context, parse_substitution(text))
        self.assertNotIn('data_path', defaults)
        self.assertEqual(Path(resolve(defaults['model_path'])), models)
        context.launch_configurations['model_path'] = str(models)
        self.assertTrue(Path(resolve(defaults['model_param_path'])).is_file())
        self.assertTrue(Path(resolve(defaults['node_param_path'])).is_file())
        self.assertIn(PACKAGE + '_models', [e.text for e in E.parse(share / 'package.xml').findall('exec_depend')])

    def test_native_cold_warm_temporal_and_sync_reset(self):
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        inputs = list(models.glob('*.onnx'))
        self.assertEqual(len(inputs), 3)
        before = {p.name: identity(p) for p in inputs}
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary) / 'models'
            shutil.copytree(models, cache)
            for run in range(2):
                self.run_profile(cache)
                engines = list(cache.glob('*.engine'))
                self.assertEqual(len(engines), 3)
                self.assertTrue(all(p.stat().st_size > 0 for p in engines))
                current = {p.name: identity(p) for p in engines}
                if run == 0:
                    prepared = current
                else:
                    self.assertEqual(current, prepared)
        self.assertEqual({p.name: identity(p) for p in inputs}, before)

    def run_profile(self, cache):
        share = Path(get_package_share_directory(PACKAGE))
        rclpy.init()
        peer = rclpy.create_node('streampetr_fixture_peer')
        outputs, timings = [], []
        peer.create_subscription(DetectedObjects, '/stream_petr/output/objects', outputs.append, 10)
        peer.create_subscription(Float64Stamped, '/stream_petr/latency/inference', timings.append, 10)
        images = [peer.create_publisher(Image, '/sensing/camera/camera' + str(i) + '/image_rect_color',
                  qos_profile_sensor_data) for i in range(5)]
        infos = [peer.create_publisher(CameraInfo, '/sensing/camera/camera' + str(i) + '/camera_info',
                 qos_profile_sensor_data) for i in range(5)]
        transforms = peer.create_publisher(TFMessage, '/tf_static',
            QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
        transform = TransformStamped(child_frame_id='base_link')
        transform.header.frame_id = 'map'
        transform.transform.rotation.w = 1.
        static = TFMessage(transforms=[transform])
        command = [LAUNCH, str(share / 'launch/streampetr.launch.xml'), 'model_path:=' + str(cache)]
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
                        publish()
                        rclpy.spin_once(peer, timeout_sec=.05)

                def wait(condition, publish=lambda: None, seconds=600):
                    until = time.monotonic() + seconds
                    while time.monotonic() < until and not condition():
                        pump(.1, publish)
                    self.assertTrue(condition(), console())

                def calibration():
                    transforms.publish(static)
                    for i, publisher in enumerate(infos):
                        focal = 400. + i * 10.
                        info = CameraInfo(width=640, height=480, distortion_model='plumb_bob')
                        info.header.frame_id = 'base_link'
                        info.d = [0.] * 5
                        info.k = [focal, 0., 320., 0., focal, 240., 0., 0., 1.]
                        info.r = [1., 0., 0., 0., 1., 0., 0., 0., 1.]
                        info.p = [focal, 0., 320., 0., 0., focal, 240., 0., 0., 0., 1., 0.]
                        publisher.publish(info)

                def publish(indices=range(1, 5), stamp=None):
                    calibration()
                    stamp = stamp or peer.get_clock().now().to_msg()
                    for i in indices:
                        msg = Image(height=480, width=640, encoding='rgb8', step=640 * 3)
                        msg.header.frame_id = 'base_link'
                        msg.header.stamp = copy.deepcopy(stamp)
                        msg.data = bytes([32 + i * 16, 64, 128]) * (480 * 640)
                        images[i].publish(msg)

                def batch(stamp):
                    publish(range(1, 5), stamp)
                    pump(.15)
                    publish([0], stamp)

                try:
                    wait(lambda: all(p.get_subscription_count() for p in images + infos), calibration)
                    pump(.5, calibration)
                    # The anchor cannot run until the fifth image has arrived.
                    stamp = peer.get_clock().now().to_msg()
                    pump(.3, lambda: publish([0, 1, 2, 3], stamp))
                    self.assertFalse(outputs, console())
                    wait(lambda: bool(outputs and timings), lambda: batch(peer.get_clock().now().to_msg()))
                    self.assertEqual(outputs[-1].header.frame_id, 'base_link')
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
                    # Exercise temporal carry-over with an exact new output timestamp.
                    for reset in [False, True]:
                        outputs.clear()
                        stamp = peer.get_clock().now().to_msg()
                        if reset:
                            old = copy.deepcopy(stamp)
                            old.sec -= 2
                            publish(range(1, 5), old)
                            pump(.2)
                            publish([0], stamp)
                            pump(.2)
                            self.assertFalse(outputs, console())
                            self.assertIn("Couldn't sync cameras", console())
                        wait(lambda: any(msg.header.stamp == stamp for msg in outputs), lambda: batch(stamp))
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=20)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('StreamPETR shutdown timed out: ' + console())
                    self.assertEqual(process.returncode, 0, console())
                    self.assertNotIn('process has died', console())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main(verbosity=2)
