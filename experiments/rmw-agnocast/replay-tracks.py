"""Pace actual cluster detections into a fresh Universe tracker for each delivery trace."""
import copy
import os
import runpy
import signal
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path

import rclpy
from autoware_perception_msgs.msg import DetectedObjects, Shape, TrackedObjects
from geometry_msgs.msg import Point32
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import PointCloud2
from sensor_msgs_py.point_cloud2 import read_points
from tier4_perception_msgs.msg import DetectedObjectsWithFeature

cluster = runpy.run_path(str(Path(__file__).with_name("replay-clusters.py")))
wait = cluster["wait"]
spacing = float(os.environ.get("REPLAY_TARGET_SPACING_M", "4"))
return_group = int(os.environ.get("REPLAY_RETURN_GROUP", "25"))
tracker_dir = Path("src/universe/autoware_universe/perception/autoware_multi_object_tracker")


def clustered_scenes(node):
    responses = []
    pub = node.create_publisher(PointCloud2, "/input", qos_profile_sensor_data)
    sub = node.create_subscription(DetectedObjectsWithFeature, "/output", responses.append, 1)
    wait(node, lambda: pub.get_subscription_count() == 1 and sub.get_publisher_count() == 1)
    scenes = []
    glimpse = os.environ.get("REPLAY_HIDE_GLIMPSE") != "1"
    for seq in range(32):
        cloud, expected = cluster["scene"](seq, time.time_ns())
        pub.publish(cloud)
        wait(node, lambda: responses)
        output = responses.pop()
        assert output.header == cloud.header
        msg = DetectedObjects(header=output.header)
        ids = []
        for feature in output.feature_objects:
            obj = feature.object
            pos = obj.kinematics.pose_with_covariance.pose.position
            pts = read_points(feature.feature.cluster, field_names=("x", "y"))
            xs, ys = pts["x"] - pos.x, pts["y"] - pos.y
            # Shape-estimation fixture for these rectangular, planar clusters.
            obj.shape.type = Shape.POLYGON
            obj.shape.dimensions.x = float(max(xs) - min(xs))
            obj.shape.dimensions.y = float(max(ys) - min(ys))
            obj.shape.dimensions.z = .1
            obj.shape.footprint.points = [Point32(x=float(x), y=float(y)) for x, y in
                                          [(min(xs), min(ys)), (max(xs), min(ys)),
                                           (max(xs), max(ys)), (min(xs), max(ys))]]
            obj.kinematics.pose_with_covariance.pose.orientation.w = 1.
            target = round(pos.x / 4)
            ids.append(target)
            pos.x += (spacing - 4) * target
            msg.objects.append(obj)
        assert sorted(ids) == expected
        # A second established landmark is occluded, glimpsed for three frames,
        # and then visible again from the selected return group.
        if seq <= 3 or (glimpse and 9 <= seq <= 11) or seq >= return_group:
            occluded = copy.deepcopy(msg.objects[ids.index(0)])
            occluded.kinematics.pose_with_covariance.pose.position.y += 8.
            msg.objects.append(occluded)
        scenes.append(msg)
    return scenes


def tracker_command(name):
    exe = Path(os.environ["TRACKER_PREFIX"]) / "lib/autoware_multi_object_tracker/multi_object_tracker_node"
    args = [str(exe), "--ros-args", "-r", "__node:=" + name]
    for config in sorted((tracker_dir / "config").glob("*.param.yaml")):
        args.extend(["--params-file", str(config)])
    args.extend(["-p", "publish_merged_objects:=false"])
    for i in range(1, 13):
        channel = "lidar_clustering" if i == 1 else "none"
        args.extend(["-p", f"input/detection{i:02}/channel:={channel}"])
    return args


