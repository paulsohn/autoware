# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check installed viewer-frame publishers against clouds and the indexed map."""
import math
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import xml.etree.ElementTree as ET

from ament_index_python.packages import get_package_prefix, get_package_share_directory
from autoware_map_msgs.msg import MapProjectorInfo
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile
from sensor_msgs.msg import PointCloud2, PointField
from tf2_msgs.msg import TFMessage

MODE = sys.argv.pop(1)
RETAINED = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)


def executable(package, name):
    return str(Path(get_package_prefix(package)) / 'lib' / package / name)


class ViewerTransform(unittest.TestCase):
    def test_installed_transform(self):
        rclpy.init()
        peer = rclpy.create_node('viewer_tf_peer')
        transforms = []
        peer.create_subscription(TFMessage, '/tf_static', lambda msg: transforms.extend(msg.transforms), RETAINED)
        package = 'autoware_map_tf_generator'
        config = Path(get_package_share_directory(package)) / 'config/map_tf_generator.param.yaml'
        commands = [[executable(package, 'autoware_' + MODE + '_map_tf_generator'),
                     '--ros-args', '--params-file', str(config)]]
        if MODE == 'pcd':
            publisher = peer.create_publisher(PointCloud2, '/pointcloud_map', RETAINED)
            message = PointCloud2(height=1, width=3, point_step=12, row_step=36, is_dense=True,
                                  fields=[PointField(name=name, offset=i * 4, datatype=PointField.FLOAT32, count=1)
                                          for i, name in enumerate(['x', 'y', 'z'])],
                                  data=struct.pack('<fff', 2.0, 4.0, 6.0) * 3)
            message.header.frame_id = 'map'
        else:
            commands[0] += ['-r', 'vector_map:=/map/vector_map']
            publisher = peer.create_publisher(MapProjectorInfo, '/map/map_projector_info', RETAINED)
            message = MapProjectorInfo(projector_type=MapProjectorInfo.MGRS, mgrs_grid='54SVE',
                                       vertical_datum=MapProjectorInfo.WGS84)
            lanelet = Path(get_package_share_directory('autoware_map_asset')) / 'lanelet2_map.osm'
            commands.append([executable('autoware_map_loader', 'autoware_lanelet2_map_loader'), '--ros-args',
                             '-p', 'lanelet2_map_path:=' + str(lanelet), '-p', 'lanelet2_map_metadata_path:=""',
                             '-p', 'center_line_resolution:=5.0', '-p', 'use_waypoints:=true',
                             '-p', 'allow_unsupported_version:=true', '-p', 'enable_selected_map_loading:=false'])
        try:
            with tempfile.TemporaryFile() as log:
                processes = []

                def output():
                    return os.pread(log.fileno(), os.fstat(log.fileno()).st_size, 0).decode(errors='replace')

                try:
                    for command in commands:
                        processes.append(subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT))
                    publisher.publish(message)
                    deadline = time.monotonic() + 30
                    found = []
                    while time.monotonic() < deadline:
                        for process in processes:
                            self.assertIsNone(process.poll(), output())
                        found = [t for t in transforms if t.header.frame_id == 'map' and t.child_frame_id == 'viewer']
                        if found:
                            break
                        rclpy.spin_once(peer, timeout_sec=0.05)
                    self.assertTrue(found, output())
                    transform = found[-1].transform
                    xyz = [transform.translation.x, transform.translation.y, transform.translation.z]
                    self.assertTrue(all(math.isfinite(value) for value in xyz))
                    self.assertAlmostEqual(transform.rotation.w, 1.0)
                    self.assertEqual([transform.rotation.x, transform.rotation.y, transform.rotation.z], [0.0, 0.0, 0.0])
                    if MODE == 'pcd':
                        self.assertEqual(xyz, [2.0, 4.0, 6.0])
                    else:
                        # Original MGRS tags give conservative 100 m projected bounds.
                        tags = [{t.attrib['k']: t.attrib['v'] for t in node.findall('tag')}
                                for node in ET.parse(lanelet).getroot().findall('node')]
                        codes = [tag['mgrs_code'] for tag in tags]
                        self.assertTrue(all(code.startswith('54SVE') and len(code) == 11 for code in codes))
                        east = [int(code[5:8]) * 100 for code in codes]
                        north = [int(code[8:11]) * 100 for code in codes]
                        heights = [float(tag['ele']) for tag in tags]
                        for value, lower, upper in zip(xyz, [min(east), min(north), min(heights)],
                                                       [max(east) + 100, max(north) + 100, max(heights)]):
                            self.assertGreaterEqual(value, lower)
                            self.assertLessEqual(value, upper)
                finally:
                    for process in reversed(processes):
                        if process.poll() is None:
                            process.send_signal(signal.SIGINT)
                    for process in reversed(processes):
                        try:
                            process.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                            self.fail('Viewer TF shutdown timed out: ' + output())
                        self.assertEqual(process.returncode, 0, output())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
