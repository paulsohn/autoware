"""Exercise the crop's lazy Cyclone publisher in a separate, untimed pipeline run."""
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import rclpy
from autoware_internal_msgs.msg import PublishedTime

topic = "/output/debug/published_time"
stamps = []
rclpy.init()
node = rclpy.create_node("cold_crop_observer")
try:
    if node.count_publishers(topic):
        raise RuntimeError(f"expected a fresh graph for {topic}")
    node.create_subscription(
        PublishedTime, topic,
        lambda msg: stamps.append((msg.header.stamp.sec, msg.header.stamp.nanosec)), 10)
    with tempfile.TemporaryDirectory() as tmp:
        with subprocess.Popen([
            sys.executable, "src/universe_crop_benchmark/run_case.py", "fixed",
            str(Path(tmp) / "frames.csv"), "--payload-scope",
        ]) as run:
            deadline = time.monotonic() + 45
            try:
                while run.poll() is None and time.monotonic() < deadline:
                    rclpy.spin_once(node, timeout_sec=0.1)
                if run.wait(timeout=1):
                    raise RuntimeError("cold crop pipeline failed")
            finally:
                if run.poll() is None:
                    run.terminate()
    if not stamps or any(stamp == (0, 0) for stamp in stamps) or stamps != sorted(set(stamps)):
        raise RuntimeError(f"invalid PublishedTime sequence ({len(stamps)} messages)")
    print(f"Received {len(stamps)} distinct stamped diagnostics from {topic}")
finally:
    node.destroy_node()
    rclpy.shutdown()
