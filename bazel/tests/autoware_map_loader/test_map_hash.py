# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the original installed map identity publisher and XML service."""
import hashlib
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_prefix, get_package_share_directory
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile
from tier4_external_api_msgs.msg import MapHash, ResponseStatus
from tier4_external_api_msgs.srv import GetTextFile


class MapIdentity(unittest.TestCase):
    def test_hash_topic_and_xml_service(self):
        share = Path(get_package_share_directory('autoware_map_asset'))
        lanelet = share / 'lanelet2_map.osm'
        cloud = share / 'pointcloud_map.pcd'
        binary = Path(get_package_prefix('autoware_map_loader')) / 'lib/autoware_map_loader/map_hash_generator'
        rclpy.init()
        peer = rclpy.create_node('map_hash_peer')
        messages = []
        peer.create_subscription(MapHash, '/api/autoware/get/map/info/hash', messages.append,
                                 QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
        client = peer.create_client(GetTextFile, '/api/autoware/get/map/lanelet/xml')
        try:
            with tempfile.TemporaryFile() as log:
                process = subprocess.Popen([str(binary), '--ros-args', '-p', 'lanelet2_map_path:=' + str(lanelet),
                                            '-p', 'pointcloud_map_path:=' + str(cloud)],
                                           stdout=log, stderr=subprocess.STDOUT)

                def output():
                    return os.pread(log.fileno(), os.fstat(log.fileno()).st_size, 0).decode(errors='replace')

                def wait(condition):
                    deadline = time.monotonic() + 25
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if condition():
                            return
                        rclpy.spin_once(peer, timeout_sec=0.05)
                    self.fail('Map identity timed out: ' + output())

                try:
                    wait(lambda: messages and client.service_is_ready())
                    self.assertEqual(messages[-1].lanelet, hashlib.sha256(lanelet.read_text().encode()).hexdigest())
                    self.assertEqual(messages[-1].pcd, hashlib.sha256(cloud.read_bytes()).hexdigest())
                    future = client.call_async(GetTextFile.Request())
                    wait(future.done)
                    self.assertIsNone(future.exception())
                    self.assertEqual(future.result().status.code, ResponseStatus.SUCCESS)
                    self.assertEqual(future.result().file.text, lanelet.read_text())
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Map identity shutdown timed out: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
