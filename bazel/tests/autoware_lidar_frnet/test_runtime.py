# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run both real FRNet sensor models through indexed XML with writable caches."""
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
from launch import LaunchContext
from launch.frontend.parse_substitution import parse_substitution
from launch.utilities import perform_substitutions
import launch_ros.substitutions
import rclpy
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import PointCloud2, PointField
import yaml

LAUNCH = str(Path(sys.argv.pop(1)).resolve())
PACKAGE = 'autoware_lidar_frnet'


def identity(path):
    return path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest()


def input_cloud():
    points = []
    for ring in range(16):
        pitch = math.radians(-12 + ring * 17 / 15)
        for column in range(512):
            yaw = 2 * math.pi * (column + .25) / 512
            points.append((20 * math.cos(pitch) * math.cos(yaw),
                20 * math.cos(pitch) * math.sin(yaw), 20 * math.sin(pitch),
                100 + column % 50, 1, ring))
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


class FRNet(unittest.TestCase):
    def test_indexed_model_defaults(self):
        share = Path(get_package_share_directory(PACKAGE))
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        defaults = {e.get('name'): e.get('default') for e in
            E.parse(share / 'launch/lidar_frnet.launch.xml').findall('arg') if e.get('default') is not None}
        context = LaunchContext()
        self.assertEqual(Path(perform_substitutions(context, parse_substitution(defaults['model_path']))), models)
        parameters = yaml.safe_load((share / 'config/frnet.param.yaml').read_text())['/**']['ros__parameters']
        for sensor in ['ot128', 'qt128']:
            context.launch_configurations.update(model_name='frnet', sensor_model=sensor, model_path=str(models))
            for expression in [parameters['onnx_path'], defaults['ml_package_param_path']]:
                resolved = Path(perform_substitutions(context, parse_substitution(expression)))
                self.assertTrue(resolved.is_relative_to(models) and resolved.is_file())
        self.assertIn(PACKAGE + '_models', [e.text for e in E.parse(share / 'package.xml').findall('exec_depend')])

    def run_sensor(self, sensor):
        share = Path(get_package_share_directory(PACKAGE))
        models = Path(get_package_share_directory(PACKAGE + '_models'))
        source = models / ('frnet_' + sensor + '.onnx')
        before = identity(source)
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            model = cache / source.name
            shutil.copyfile(source, model)
            params = yaml.safe_load((share / 'config/frnet.param.yaml').read_text())
            params['/**']['ros__parameters'].update(onnx_path=str(model), trt_precision='fp16')
            params['/**']['ros__parameters']['filter']['output_format'] = 'xyzirc'
            config = cache / 'profile.yaml'
            config.write_text(yaml.safe_dump(params))
            metadata = models / ('ml_package_frnet_' + sensor + '.param.yaml')
            self.run_profile(sensor, config, metadata)
            engine = model.with_suffix('.engine')
            self.assertGreater(engine.stat().st_size, 0)
            prepared = identity(engine)
            self.run_profile(sensor, config, metadata)
            self.assertEqual(identity(engine), prepared)
        self.assertEqual(identity(source), before)

    def run_profile(self, sensor, config, metadata):
        share = Path(get_package_share_directory(PACKAGE))
        model_params = yaml.safe_load(metadata.read_text())['/**']['ros__parameters']
        outputs = {kind: [] for kind in ['segmentation', 'visualization', 'filtered']}
        timing = []
        rclpy.init()
        peer = rclpy.create_node('frnet_fixture_peer')
        for kind in outputs:
            peer.create_subscription(PointCloud2, '/frnet_gate/' + kind, outputs[kind].append, qos_profile_sensor_data)
        peer.create_subscription(Float64Stamped, '/lidar_frnet/debug/processing_time/total_ms', timing.append, 10)
        publisher = peer.create_publisher(PointCloud2, '/frnet_gate/input', qos_profile_sensor_data)
        sample = input_cloud()
        args = [LAUNCH, str((share / 'launch/lidar_frnet.launch.xml').resolve()),
            'sensor_model:=' + sensor, 'model_param_path:=' + str(config),
            'ml_package_param_path:=' + str(metadata), 'use_pointcloud_container:=false',
            'input/pointcloud:=/frnet_gate/input']
        args += ['output/pointcloud/' + kind + ':=/frnet_gate/' + kind for kind in outputs]
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def console():
                    log.seek(0)
                    return log.read()

                try:
                    deadline = time.monotonic() + 300
                    while time.monotonic() < deadline and not (all(outputs.values()) and timing):
                        self.assertIsNone(process.poll(), console())
                        publisher.publish(sample)
                        rclpy.spin_once(peer, timeout_sec=.1)
                    self.assertTrue(all(outputs.values()) and timing, console())
                    self.assertGreater(timing[-1].data, 0.)
                    self.assertTrue(math.isfinite(timing[-1].data))
                    for messages in outputs.values():
                        self.assertEqual(messages[-1].header, sample.header)
                        self.assertEqual(len(messages[-1].data), messages[-1].row_step * messages[-1].height)
                    segmentation = outputs['segmentation'][-1]
                    visualization = outputs['visualization'][-1]
                    self.assertGreaterEqual(segmentation.width * segmentation.height, sample.width)
                    self.assertEqual(visualization.width * visualization.height, segmentation.width * segmentation.height)
                    fields = {f.name: f for f in segmentation.fields}
                    self.assertEqual(fields['class_id'].datatype, PointField.UINT8)
                    self.assertEqual(fields['probability'].datatype, PointField.FLOAT32)
                    data = bytes(segmentation.data)
                    for index in range(segmentation.width * segmentation.height):
                        offset = index * segmentation.point_step
                        label = data[offset + fields['class_id'].offset]
                        probability = struct.unpack_from('<f', data, offset + fields['probability'].offset)[0]
                        self.assertLess(label, len(model_params['class_names']))
                        self.assertTrue(math.isfinite(probability))
                        self.assertGreaterEqual(probability, 0.)
                        self.assertLessEqual(probability, 1.001)
                    filtered = outputs['filtered'][-1]
                    self.assertEqual(filtered.fields, sample.fields)
                    self.assertEqual(filtered.point_step, sample.point_step)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('FRNet shutdown timed out: ' + console())
                    self.assertEqual(process.returncode, 0, console())
                    self.assertNotIn('process has died', console())
        finally:
            peer.destroy_node()
            rclpy.shutdown()

    def test_ot128(self): self.run_sensor('ot128')
    def test_qt128(self): self.run_sensor('qt128')


if __name__ == '__main__':
    unittest.main()
