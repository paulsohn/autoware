# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Qualify the indexed PTV3 FP16 profile through its real XML launcher."""
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
PACKAGE = 'autoware_ptv3'


def identity(path):
    with path.open('rb') as stream:
        return path.stat().st_mtime_ns, hashlib.file_digest(stream, 'sha256').hexdigest()


def pointcloud():
    points = [(i * .24 - 17., j * .24 - 17., .2 + (i % 4) * .12, 100 + i % 50, 1, j)
              for i in range(144) for j in range(144)]
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


class PTV3(unittest.TestCase):
    def test_indexed_defaults_and_manifests(self):
        share = Path(get_package_share_directory(PACKAGE))
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        defaults = {e.get('name'): e.get('default') for e in E.parse(share / 'launch/ptv3.launch.xml').findall('arg') if e.get('default') is not None}
        self.assertNotIn('data_path', defaults)
        self.assertEqual(Path(perform_substitutions(LaunchContext(), parse_substitution(defaults['model_path']))), models)
        parent = next(e.get('default') for e in E.parse(PARENT_XML).findall('arg') if e.get('name') == 'model_path')
        self.assertEqual(Path(perform_substitutions(LaunchContext(), parse_substitution(parent))), models)
        for path in [share / 'package.xml', Path(PARENT_MANIFEST)]:
            self.assertIn(PACKAGE + '_models', [e.text for e in E.parse(path).findall('exec_depend')])
        context = LaunchContext()
        context.launch_configurations.update(model_path=str(models), model_variant='ptv3')
        config = yaml.safe_load((share / 'config/ptv3.param.yaml').read_text())['/**']['ros__parameters']
        for key in ['encoder', 'segmentation3d', 'detection3d']:
            self.assertTrue(Path(perform_substitutions(context, parse_substitution(config[key]['onnx_path']))).is_file())
        self.assertTrue(Path(perform_substitutions(context, parse_substitution(config['plugins_path']))).is_file())

    def test_cold_and_warm_fp16(self):
        share = Path(get_package_share_directory(PACKAGE))
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        sources = list(models.glob('*.onnx'))
        self.assertEqual(len(sources), 3)
        before = {p.name: identity(p) for p in sources}
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary) / 'model'
            shutil.copytree(models, cache)
            config = yaml.safe_load((share / 'config/ptv3.param.yaml').read_text())
            parameters = config['/**']['ros__parameters']
            parameters.update(trt_precision='fp16', cloud_capacity=24000,
                densification_num_past_frames=0, densification_world_frame_id='base_link')
            parameters['encoder'].update(voxels_num=[16000, 20000, 24000],
                pooled_voxels_num_max=[24000, 16000, 8000, 4000])
            for task in ['encoder', 'segmentation3d', 'detection3d']:
                parameters[task]['workspace_size'] = 1073741824
            profile = Path(temporary) / 'profile.yaml'
            profile.write_text(yaml.safe_dump(config))
            self.run_profile(cache, profile)
            engines = list(cache.glob('*.engine'))
            self.assertEqual(len(engines), 3)
            self.assertTrue(all(p.stat().st_size > 0 for p in engines))
            prepared = {p.name: identity(p) for p in engines}
            self.run_profile(cache, profile)
            self.assertEqual({p.name: identity(p) for p in engines}, prepared)
        self.assertEqual({p.name: identity(p) for p in sources}, before)

    def run_profile(self, cache, profile):
        share = Path(get_package_share_directory(PACKAGE))
        rclpy.init()
        peer = rclpy.create_node('ptv3_fixture_peer')
        output, timing = [], []
        clouds = {name: [] for name in ["segmentation", "visualization", "filtered"]}
        for name, messages in clouds.items():
            peer.create_subscription(PointCloud2, "/ptv3_gate/" + name, messages.append, qos_profile_sensor_data)
        peer.create_subscription(DetectedObjects, '/ptv3_gate/output', output.append, 10)
        peer.create_subscription(Float64Stamped, '/ptv3/debug/processing_time/total_ms', timing.append, 10)
        publisher = peer.create_publisher(PointCloud2, '/ptv3_gate/input', qos_profile_sensor_data)
        sample = pointcloud()
        command = [LAUNCH, str((share / 'launch/ptv3.launch.xml').resolve()),
            'model_path:=' + str(cache), 'model_param_path:=' + str(profile),
            'use_pointcloud_container:=false', 'input/pointcloud:=/ptv3_gate/input',
            'output/objects:=/ptv3_gate/output'] + [
                'output/pointcloud/' + name + ':=/ptv3_gate/' + name for name in clouds]
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)

                def console():
                    log.seek(0)
                    return log.read()

                try:
                    deadline = time.monotonic() + 600
                    while time.monotonic() < deadline and not (output and timing and all(clouds.values())):
                        self.assertIsNone(process.poll(), console())
                        publisher.publish(sample)
                        rclpy.spin_once(peer, timeout_sec=.1)
                    self.assertTrue(output and timing and all(clouds.values()), console())
                    for name, messages in clouds.items():
                        cloud = messages[-1]
                        self.assertEqual(cloud.header, sample.header)
                        self.assertEqual(cloud.row_step, cloud.width * cloud.point_step)
                        self.assertEqual(len(cloud.data), cloud.row_step * cloud.height)
                        self.assertLessEqual(cloud.width, sample.width)
                        self.assertTrue({"x", "y", "z"}.issubset({f.name for f in cloud.fields}))
                        if name != "filtered":
                            self.assertGreater(cloud.width, 0)
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
                        self.fail('PTV3 shutdown timed out: ' + console())
                    self.assertEqual(process.returncode, 0, console())
                    self.assertNotIn('process has died', console())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
