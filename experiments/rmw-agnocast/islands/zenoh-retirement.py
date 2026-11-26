"""Stock Zenoh network-only publisher retirement witness. Run with a private router."""
import os
import select
import signal
import subprocess
import sys
import time

import rclpy
from rclpy.executors import ExternalShutdownException
from std_msgs.msg import Int32

if sys.argv[1:] and sys.argv[1] == "writer":
    rclpy.init()
    node = rclpy.create_node("retire_writer_" + str(os.getpid()))
    pub = node.create_publisher(Int32, sys.argv[2], 10)
    node.create_timer(.04,lambda: pub.publish(Int32(data=1)))
    try:
        if sys.argv[3] == "signal":
            rclpy.spin(node)
        else:
            while not select.select([sys.stdin], [], [], 0)[0]:
                rclpy.spin_once(node,timeout_sec=.04)
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()
    sys.exit(0)

rclpy.init()
node = rclpy.create_node("retire_observer")
for pair in range(12):
    mode = "signal" if pair % 2 == 0 else "orderly"
    topics = [f"/retirement/pair_{pair}_{i}" for i in range(2)]
    workers = [subprocess.Popen([sys.executable,__file__,"writer",topic,mode],
                                stdin=subprocess.PIPE) for topic in topics]
    seen = set()
    readers = [node.create_subscription(Int32,t,lambda _, name=t: seen.add(name),10) for t in topics]
    try:
        end = time.monotonic() + 7
        while not all(node.count_publishers(t)==1 for t in topics):
            assert time.monotonic()<end, topics
            rclpy.spin_once(node,timeout_sec=.025)
        while seen != set(topics):
            assert time.monotonic()<end, seen
            rclpy.spin_once(node,timeout_sec=.025)
        for sub in readers:
            node.destroy_subscription(sub)
        start = time.monotonic()
        for proc in workers:
            if mode == "signal":
                proc.send_signal(signal.SIGINT)
            else:
                proc.stdin.write(b"\n")
                proc.stdin.flush()
            assert proc.wait(timeout=5)==0
        times = {}
        while time.monotonic() < start+15 and len(times)<2:
            for topic in topics:
                if topic not in times and node.count_publishers(topic)==0:
                    times[topic] = round(time.monotonic()-start,3)
            rclpy.spin_once(node,timeout_sec=.025)
        print(mode,pair,"seconds from stop request",times, flush=True)
        assert all(times.get(t,16)<=7 for t in topics), times
    finally:
        for proc in workers:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
node.destroy_node()
rclpy.shutdown()
