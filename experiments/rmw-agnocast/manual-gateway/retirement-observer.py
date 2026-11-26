"""Observe network graph identities without subscribing to either data topic."""
import json
import os
from pathlib import Path
import select
import sys
import time

import rclpy

side, stop = sys.argv[1:]
rclpy.init()
assert rclpy.get_rmw_implementation_identifier() == "rmw_zenoh_cpp"
node = rclpy.create_node("retirement_observer_" + side)
poll = select.poll()
workers = {}
seen = set()
deadline = time.monotonic() + 95

def log(**event):
    print(json.dumps(dict(ns=time.time_ns(), **event)), flush=True)

log(ready=True)
while not Path(stop).exists():
    assert time.monotonic() < deadline
    for topic in ("active1", "active2"):
        path = "/churn/" + topic
        count = node.count_publishers(path)
        infos = node.get_publishers_info_by_topic(path)
        identities = {}
        for info in infos:
            gid = bytes(info.endpoint_gid).hex()
            identities[gid] = info.node_name
            if side == "host" and gid not in seen:
                pid = int(info.node_name.removeprefix("manual_gateway_destination_"))
                seen.add(gid)
                try:
                    fd = os.pidfd_open(pid)
                except ProcessLookupError:
                    log(gone_before_first_observation=gid, topic=topic, pid=pid)
                    continue
                assert b"--endpoint" in Path(f"/proc/{pid}/cmdline").read_bytes().split(b"\0")
                poll.register(fd, select.POLLIN)
                workers[fd] = (topic, gid, pid)
        log(topic=topic, count=count, endpoints=identities)
    for fd, mask in poll.poll(0):
        assert mask & select.POLLIN
        topic, gid, pid = workers.pop(fd)
        log(exited=gid, topic=topic, pid=pid)
        poll.unregister(fd)
        os.close(fd)
    rclpy.spin_once(node, timeout_sec=.05)
for fd, (topic, gid, pid) in workers.items():
    log(alive_at_end=gid, topic=topic, pid=pid)
    os.close(fd)
node.destroy_node()
rclpy.shutdown()
