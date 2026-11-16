# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check the projection loader's latched map projector output in either backend."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from autoware_map_msgs.msg import MapProjectorInfo
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile

EXECUTABLE, YAML = [str(Path(p).resolve()) for p in sys.argv[1:3]]
del sys.argv[1:3]


class ProjectionRuntime(unittest.TestCase):
    def test_projection(self):
        environment = dict(os.environ, ROS_DOMAIN_ID="92")
        enabled = environment["AUTOWARE_TEST_AGNOCAST"] == "1"
        environment["ENABLE_AGNOCAST"] = "1" if enabled else "0"
        if enabled:
            environment["LD_PRELOAD"] = environment["AGNOCAST_TEST_HEAPHOOK"]
        else:
            environment.pop("LD_PRELOAD", None)
        os.environ["ROS_DOMAIN_ID"] = environment["ROS_DOMAIN_ID"]
        rclpy.init()
        node = rclpy.create_node("projection_runtime_peer")
        messages = []
        subscription = node.create_subscription(
            MapProjectorInfo, "/map/map_projector_info", messages.append,
            QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
        try:
            with tempfile.TemporaryFile(mode="w+") as log:
                process = subprocess.Popen([EXECUTABLE, "--ros-args", "-p", "map_projector_info_path:=" + YAML,
                                            "-p", 'lanelet2_map_path:=""'],
                                           env=environment, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and not messages and process.poll() is None:
                        rclpy.spin_once(node, timeout_sec=0.1)
                    if not messages:
                        log.seek(0)
                        self.fail("Missing map projector info; node log:\n" + log.read())
                    info = messages[-1]
                    self.assertEqual(info.projector_type, MapProjectorInfo.LOCAL_CARTESIAN_UTM)
                    self.assertEqual(info.vertical_datum, MapProjectorInfo.WGS84)
                    self.assertAlmostEqual(info.map_origin.latitude, 35.6762)
                    self.assertAlmostEqual(info.map_origin.longitude, 139.6503)
                    self.assertEqual(info.map_origin.altitude, 0.0)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail("Projection loader failed to stop on SIGINT")
                log.seek(0)
                self.assertEqual(process.returncode, 0, log.read())
        finally:
            node.destroy_subscription(subscription)
            node.destroy_node()
            rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