def replay(node, scenes, path, index):
    _, mode, condition = path.stem.split("-")[:3]
    near = cluster["read_run"](path, mode, condition == "slow")[0]
    frames = sorted((stamp + age, seq) for seq, (stamp, age) in near.items() if age >= 0)
    name = f"tracker_replay_{index}"
    pub = node.create_publisher(DetectedObjects, f"/{name}/input/detection01/objects", 1)
    replies = []
    sub = node.create_subscription(TrackedObjects, f"/{name}/output/objects",
                                   lambda msg: replies.append((time.time_ns(), msg)), 1)
    proc = subprocess.Popen(tracker_command(name))
    try:
        wait(node, lambda: pub.get_subscription_count() == 1 and sub.get_publisher_count() == 1)
        time.sleep(.15)  # allow transient-local static TF delivery
        shift = time.time_ns() + 50_000_000 - near[0][0]
        first, uuids = {}, defaultdict(set)
        positions = defaultdict(set)
        landmark_frames, landmark_times = [], []
        before, after = set(), set()
        worst_lag = 0
        duplicate_frames = []
        for arrival, seq in frames:
            msg = scenes[seq]
            stamp = near[seq][0] + shift
            msg.header.stamp.sec, msg.header.stamp.nanosec = divmod(stamp, 10**9)
            release = arrival + shift
            time.sleep(max(0, (release - time.time_ns()) / 1e9))
            worst_lag = max(worst_lag, time.time_ns() - release)
            pub.publish(msg)
            wait(node, lambda: replies)
            received, output = replies.pop()
            assert output.header.stamp == msg.header.stamp and output.header.frame_id == "map", seq
            seen = set()
            for obj in output.objects:
                pos = obj.kinematics.pose_with_covariance.pose.position
                if abs(pos.y - 8.1) < .3 and abs(pos.x - .15) < .3:
                    if seq <= 3:
                        before.add(bytes(obj.object_id.uuid))
                    if seq >= return_group:
                        after.add(bytes(obj.object_id.uuid))
                    continue
                target = round((pos.x - .15) / spacing)
                if not 0 <= target <= 30 or abs(pos.x - (spacing * target + .15)) > .3 or abs(pos.y - .1) > .3:
                    raise ValueError(f"unmatched track at {path} group {seq}: {pos}")
                if target in seen:
                    duplicate_frames.append((seq, target))
                seen.add(target)
                uid = bytes(obj.object_id.uuid)
                uuids[target].add(uid)
                positions[uid].add(target)
                if target:
                    onset = target - 1
                    assert onset <= seq
                    first.setdefault(onset, (received - (near[onset][0] + shift)) / 1e6)
            if 0 in seen:
                landmark_frames.append(seq)
                landmark_times.append(received)
        assert landmark_frames and landmark_frames[0] < 4 and landmark_frames[-1] == 31
        assert before and after, (path, before, after)
        holes = [seq for _, seq in frames if seq >= landmark_frames[0] and seq not in landmark_frames]
        landmark_gap = max(b - a for a, b in zip(landmark_times, landmark_times[1:])) / 1e6
        switches = sum(len(locations) > 1 for locations in positions.values())
        missed = sorted(set(range(30)) - first.keys())
        print(f"trace={path.parent.name}/{path.stem} frames={len(frames)} confirmed={len(first)} "
              f"within_100ms={sum(delay <= 100 for delay in first.values())} missed={missed} "
              f"occluded_common={len(before & after)} occluded_ids={len(before)}/{len(after)} landmark_ids={len(uuids[0])} landmark_holes={holes} "
              f"switch_ids={switches} duplicates={duplicate_frames} landmark_gap_ms={landmark_gap:.2f} max_release_lag_ms={worst_lag / 1e6:.2f} "
              f"first_confirm_ms={','.join(f'{i}:{first[i]:.1f}' for i in sorted(first))}", flush=True)
    finally:
        if proc.poll() is None:
            proc.send_signal(signal.SIGINT)
        status = proc.wait(timeout=10)
        node.destroy_subscription(sub)
        node.destroy_publisher(pub)
        if status:
            raise RuntimeError(f"tracker exited {status}")


def main():
    rclpy.init()
    node = rclpy.create_node("track_replay_fixture")
    try:
        scenes = clustered_scenes(node)
        for i, p in enumerate(sys.argv[1:]):
            replay(node, scenes, Path(p), i)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
