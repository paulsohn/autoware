# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise map publication, metadata and loading services in either backend."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from autoware_map_msgs.msg import LaneletMapBin, LaneletMapMetaData, MapProjectorInfo, PointCloudMapMetaData
from autoware_map_msgs.srv import GetDifferentialPointCloudMap, GetSelectedLanelet2Map
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile
from sensor_msgs.msg import PointCloud2
import yaml

EXECUTABLE, MAP = [str(Path(p).resolve()) for p in sys.argv[1:3]]
MODE = sys.argv[3]
del sys.argv[1:4]

PCD = """VERSION 0.7
FIELDS x y z
SIZE 4 4 4
TYPE F F F
COUNT 1 1 1
WIDTH 3
HEIGHT 1
VIEWPOINT 0 0 0 1 0 0 0
POINTS 3
DATA ascii
0 0 0
1 0 0
0 1 0
"""


class MapRuntime(unittest.TestCase):
    def test_map_and_service(self):
        environment = dict(os.environ, ROS_DOMAIN_ID="93")
        enabled = environment["AUTOWARE_TEST_AGNOCAST"] == "1"
        environment["ENABLE_AGNOCAST"] = "1" if enabled else "0"
        if enabled:
            environment["LD_PRELOAD"] = environment["AGNOCAST_TEST_HEAPHOOK"]
        else:
            environment.pop("LD_PRELOAD", None)
        os.environ["ROS_DOMAIN_ID"] = environment["ROS_DOMAIN_ID"]
        rclpy.init()
        node = rclpy.create_node("map_loader_runtime_peer")
        maps, metadata = [], []
        qos = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
        is_cloud = MODE == "pointcloud"
        subscriptions = [
            node.create_subscription(PointCloud2 if is_cloud else LaneletMapBin,
                                     "output/pointcloud_map" if is_cloud else "/map/vector_map", maps.append, qos),
            node.create_subscription(PointCloudMapMetaData if is_cloud else LaneletMapMetaData,
                                     "output/pointcloud_map_metadata" if is_cloud else "output/lanelet2_map_metadata",
                                     metadata.append, qos),
        ]
        service = GetDifferentialPointCloudMap if is_cloud else GetSelectedLanelet2Map
        client = node.create_client(service, "service/get_differential_pcd_map" if is_cloud else "service/get_selected_lanelet2_map")
        projector_pub = node.create_publisher(MapProjectorInfo, "/map/map_projector_info", qos)
        try:
            with tempfile.TemporaryDirectory() as work, tempfile.TemporaryFile(mode="w+") as log:
                if is_cloud:
                    pcd = Path(work) / "single.pcd"
                    pcd.write_text(PCD)
                    parameters = {"pcd_paths_or_directory": [str(pcd)], "pcd_metadata_path": str(Path(work) / "absent.yaml"),
                                  "enable_whole_load": True, "enable_downsampled_whole_load": False,
                                  "enable_partial_load": True, "enable_selected_load": False}
                else:
                    parameters = {"lanelet2_map_path": MAP, "lanelet2_map_metadata_path": "",
                                  "center_line_resolution": 5.0, "use_waypoints": True,
                                  "allow_unsupported_version": True, "enable_selected_map_loading": True}
                parameter_file = Path(work) / "parameters.yaml"
                parameter_file.write_text(yaml.safe_dump({"/**": {"ros__parameters": parameters}}))
                process = subprocess.Popen([EXECUTABLE, "--ros-args", "--params-file", str(parameter_file)],
                                           env=environment, stdout=log, stderr=subprocess.STDOUT)
                try:
                    projector = MapProjectorInfo()
                    projector.projector_type = MapProjectorInfo.MGRS
                    projector.mgrs_grid = "54SUE"
                    projector.vertical_datum = MapProjectorInfo.WGS84
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and process.poll() is None:
                        if maps and metadata and client.service_is_ready():
                            break
                        if not is_cloud:
                            projector_pub.publish(projector)
                        rclpy.spin_once(node, timeout_sec=0.1)
                    if not (maps and metadata and client.service_is_ready()):
                        log.seek(0)
                        self.fail("Missing map/metadata/service; node log:\n" + log.read())
                    self.assertEqual(maps[-1].header.frame_id, "map")
                    self.assertEqual(metadata[-1].header.frame_id, "map")
                    self.assertEqual(len(metadata[-1].metadata_list), 1)
                    if is_cloud:
                        self.assertEqual(maps[-1].width * maps[-1].height, 3)
                    else:
                        self.assertGreater(len(maps[-1].data), 0)
                    request = service.Request()
                    if is_cloud:
                        request.area.center_x, request.area.center_y, request.area.radius = 0.0, 0.0, 10.0
                    else:
                        request.cell_ids = [metadata[-1].metadata_list[0].cell_id]
                    future = client.call_async(request)
                    rclpy.spin_until_future_complete(node, future, timeout_sec=15)
                    log.seek(0)
                    self.assertTrue(future.done(), log.read())
                    response = future.result()
                    self.assertIsNotNone(response)
                    self.assertEqual(response.header.frame_id, "map")
                    if is_cloud:
                        self.assertEqual(len(response.new_pointcloud_with_ids), 1)
                        cell = response.new_pointcloud_with_ids[0]
                        self.assertEqual(cell.cell_id, metadata[-1].metadata_list[0].cell_id)
                        self.assertEqual(cell.pointcloud.width * cell.pointcloud.height, 3)
                        self.assertEqual(list(response.ids_to_remove), [])
                        request.cached_ids = [cell.cell_id]
                        future = client.call_async(request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=15)
                        self.assertTrue(future.done())
                        self.assertIsNotNone(future.result())
                        self.assertEqual(list(future.result().new_pointcloud_with_ids), [])
                        self.assertEqual(list(future.result().ids_to_remove), [])
                    else:
                        self.assertEqual(response.lanelet2_cells.header.frame_id, "map")
                        self.assertGreater(len(response.lanelet2_cells.data), 0)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail("Map loader failed to stop on SIGINT")
                log.seek(0)
                self.assertEqual(process.returncode, 0, log.read())
        finally:
            for subscription in subscriptions:
                node.destroy_subscription(subscription)
            node.destroy_node()
            rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
