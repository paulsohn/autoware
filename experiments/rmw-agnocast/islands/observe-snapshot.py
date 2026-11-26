"""Verify both discovery announcements as an ordinary network-side ROS subscriber."""
import json
import sys
import time
from pathlib import Path

import rclpy
from ament_index_python.packages import get_package_share_directory
from rmw_agnocast_discovery_msgs.msg import DiscoveryEndpoint, IslandSnapshot

boot_id = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
expected_roles = dict(zip(map(int, sys.argv[1:]),
                          (DiscoveryEndpoint.PUBLISHER, DiscoveryEndpoint.SUBSCRIPTION)))
type_file = Path(get_package_share_directory("std_msgs")) / "msg/Int32.json"
expected_hash = json.loads(type_file.read_text())["type_hashes"][0]["hash_string"]
seen = {}

def observe(message):
    if message.ipc_ns_inode not in expected_roles:
        return
    for endpoint in message.endpoints:
        if endpoint.topic_name != "/island_control/pinned":
            continue
        assert message.host_uuid == boot_id and message.generation > 0
        assert endpoint.role == expected_roles[message.ipc_ns_inode]
        assert endpoint.reliability == DiscoveryEndpoint.RELIABLE
        assert endpoint.type_name == "std_msgs/msg/Int32"
        digest = endpoint.type_hash
        assert f"RIHS{digest.version:02}_{bytes(digest.value).hex()}" == expected_hash
        seen[message.ipc_ns_inode] = message.generation

rclpy.init()
node = rclpy.create_node("typed_snapshot_observer")
node.create_subscription(IslandSnapshot, "/_rmw_agnocast/discovery_v2", observe, 10)
deadline = time.monotonic() + 10
while seen.keys() != expected_roles.keys():
    assert time.monotonic() < deadline, seen
    rclpy.spin_once(node, timeout_sec=.1)
print("Verified typed snapshots from", seen, flush=True)
node.destroy_node()
rclpy.shutdown()
