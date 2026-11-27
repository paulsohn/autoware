# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise every original CUDA preprocessing command through its XML launcher."""
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_internal_debug_msgs.msg import Float32Stamped
from geometry_msgs.msg import TwistWithCovarianceStamped
from rcl_interfaces.msg import Parameter, ParameterType, ParameterValue
from rcl_interfaces.srv import SetParameters
import rclpy
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import PointCloud2, PointField
import yaml

LAUNCH = str(Path(sys.argv.pop(1)).resolve())
PACKAGE = 'autoware_cuda_pointcloud_preprocessor'
INPUT = [(5.01, 4), (5.04, 8), (8.1, 12)]


def cloud(points, extended=False):
    message = PointCloud2(height=1, width=len(points), is_dense=True)
    message.header.frame_id = 'base_link'
    message.header.stamp.sec = 42
    fields = [('x', 0, PointField.FLOAT32), ('y', 4, PointField.FLOAT32),
              ('z', 8, PointField.FLOAT32), ('intensity', 12, PointField.UINT8),
              ('return_type', 13, PointField.UINT8), ('channel', 14, PointField.UINT16)]
    if extended:
        fields += [('azimuth', 16, PointField.FLOAT32), ('elevation', 20, PointField.FLOAT32),
                   ('distance', 24, PointField.FLOAT32), ('time_stamp', 28, PointField.UINT32)]
    message.fields = [PointField(name=n, offset=o, datatype=t, count=1) for n, o, t in fields]
    message.point_step = 32 if extended else 16
    message.row_step = message.point_step * message.width
    message.data = b''.join(struct.pack('<fffBBH', x, 0., 0., intensity, 1, i % 2) +
                            (struct.pack('<fffI', 0., 0., x, i * 1000) if extended else b'')
                            for i, (x, intensity) in enumerate(points))
    return message


def coordinates(message):
    offsets = {f.name: f.offset for f in message.fields}
    return sorted(struct.unpack_from('<f', bytes(message.data), i * message.point_step + offsets['x'])[0]
                  for i in range(message.width * message.height))


