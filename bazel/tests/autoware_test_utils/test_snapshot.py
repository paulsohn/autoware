# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the snapshot executable's subscriptions, service and YAML output."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from nav_msgs.msg import Odometry
import rclpy
from std_srvs.srv import Empty
import yaml

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())


class SnapshotRuntime(unittest.TestCase):
    def test_snapshot(self):
        os.environ["ROS_DOMAIN_ID"] = "87"
        environment = dict(os.environ, LD_LIBRARY_PATH=str(Path("lib").absolute()))
        environment.pop("LD_PRELOAD", None)
        rclpy.init()
        node = rclpy.create_node("snapshot_runtime_peer")
        publisher = node.create_publisher(Odometry, "/localization/kinematic_state", 10)
        client = node.create_client(Empty, "/autoware_test_utils/topic_snapshot_saver")
        map_uri = "package://autoware_test_utils/test_map/lanelet2_map.osm"
        try:
            with tempfile.TemporaryDirectory() as work, tempfile.TemporaryFile(mode="w+") as log:
                process = subprocess.Popen(
                    [EXECUTABLE, "--ros-args", "-p", "map_path:=" + map_uri, "-p",
                     "config_path:=package://autoware_test_utils/config/sample_topic_snapshot.yaml"],
                    cwd=work, env=environment, stdout=log, stderr=subprocess.STDOUT,
                )
                try:
                    message = Odometry()
                    message.header.frame_id = "map"
                    message.header.stamp.sec = 42
                    message.pose.pose.position.x = 12.5
                    snapshot = Path(work) / "topic_snapshot.yaml"
                    deadline = time.monotonic() + 30
                    data = None
                    while time.monotonic() < deadline and process.poll() is None:
                        publisher.publish(message)
                        rclpy.spin_once(node, timeout_sec=0.1)
                        if not client.service_is_ready():
                            continue
                        future = client.call_async(Empty.Request())
                        rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                        if future.done():
                            self.assertIsNotNone(future.result())
                            if snapshot.exists():
                                data = yaml.safe_load(snapshot.read_text())
                                if data["self_odometry"]["header"]["stamp"]["sec"] == 42:
                                    break
                    log.seek(0)
                    self.assertIsNotNone(data, log.read())
                    self.assertEqual(data["format_version"], 1)
                    self.assertEqual(data["map_path_uri"], map_uri)
                    self.assertEqual(data["self_odometry"]["header"]["stamp"]["sec"], 42)
                    self.assertEqual(data["self_odometry"]["header"]["frame_id"], "map")
                    self.assertEqual(data["self_odometry"]["pose"]["pose"]["position"]["x"], 12.5)
                    self.assertTrue({"self_acceleration", "operation_mode", "route", "traffic_signal",
                                     "dynamic_object"}.issubset(data))
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail("Snapshot node failed to stop on SIGINT")
                log.seek(0)
                self.assertEqual(process.returncode, 0, log.read())
        finally:
            node.destroy_node()
            rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
