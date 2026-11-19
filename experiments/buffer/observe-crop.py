import csv
import os
import subprocess
import sys
import time
from pathlib import Path
if os.environ.get("RMW_IMPLEMENTATION") != "rmw_zenoh_cpp":
    raise SystemExit("Select rmw_zenoh_cpp before starting Python")

import rclpy
from autoware_internal_debug_msgs.msg import Float64Stamped

out = Path(sys.argv[1])
command = [sys.executable, 'src/crop_pipeline_benchmark/run_case.py', 'rmw_zenoh_cpp',
           'shmem', str(out / 'pipeline.csv'), '--two-edge']
proc = subprocess.Popen(command)
try:
    time.sleep(0.6)
    rclpy.init()
    node = rclpy.create_node('crop_timer_reader')
    times = []
    sub = None
    deadline = time.monotonic() + 50
    while proc.poll() is None and time.monotonic() < deadline:
        if sub is None:
            for topic, types in node.get_topic_names_and_types():
                if topic.endswith('/debug/processing_time_ms'):
                    print('timer topic', topic, types, flush=True)
                    sub = node.create_subscription(Float64Stamped, topic, lambda m: times.append((m.stamp.sec, m.stamp.nanosec, m.data)), 100)
                    break
        rclpy.spin_once(node, timeout_sec=0.05)
    if not times: print(node.get_topic_names_and_types(),flush=True)
    if proc.wait(timeout=2) != 0 or len(times) < 50:
        raise RuntimeError(f'pipeline status {proc.returncode}, {len(times)} timer samples')
    with (out / 'processing.csv').open('x') as f:
        csv.writer(f).writerows(times)
    print('timer samples',len(times))
    node.destroy_node()
    rclpy.shutdown()
finally:
    if proc.poll() is None:
        proc.terminate()
        proc.wait(timeout=10)
