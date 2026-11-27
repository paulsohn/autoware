# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Qualify shipped fusion launches with calibrated synthetic camera/LiDAR inputs."""
from contextlib import contextmanager
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

from ament_index_python.packages import get_package_share_directory
from autoware_perception_msgs.msg import DetectedObject, DetectedObjects, ObjectClassification, Shape
from sensor_msgs.msg import CameraInfo, Image, PointCloud2, PointField
from tier4_perception_msgs.msg import DetectedObjectsWithFeature, DetectedObjectWithFeature
import rclpy
import yaml

LAUNCH = str(Path(sys.argv.pop(1)).resolve())
PACKAGE = 'autoware_image_projection_based_fusion'


def identity(path):
    with path.open('rb') as stream:
        return path.stat().st_mtime_ns, hashlib.file_digest(stream, 'sha256').hexdigest()


def cloud(points=None, stamp=42):
    points = points or [(-.1, -.1, 10.), (-.1, .1, 10.), (.1, .1, 10.), (.1, -.1, 10.)]
    msg = PointCloud2(height=1, width=len(points), point_step=16, is_dense=True)
    msg.header.frame_id = 'base_link'
    msg.header.stamp.sec = stamp
    msg.row_step = msg.width * 16
    msg.fields = [PointField(name=n, offset=i * 4, datatype=PointField.FLOAT32, count=1)
                  for i, n in enumerate(['x', 'y', 'z', 'intensity'])]
    msg.data = b''.join(struct.pack('<ffff', *p, 50.) for p in points)
    return msg


def classification(label):
    return ObjectClassification(label=label, probability=1.)


def rois(label=ObjectClassification.CAR, stamp=42, rectangle=(27, 19, 10, 10)):
    msg = DetectedObjectsWithFeature()
    msg.header.frame_id = 'base_link'
    msg.header.stamp.sec = stamp
    obj = DetectedObjectWithFeature()
    obj.object.classification = [classification(label)]
    obj.object.existence_probability = .9
    roi = obj.feature.roi
    roi.x_offset, roi.y_offset, roi.width, roi.height = rectangle
    msg.feature_objects = [obj]
    return msg


def camera(focal):
    msg = CameraInfo(width=64, height=48, distortion_model='plumb_bob')
    msg.header.frame_id = 'base_link'
    msg.d = [0.] * 5
    msg.k = [focal, 0., 32., 0., focal, 24., 0., 0., 1.]
    msg.r = [1., 0., 0., 0., 1., 0., 0., 0., 1.]
    msg.p = [focal, 0., 32., 0., 0., focal, 24., 0., 0., 0., 1., 0.]
    return msg


