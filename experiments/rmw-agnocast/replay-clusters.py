"""Replay recorded near-branch arrival order through the Universe cluster node."""
import re
import runpy
import struct
import sys
import time
from pathlib import Path

import rclpy
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import PointCloud2, PointField
from tier4_perception_msgs.msg import DetectedObjectsWithFeature

read_run = runpy.run_path(str(Path(__file__).with_name("summarize-isolation.py")))["read_run"]


def scene(seq, stamp):
    # One persistent landmark plus every three-acquisition visibility window.
    targets = [0] + [onset + 1 for onset in range(30) if onset <= seq < onset + 3]
    points = [(4 * target + .1 * x, .1 * y, 1.) for target in targets
              for x in range(4) for y in range(3)]
    msg = PointCloud2(height=1, width=len(points), is_dense=True, point_step=12,
                      row_step=12 * len(points))
    msg.header.frame_id = "base_link"
    msg.header.stamp.sec, msg.header.stamp.nanosec = divmod(stamp, 10**9)
    msg.fields = [PointField(name=name, offset=i * 4, datatype=PointField.FLOAT32, count=1)
                  for i, name in enumerate("xyz")]
    msg.data = b"".join(struct.pack("<fff", *p) for p in points)
    return msg, targets


def wait(node, condition):
    end = time.monotonic() + 10
    while not condition():
        if time.monotonic() >= end:
            raise RuntimeError("timed out waiting for cluster node or its output")
        rclpy.spin_once(node, timeout_sec=.05)


def replay(node, pub, replies, path):
    mode, condition = re.fullmatch(r"\d+-(\w+)-(healthy|slow-near)", path.stem).groups()
    near = read_run(path, mode, condition == "slow-near")[0]
    first = {}
    frames = sorted((stamp + age, seq, stamp) for seq, (stamp, age) in near.items() if age >= 0)
    for arrival, seq, stamp in frames:
        msg, targets = scene(seq, stamp)
        pub.publish(msg)
        wait(node, lambda: replies)
        output, = replies
        replies.clear()
        if output.header != msg.header:
            raise ValueError(f"wrong output header for {path} group {seq}")
        found = []
        for feature in output.feature_objects:
            points = feature.feature.cluster
            pos = feature.object.kinematics.pose_with_covariance.pose.position
            target = round(pos.x / 4)
            if (points.width * points.height != 12 or points.header != msg.header
                    or abs(pos.x - (4 * target + .15)) > .001 or abs(pos.y - .1) > .001):
                raise ValueError(f"incorrect cluster for {path} group {seq}")
            found.append(target)
            if target:
                first.setdefault(target - 1, arrival)
        if sorted(found) != targets:
            raise ValueError(f"wrong target list for {path} group {seq}: {found}")
    missed = sorted(set(range(30)) - first.keys())
    late = sorted(i for i, t in first.items() if t - near[i][0] > 100_000_000)
    if condition == "healthy" and (missed or late):
        raise ValueError(f"unexplained loss in healthy control {path}")
    return (f"frames={len(frames)} detected={len(first)}/30 "
            f"on_time={len(first) - len(late)}/30 missed={missed} late={late}")


def main():
    rclpy.init()
    node = rclpy.create_node("cluster_replay_fixture")
    replies = []
    pub = node.create_publisher(PointCloud2, "/input", qos_profile_sensor_data)
    sub = node.create_subscription(DetectedObjectsWithFeature, "/output", replies.append, 1)
    try:
        wait(node, lambda: pub.get_subscription_count() == 1 and sub.get_publisher_count() == 1)
        for root in map(Path, sys.argv[1:]):
            paths = sorted(root.glob("*.log"))
            main_pairs = {"pipeline-isolation-results": 8, "pipeline-busy-results": 4}[root.name]
            expected = {f"{trial}-{mode}-{condition}" for mode in ("native", "stock", "cyclone", "zenoh")
                        for trial in range(1, (main_pairs if mode in ("native", "stock") else 2) + 1)
                        for condition in ("healthy", "slow-near")}
            if {p.stem for p in paths} != expected:
                raise ValueError(f"incomplete or unexpected trace matrix under {root}")
            for path in paths:
                print(root.name, path.stem, replay(node, pub, replies, path), flush=True)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
