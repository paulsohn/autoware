# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the shipped Python component launcher with real GPU classification."""
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
import rclpy
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import PointCloud2, PointField

LAUNCH = str(Path(sys.argv.pop(1)).resolve())


class GroundCudaLaunch(unittest.TestCase):
    def test_shipped_component_launch(self):
        share = Path(get_package_share_directory('autoware_ground_segmentation_cuda'))
        rclpy.init()
        peer = rclpy.create_node('ground_cuda_launch_peer')
        ground, obstacles = [], []
        peer.create_subscription(PointCloud2,
            '/cuda_scan_ground_segmentation_filter/output/ground_pointcloud',
            ground.append, qos_profile_sensor_data)
        peer.create_subscription(PointCloud2, '/ground_cuda_launch/obstacles',
            obstacles.append, qos_profile_sensor_data)
        publisher = peer.create_publisher(PointCloud2, '/ground_cuda_launch/input',
            qos_profile_sensor_data)
        sample = PointCloud2(height=1, width=2, point_step=16, row_step=32, is_dense=True)
        sample.header.frame_id = 'base_link'
        sample.header.stamp.sec = 42
        sample.fields = [PointField(name=n, offset=o, datatype=t, count=1)
            for n, o, t in [('x', 0, PointField.FLOAT32), ('y', 4, PointField.FLOAT32),
                ('z', 8, PointField.FLOAT32), ('intensity', 12, PointField.UINT8),
                ('return_type', 13, PointField.UINT8), ('channel', 14, PointField.UINT16)]]
        sample.data = struct.pack('<fffBBHfffBBH',
            5., .01, 0., 42, 1, 7, 5., .01, 1.5, 99, 2, 8)
        command = [LAUNCH, str(share / 'launch/cuda_scan_ground_filter.launch.py'),
            'input/pointcloud:=/ground_cuda_launch/input',
            'output/pointcloud:=/ground_cuda_launch/obstacles']
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)

                def console():
                    log.seek(0)
                    return log.read()

                try:
                    deadline = time.monotonic() + 45
                    while time.monotonic() < deadline and not (ground and obstacles):
                        self.assertIsNone(process.poll(), console())
                        publisher.publish(sample)
                        rclpy.spin_once(peer, timeout_sec=.1)
                    self.assertTrue(ground and obstacles, console())
                    for cloud, intensity, channel, z in [
                            (ground[-1], 42, 7, 0.), (obstacles[-1], 99, 8, 1.5)]:
                        self.assertEqual(cloud.header, sample.header)
                        self.assertEqual(cloud.fields, sample.fields)
                        self.assertEqual((cloud.width, cloud.height, cloud.point_step), (1, 1, 16))
                        self.assertEqual(cloud.row_step, 16)
                        self.assertEqual(len(cloud.data), 16)
                        point = struct.unpack('<fffBBH', bytes(cloud.data))
                        self.assertAlmostEqual(point[2], z)
                        self.assertEqual((point[3], point[5]), (intensity, channel))
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Ground component shutdown timed out: ' + console())
                    self.assertEqual(process.returncode, 0, console())
                    self.assertNotIn('process has died', console())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