class FusionLaunch(unittest.TestCase):
    def setUp(self):
        self.share = Path(get_package_share_directory(PACKAGE))
        self.temporary = tempfile.TemporaryDirectory()
        self.folder = Path(self.temporary.name)
        self.topic = '/fusion_gate/' + self._testMethodName
        rclpy.init()
        self.peer = rclpy.create_node('fusion_fixture_peer')
        self.process = None

    def tearDown(self):
        self.peer.destroy_node()
        rclpy.shutdown()
        self.temporary.cleanup()

    def sync_profile(self, debug=False):
        config = yaml.safe_load((self.share / 'config/fusion_common.param.yaml').read_text())
        params = config['/**']['ros__parameters']
        params.update(debug_mode=debug, rois_timestamp_offsets=[0., 0.], rois_timeout_sec=1., msg3d_timeout_sec=1.,
            point_project_to_unrectified_image=[False, False],
            approximate_camera_projection=[False, False])
        params['matching_strategy'] = {'type': 'naive', 'threshold': .05}
        path = self.folder / 'sync.yaml'
        path.write_text(yaml.safe_dump(config))
        return path

    @contextmanager
    def running(self, name, extra=()):
        segmentation = name == 'segmentation_pointcloud_fusion'
        arguments = [('input/camera_number' if segmentation else 'input/rois_number') + ':=2',
                     'sync_param_path:=' + str(self.sync_profile(debug=name == 'pointpainting_fusion'))]
        for i in range(2):
            arguments += [('input/mask' if segmentation else 'input/rois') + str(i) + ':=' + self.topic + '/roi' + str(i),
                          'input/camera_info' + str(i) + ':=' + self.topic + '/camera' + str(i)]
        launch_file = (Path(__file__).with_name('pointcloud.launch.py') if name == 'roi_pointcloud_fusion'
                       else self.share / ('launch/' + name + '.launch.xml'))
        command = [LAUNCH, str(launch_file), *arguments, *extra]
        with tempfile.TemporaryFile(mode='w+') as self.log:
            self.process = subprocess.Popen(command, stdout=self.log, stderr=subprocess.STDOUT)
            try:
                yield
            finally:
                if self.process.poll() is None:
                    self.process.send_signal(signal.SIGINT)
                try:
                    self.process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
                    self.fail('Fusion shutdown timed out: ' + self.console())
                self.assertEqual(self.process.returncode, 0, self.console())
                self.assertNotIn('process has died', self.console())
                self.process = None

    def console(self):
        return os.pread(self.log.fileno(), os.fstat(self.log.fileno()).st_size, 0).decode(errors="replace")

    def pump(self, seconds, publish):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.assertIsNone(self.process.poll(), self.console())
            self.assertNotIn('process has died', self.console())
            publish()
            rclpy.spin_once(self.peer, timeout_sec=.05)

    def wait(self, condition, publish, seconds=30):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline and not condition():
            self.pump(.05, publish)
        self.assertTrue(condition(), self.console())

    def ready(self, pubs, cameras, seconds=30):
        self.wait(lambda: all(p.get_subscription_count() for p in pubs), cameras, seconds)
        # Discovery precedes camera-projector initialization; don't inject a partial startup frame.
        self.wait(lambda: all(f'Camera projector initialization for ROI {i} finished.' in self.console()
                              for i in range(2)), cameras, seconds)

    def publishers(self, input_type, roi_type=DetectedObjectsWithFeature, focal=40.):
        source = self.peer.create_publisher(input_type, self.topic + '/input', 10)
        images = [self.peer.create_publisher(roi_type, self.topic + '/roi' + str(i), 10) for i in range(2)]
        calibration = [self.peer.create_publisher(CameraInfo, self.topic + '/camera' + str(i), 10) for i in range(2)]

        def cameras():
            for i, publisher in enumerate(calibration):
                publisher.publish(camera(focal * (i + 1)))

        def publish(message, rois_pair, indices=(0, 1)):
            cameras()
            source.publish(message)
            for i in indices:
                images[i].publish(rois_pair[i])

        return publish, cameras, [source, *images, *calibration]

    def check_cloud(self, msg):
        self.assertEqual(msg.row_step, msg.width * msg.point_step)
        self.assertEqual(len(msg.data), msg.row_step * msg.height)
        self.assertTrue({'x', 'y', 'z'}.issubset({f.name for f in msg.fields}))

    def test_roi_object_sync_and_history(self):
        outputs = []
        self.peer.create_subscription(DetectedObjects, self.topic + '/output', outputs.append, 10)
        publish, cameras, pubs = self.publishers(DetectedObjects)
        source = DetectedObjects()
        source.header = cloud().header
        obj = DetectedObject(existence_probability=.125)
        obj.classification = [classification(ObjectClassification.CAR)]
        obj.kinematics.pose_with_covariance.pose.position.z = 10.
        obj.kinematics.pose_with_covariance.pose.orientation.w = 1.
        obj.shape.type = Shape.BOUNDING_BOX
        obj.shape.dimensions.x = obj.shape.dimensions.y = obj.shape.dimensions.z = 2.
        source.objects = [obj]
        pair = [rois(), rois(rectangle=(23, 15, 18, 18))]
        with self.running('roi_detected_object_fusion', [
                'input/objects:=' + self.topic + '/input', 'output/objects:=' + self.topic + '/output']):
            self.ready(pubs, cameras)
            self.pump(.2, lambda: publish(source, pair, (0,)))
            self.assertFalse(outputs, self.console())
            self.wait(lambda: bool(outputs), lambda: publish(source, pair))
            self.assertEqual(outputs[-1].header, source.header)
            self.assertEqual(outputs[-1].objects, source.objects)
            outputs.clear()
            old = copy.deepcopy(source)
            old.header.stamp.sec = 41
            late = [rois(stamp=41), rois(stamp=41, rectangle=(23, 15, 18, 18))]
            self.pump(.3, lambda: publish(old, late))
            self.assertFalse(any(msg.header.stamp.sec == 41 for msg in outputs), self.console())
            outputs.clear()
            source.header.stamp.sec = 43
            source.objects = []
            for item in pair:
                item.header.stamp.sec = 43
                item.feature_objects = []
            self.wait(lambda: bool(outputs), lambda: publish(source, pair))
            self.assertEqual(outputs[-1].header, source.header)
            self.assertFalse(outputs[-1].objects)
            outputs.clear()
            source.header.stamp.sec = 20
            for item in pair:
                item.header.stamp.sec = 20
            self.wait(lambda: any(msg.header.stamp.sec == 20 for msg in outputs),
                      lambda: publish(source, pair))
            outputs.clear()
            source.header.stamp.sec = 19
            for item in pair:
                item.header.stamp.sec = 19
            self.pump(.3, lambda: publish(source, pair))
            self.assertFalse(any(msg.header.stamp.sec == 19 for msg in outputs), self.console())

    def test_roi_cluster_labels_points(self):
        outputs = []
        self.peer.create_subscription(DetectedObjectsWithFeature, self.topic + '/output', outputs.append, 10)
        publish, cameras, pubs = self.publishers(DetectedObjectsWithFeature)
        source = DetectedObjectsWithFeature()
        source.header = cloud().header
        obj = DetectedObjectWithFeature()
        obj.object.classification = [classification(ObjectClassification.UNKNOWN)]
        obj.object.kinematics.pose_with_covariance.pose.position.z = 10.
        obj.object.kinematics.pose_with_covariance.pose.orientation.w = 1.
        obj.feature.cluster = cloud()
        source.feature_objects = [obj]
        pair = [rois(rectangle=(31, 23, 1, 1)), rois(rectangle=(31, 23, 2, 2))]
        config = yaml.safe_load((self.share / 'config/roi_cluster_fusion.param.yaml').read_text())
        config['/**']['ros__parameters']['only_allow_inside_cluster'] = False
        path = self.folder / 'cluster.yaml'
        path.write_text(yaml.safe_dump(config))
        with self.running('roi_cluster_fusion', ['input/clusters:=' + self.topic + '/input',
                'output/clusters:=' + self.topic + '/output', 'param_path:=' + str(path)]):
            self.ready(pubs, cameras)
            self.wait(lambda: bool(outputs), lambda: publish(source, pair))
            self.assertEqual(outputs[-1].header, source.header)
            self.assertEqual(len(outputs[-1].feature_objects), 1, self.console())
            result = outputs[-1].feature_objects[0]
            self.assertEqual(result.object.classification[0].label, ObjectClassification.CAR)
            self.assertEqual(bytes(result.feature.cluster.data), bytes(obj.feature.cluster.data))

    def test_roi_pointcloud_emits_cluster(self):
        outputs = []
        self.peer.create_subscription(DetectedObjectsWithFeature, self.topic + '/output', outputs.append, 10)
        publish, cameras, pubs = self.publishers(PointCloud2)
        source = cloud()
        pair = [rois(ObjectClassification.PEDESTRIAN, rectangle=(30, 22, 4, 4)),
                rois(ObjectClassification.PEDESTRIAN, rectangle=(28, 20, 8, 8))]
        with self.running('roi_pointcloud_fusion', ['input/pointcloud:=' + self.topic + '/input',
                'output/clusters:=' + self.topic + '/output']):
            self.ready(pubs, cameras)
            self.wait(lambda: bool(outputs), lambda: publish(source, pair))
            self.assertEqual(outputs[-1].header, source.header)
            self.assertTrue(outputs[-1].feature_objects, self.console())
            for result in outputs[-1].feature_objects:
                self.assertEqual(result.object.classification[0].label, ObjectClassification.PEDESTRIAN)
                self.check_cloud(result.feature.cluster)
                self.assertGreaterEqual(result.feature.cluster.width, 2)

    def test_semantic_mask_and_frame_reset(self):
        outputs = []
        self.peer.create_subscription(PointCloud2, self.topic + '/output', outputs.append, 10)
        publish, cameras, pubs = self.publishers(PointCloud2, Image)
        source = cloud()

        def mask(label, stamp):
            msg = Image(height=48, width=64, encoding='mono8', step=64)
            msg.header = copy.deepcopy(source.header)
            msg.header.stamp.sec = stamp
            msg.data = struct.pack('<Bi', label, 64 * 48)
            return msg

        pair = [mask(9, 42), mask(9, 42)]
        with self.running('segmentation_pointcloud_fusion', [
                'input/pointcloud:=' + self.topic + '/input', 'output/pointcloud:=' + self.topic + '/output']):
            self.ready(pubs, cameras)
            self.wait(lambda: bool(outputs), lambda: publish(source, pair))
            self.assertEqual(outputs[-1].header, source.header)
            self.check_cloud(outputs[-1])
            self.assertEqual(outputs[-1].width, 0)
            outputs.clear()
            source.header.stamp.sec = 43
            pair = [mask(7, 43), mask(7, 43)]
            self.wait(lambda: bool(outputs), lambda: publish(source, pair))
            self.assertEqual(outputs[-1].header, source.header)
            self.check_cloud(outputs[-1])
            self.assertEqual(bytes(outputs[-1].data), bytes(source.data))

    def test_pointpainting_cold_warm(self):
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        inputs = list(models.glob('*.onnx'))
        self.assertEqual(len(inputs), 2)
        before = {p.name: identity(p) for p in inputs}
        cache = self.folder / 'model'
        shutil.copytree(models, cache)
        config = yaml.safe_load((self.share / 'config/pointpainting.param.yaml').read_text())
        config['/**']['ros__parameters'].update(cloud_capacity=2048, trt_precision='fp16',
            densification_params={'world_frame_id': 'base_link', 'num_past_frames': 0})
        path = self.folder / 'pointpainting.yaml'
        path.write_text(yaml.safe_dump(config))
        for run in range(2):
            outputs, painted = [], []
            output_sub = self.peer.create_subscription(DetectedObjects, self.topic + '/output', outputs.append, 10)
            painted_sub = self.peer.create_subscription(PointCloud2, '/pointpainting/debug/painted_pointcloud', painted.append, 10)
            publish, cameras, pubs = self.publishers(PointCloud2, focal=1.)
            source = cloud([(i * .4 - 12.8, j * .4 - 6.4, 1.) for i in range(64) for j in range(32)])
            pair = [rois(rectangle=(0, 0, 64, 48)), rois(rectangle=(0, 0, 64, 48))]
            with self.running('pointpainting_fusion', ['model_path:=' + str(cache),
                    'model_param_path:=' + str(path), 'use_pointcloud_container:=false',
                    'input/pointcloud:=' + self.topic + '/input', 'output/objects:=' + self.topic + '/output']):
                self.ready(pubs, cameras, seconds=240)
                self.wait(lambda: bool(outputs and painted), lambda: publish(source, pair), seconds=240)
                self.assertEqual(outputs[-1].header, source.header)
                self.assertEqual(painted[-1].header, source.header)
                self.check_cloud(painted[-1])
                self.assertEqual(painted[-1].width, source.width)
                fields = {f.name: f for f in painted[-1].fields}
                self.assertIn('CLASS', fields)
                flags = [struct.unpack_from('<f', bytes(painted[-1].data), offset + fields['CLASS'].offset)[0]
                         for offset in range(0, len(painted[-1].data), painted[-1].point_step)]
                self.assertEqual(set(flags), {1.}, {value: flags.count(value) for value in set(flags)})
                for obj in outputs[-1].objects:
                    self.assertTrue(obj.classification)
                    self.assertTrue(math.isfinite(obj.existence_probability))
            self.peer.destroy_subscription(output_sub)
            self.peer.destroy_subscription(painted_sub)
            for publisher in pubs:
                self.peer.destroy_publisher(publisher)
            engines = list(cache.glob('*.engine'))
            self.assertEqual(len(engines), 2)
            self.assertTrue(all(p.stat().st_size > 0 for p in engines))
            current = {p.name: identity(p) for p in engines}
            if run == 0:
                prepared = current
            else:
                self.assertEqual(current, prepared)
        self.assertEqual({p.name: identity(p) for p in inputs}, before)


if __name__ == '__main__':
    unittest.main(verbosity=2)
