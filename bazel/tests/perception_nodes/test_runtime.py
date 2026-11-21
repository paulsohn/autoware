# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Start installed perception components with their indexed configuration closure."""
import math
import os
import struct
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_prefix, get_package_share_directory
from rcl_interfaces.srv import GetParameters
import rclpy

PACKAGE, EXECUTABLE, CONFIG, LIBRARY = [sys.argv.pop(1) for _ in range(4)]


class PerceptionRuntime(unittest.TestCase):
    def test_indexed_node(self):
        rclpy.init()
        peer = rclpy.create_node('perception_node_peer')
        namespace = '/bazel_perception_' + str(os.getpid())
        binary = Path(get_package_prefix(PACKAGE)) / 'lib' / PACKAGE / EXECUTABLE
        self.assertTrue(binary.is_file(), binary)
        args = [str(binary), '--ros-args', '-r', '__ns:=' + namespace, '-r', '__node:=under_test']
        common = Path(get_package_share_directory('autoware_test_utils')) / 'config'
        for config in ['test_common.param.yaml', 'test_nearest_search.param.yaml', 'test_vehicle_info.param.yaml']:
            args += ['--params-file', str(common / config)]
        if CONFIG != '-':
            args += ['--params-file', str(Path(get_package_share_directory(PACKAGE)) / CONFIG)]
        if PACKAGE == 'autoware_multi_object_tracker':
            share = Path(get_package_share_directory(PACKAGE))
            for config in ['data_association_matrix.param.yaml', 'input_channels.param.yaml']:
                args += ['--params-file', str(share / 'config' / config)]
            for index in range(2, 13):
                args += ['-p', 'input/detection' + str(index).zfill(2) + '/channel:=none']
            args += ['-p', 'input/detection01/channel:=detected_objects', '-p', 'ego_source:=tf',
                     '-p', 'publish_merged_objects:=false']
        if PACKAGE == 'autoware_dummy_traffic_light_publisher':
            args += ['-p', 'mode:=empty', '-p', 'fixed_color:=red']
        received = []
        if PACKAGE == 'pointcloud_to_laserscan' and EXECUTABLE != 'dummy_pointcloud_publisher':
            from sensor_msgs.msg import LaserScan, PointCloud2, PointField
            from rclpy.qos import qos_profile_sensor_data
            forward = EXECUTABLE == 'pointcloud_to_laserscan_node'
            if forward:
                args += ['-p', 'min_height:=-1.0', '-p', 'max_height:=1.0']
                input_type, input_topic = PointCloud2, namespace + '/under_test/input/pointcloud'
                output_type, output_topic = LaserScan, namespace + '/under_test/output/laserscan'
            else:
                input_type, input_topic = LaserScan, namespace + '/scan_in'
                output_type, output_topic = PointCloud2, namespace + '/cloud'
            publisher = peer.create_publisher(input_type, input_topic, qos_profile_sensor_data)
            subscription = peer.create_subscription(output_type, output_topic, received.append, qos_profile_sensor_data)
        if PACKAGE == 'autoware_dummy_traffic_light_publisher':
            from autoware_perception_msgs.msg import TrafficLightGroup, TrafficLightGroupArray
            publisher = peer.create_publisher(TrafficLightGroupArray, namespace + '/under_test/input/traffic_signals', 1)
            subscription = peer.create_subscription(
                TrafficLightGroupArray, namespace + '/under_test/output/traffic_signals', received.append, 1)
        client = peer.create_client(GetParameters, namespace + '/under_test/get_parameters')
        try:
            with tempfile.TemporaryFile() as log:
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    return os.pread(log.fileno(), os.fstat(log.fileno()).st_size, 0).decode(errors='replace')

                def wait(condition):
                    deadline = time.monotonic() + 25
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if condition():
                            return
                        rclpy.spin_once(peer, timeout_sec=0.05)
                    self.fail('Perception node timed out: ' + output())

                try:
                    wait(client.service_is_ready)
                    future = client.call_async(GetParameters.Request(names=['use_sim_time']))
                    wait(future.done)
                    self.assertIsNone(future.exception())
                    self.assertEqual(len(future.result().values), 1, output())
                    self.assertFalse(future.result().values[0].bool_value)
                    loaded = Path('/proc', str(process.pid), 'maps').read_text()
                    self.assertIn(LIBRARY, loaded)
                    self.assertNotIn('libagnocast_heaphook.so', loaded)
                    if PACKAGE == 'pointcloud_to_laserscan' and EXECUTABLE != 'dummy_pointcloud_publisher':
                        wait(lambda: publisher.get_subscription_count() > 0 and subscription.get_publisher_count() > 0)

                        def publish_and_observe():
                            if forward:
                                message = PointCloud2(height=1, width=1, point_step=12, row_step=12, is_dense=True)
                                message.fields = [PointField(name=name, offset=offset, datatype=PointField.FLOAT32, count=1)
                                                  for name, offset in [('x', 0), ('y', 4), ('z', 8)]]
                                message.data = list(struct.pack('<fff', 2.0, 0.0, 0.0))
                            else:
                                message = LaserScan(angle_min=0.0, angle_max=0.0, angle_increment=0.1,
                                                    range_min=0.1, range_max=20.0, ranges=[2.0])
                            message.header.frame_id = 'base_link'
                            message.header.stamp = peer.get_clock().now().to_msg()
                            publisher.publish(message)
                            return bool(received)

                        wait(publish_and_observe)
                        result = received[-1]
                        self.assertEqual(result.header.frame_id, 'base_link')
                        if forward:
                            self.assertTrue(any(math.isclose(value, 2.0, abs_tol=1e-4) for value in result.ranges))
                        else:
                            self.assertEqual(result.width * result.height, 1)
                            fields = {field.name: field.offset for field in result.fields}
                            self.assertAlmostEqual(struct.unpack_from('<f', result.data, fields['x'])[0], 2.0, places=4)
                            self.assertAlmostEqual(struct.unpack_from('<f', result.data, fields['y'])[0], 0.0, places=4)
                    if PACKAGE == 'autoware_dummy_traffic_light_publisher':
                        wait(lambda: bool(received))
                        self.assertEqual(list(received[-1].traffic_light_groups), [])
                        wait(lambda: publisher.get_subscription_count() > 0)
                        received.clear()

                        def publish_signal():
                            message = TrafficLightGroupArray(traffic_light_groups=[TrafficLightGroup(traffic_light_group_id=42)])
                            publisher.publish(message)
                            return any([group.traffic_light_group_id for group in item.traffic_light_groups] == [42]
                                       for item in received)

                        wait(publish_signal)
                        received.clear()
                        wait(lambda: any(not message.traffic_light_groups for message in received))
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Shutdown timed out: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