class Commands(unittest.TestCase):
    def run_profile(self, family):
        share = Path(get_package_share_directory(PACKAGE))
        cpu = Path(get_package_share_directory('autoware_pointcloud_preprocessor'))
        configs = {
            'voxel': (share / 'config/cuda_voxel_grid_downsample_filter.param.yaml',
                      'cuda_voxel_grid_downsample_filter.launch.xml', 'cuda_voxel_gird_downsample_filter_param_file'),
            'noise': (cpu / 'config/polar_voxel_noise_filter_node.param.yaml',
                      'cuda_polar_voxel_noise_filter.launch.xml', 'polar_voxel_noise_filter_param_file'),
            'outlier': (cpu / 'config/polar_voxel_outlier_filter_node.param.yaml',
                        'cuda_polar_voxel_outlier_filter.launch.xml', 'polar_voxel_outlier_filter_param_file'),
            'preprocessor': (share / 'config/cuda_pointcloud_preprocessor.param.yaml',
                             'cuda_pointcloud_preprocessor.launch.xml', 'cuda_pointcloud_preprocessor_param_file'),
            'concatenate': (cpu / 'config/concatenate_and_time_sync_node.param.yaml',
                            'cuda_concatenate_and_time_sync_node.launch.xml', 'param_file'),
        }
        source, xml, parameter_arg = configs[family]
        parameters = yaml.safe_load(source.read_text())['/**']['ros__parameters']
        expected = [p[0] for p in INPUT]
        if family == 'voxel':
            parameters['max_mem_pool_size_in_byte'] = 16 * 1024 * 1024
            expected = [5.025, 8.1]
        if family == 'noise':
            parameters.update(voxel_points_threshold=1, avg_intensity_threshold=255.,
                              use_return_type_classification=False, filter_secondary_returns=False,
                              publish_noise_cloud=True)
            expected = [5.01, 5.04]
        if family == 'outlier':
            parameters.update(use_return_type_classification=False, filter_secondary_returns=False,
                              voxel_points_threshold=2, publish_noise_cloud=True)
            expected = [5.01, 5.04]
        if family == 'preprocessor':
            parameters.update(use_imu=False, enable_ring_outlier_filter=False,
                              max_input_point_count=128, max_twist_frequency_hz=20., max_imu_frequency_hz=20.)
            parameters['crop_box.negative'] = [True]
        if family == 'concatenate':
            parameters.update(input_topics=['/cuda_gate/left', '/cuda_gate/right'], output_frame='base_link',
                              is_motion_compensated=False, publish_synchronized_pointcloud=False)
        rclpy.init()
        peer = rclpy.create_node('cuda_filter_peer')
        observed, noises, ratios = [], [], []
        try:
            peer.create_subscription(PointCloud2, '/cuda_gate/output', observed.append, qos_profile_sensor_data)
            if family in ('noise', 'outlier'):
                peer.create_subscription(PointCloud2, '/cuda_polar_voxel_' + family + '_filter/debug/pointcloud_noise',
                                         noises.append, qos_profile_sensor_data)
            if family == 'outlier':
                peer.create_subscription(Float32Stamped, '/cuda_polar_voxel_outlier_filter/debug/filter_ratio',
                                         ratios.append, qos_profile_sensor_data)
            topics = parameters['input_topics'] if family == 'concatenate' else ['/cuda_gate/input']
            publishers = [peer.create_publisher(PointCloud2, topic, qos_profile_sensor_data) for topic in topics]
            twist = peer.create_publisher(TwistWithCovarianceStamped, '/cuda_gate/twist', 10)
            samples = [cloud(INPUT[:2]), cloud(INPUT[2:])] if family == 'concatenate' else [cloud(INPUT, family == 'preprocessor')]
            with tempfile.TemporaryDirectory() as temporary, tempfile.TemporaryFile(mode='w+') as log:
                config = Path(temporary) / 'parameters.yaml'
                config.write_text(yaml.safe_dump({'/**': {'ros__parameters': parameters}}))
                args = [LAUNCH, str((share / 'launch' / xml).resolve()), parameter_arg + ':=' + str(config)]
                args += ['output:=/cuda_gate/output'] if family == 'concatenate' else ['input/pointcloud:=/cuda_gate/input', 'output/pointcloud:=/cuda_gate/output']
                if family in ('preprocessor', 'concatenate'):
                    args += ['input/twist:=/cuda_gate/twist']
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
                try:
                    def wait_for(predicate):
                        deadline = time.monotonic() + 25
                        while time.monotonic() < deadline and not predicate():
                            if process.poll() is not None:
                                log.seek(0)
                                self.fail('CUDA command exited before output:\n' + log.read())
                            velocity = TwistWithCovarianceStamped()
                            velocity.header = samples[0].header
                            twist.publish(velocity)
                            for publisher, sample in zip(publishers, samples):
                                publisher.publish(sample)
                            rclpy.spin_once(peer, timeout_sec=.05)
                        if not predicate():
                            log.seek(0)
                            self.fail('Missing ' + family + ' output:\n' + log.read())
                    wait_for(lambda: observed and (family not in ('noise', 'outlier') or noises) and
                             (family != 'outlier' or ratios))
                    result = observed[-1]
                    self.assertEqual(result.header, samples[0].header)
                    actual = coordinates(result)
                    self.assertEqual(len(actual), len(expected))
                    for a, e in zip(actual, expected):
                        self.assertAlmostEqual(a, e, places=4)
                    intensity = next(f for f in result.fields if f.name == 'intensity')
                    self.assertEqual(intensity.datatype, PointField.UINT8)
                    values = sorted(bytes(result.data)[i * result.point_step + intensity.offset]
                                    for i in range(result.width * result.height))
                    expected_intensities = [6, 12] if family == 'voxel' else ([4, 8] if family in ('noise', 'outlier') else [4, 8, 12])
                    self.assertEqual(values, expected_intensities)
                    if noises:
                        self.assertEqual(len(coordinates(noises[-1])), 1)
                        self.assertAlmostEqual(coordinates(noises[-1])[0], 8.1, places=4)
                    if ratios:
                        self.assertGreaterEqual(ratios[-1].data, 0.)
                        self.assertLessEqual(ratios[-1].data, 1.)
                    if family == 'noise':
                        client = peer.create_client(SetParameters, '/cuda_polar_voxel_noise_filter/set_parameters')
                        self.assertTrue(client.wait_for_service(timeout_sec=5))
                        request = SetParameters.Request(parameters=[Parameter(name='voxel_points_threshold',
                            value=ParameterValue(type=ParameterType.PARAMETER_INTEGER, integer_value=3))])
                        future = client.call_async(request)
                        rclpy.spin_until_future_complete(peer, future, timeout_sec=5)
                        self.assertTrue(future.done())
                        self.assertTrue(all(r.successful for r in future.result().results))
                        observed.clear()
                        wait_for(lambda: any(m.width == 0 for m in observed))
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('CUDA command shutdown timed out')
                    log.seek(0)
                    output_log = log.read()
                    self.assertEqual(process.returncode, 0, output_log)
                    self.assertNotIn('process has died', output_log)
        finally:
            peer.destroy_node()
            rclpy.shutdown()

    def test_voxel_centroids(self): self.run_profile('voxel')
    def test_noise_split_and_parameter_update(self): self.run_profile('noise')
    def test_outlier_split_and_ratio(self): self.run_profile('outlier')
    def test_preprocessor_extended_layout(self): self.run_profile('preprocessor')
    def test_concatenation(self): self.run_profile('concatenate')


if __name__ == '__main__':
    unittest.main()
