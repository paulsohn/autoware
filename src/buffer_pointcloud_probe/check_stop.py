import os
import signal
import subprocess
import sys
import time

import rclpy
from nav_msgs.msg import Odometry

mode = sys.argv[1]
if mode == "standalone":
    argv = ["ros2", "launch", "autoware_stop_filter", "stop_filter.launch.xml",
            "input_odom_name:=input/odom", "output_odom_name:=output/odom"]
else:
    argv = ["ros2", "run", "rclcpp_components", "component_container"]
child = subprocess.Popen(argv, start_new_session=True)
try:
    if mode == "component":
        subprocess.run(["ros2", "component", "load", "/ComponentManager",
                        "autoware_stop_filter", "autoware::stop_filter::StopFilterNode",
                        "-p", "vx_threshold:=0.1", "-p", "wz_threshold:=0.02"], check=True, timeout=30)
    rclpy.init()
    node = rclpy.create_node("integration_probe")
    outputs = {}
    sub = node.create_subscription(Odometry, "/output/odom",
        lambda m: outputs.update({m.header.frame_id: m.twist.twist.linear.x}), 1)
    pub = node.create_publisher(Odometry, "/input/odom", 1)
    for key, speed, expected in [("slow", 0.05, 0.0), ("moving", 0.2, 0.2)]:
        msg = Odometry()
        msg.header.frame_id = key
        msg.twist.twist.linear.x = speed
        deadline = time.monotonic() + 15
        while key not in outputs:
            if child.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("stop filter timed out")
            pub.publish(msg)
            rclpy.spin_once(node, timeout_sec=0.1)
        assert outputs[key] == expected, outputs
    print(mode, outputs, flush=True)
    node.destroy_node()
    rclpy.shutdown()
finally:
    if child.poll() is None:
        os.killpg(child.pid, signal.SIGINT)
    try:
        status = child.wait(timeout=15)
    except subprocess.TimeoutExpired:
        os.killpg(child.pid, signal.SIGKILL)
        child.wait()
        raise
    if status != 0:
        raise RuntimeError(f"shutdown exited {status}")
