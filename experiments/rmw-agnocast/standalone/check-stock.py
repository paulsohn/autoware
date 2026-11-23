"""Exercise an unmodified rclcpp publisher from a separate rclpy process."""
import os
import signal
import sqlite3
import tempfile
from pathlib import Path
import subprocess
import sys
import time

import rclpy
from rcl_interfaces.srv import GetParameters
from std_msgs.msg import String
from rclpy.serialization import deserialize_message


def check_recording(topic):
    with tempfile.TemporaryDirectory() as root:
        bag = Path(root) / 'bag'
        recorder = subprocess.Popen([
            'ros2', 'bag', 'record', '--storage', 'sqlite3', '-o', str(bag), '--topics', topic,
        ])
        try:
            time.sleep(6)
            assert recorder.poll() is None, recorder.returncode
        finally:
            if recorder.poll() is None:
                recorder.send_signal(signal.SIGINT)
            recorder.wait(timeout=6)
        assert recorder.returncode == 0, recorder.returncode
        with sqlite3.connect(next(bag.glob('*.db3'))) as db:
            rows = db.execute(
                'select name, type, serialization_format, data '
                'from topics join messages on topics.id = topic_id'
            ).fetchall()
        assert rows
        for name, ros_type, fmt, payload in rows:
            assert (name, ros_type, fmt) == (topic, 'std_msgs/msg/String', 'cdr')
            assert 'Hello' in deserialize_message(payload, String).data
        print(f'decoded stock bag samples: {len(rows)}', flush=True)


def main():
    rclpy.init()
    node = rclpy.create_node('native_stock_observer')
    messages = []
    topic = f'/stock_{os.getpid()}'
    sub = node.create_subscription(String, topic, lambda msg: messages.append(msg.data), 10)
    name = f'stock_{os.getpid()}'
    proc = subprocess.Popen([sys.argv[1], '--ros-args', '-r', f'__node:={name}', '-r', f'topic:={topic}'])
    try:
        until = time.monotonic() + 8
        while not messages and time.monotonic() < until:
            rclpy.spin_once(node, timeout_sec=0.1)
        assert messages and 'Hello' in messages[0], messages
        assert node.count_publishers(topic) == 1
        client = node.create_client(GetParameters, f'/{name}/get_parameters')
        assert client.wait_for_service(timeout_sec=3)
        request = GetParameters.Request(names=['use_sim_time'])
        future = client.call_async(request)
        rclpy.spin_until_future_complete(node, future, timeout_sec=4)
        assert future.done() and len(future.result().values) == 1
        print('stock C++ talker to Python and Python parameter request passed', flush=True)
        with open('/proc/self/maps') as maps:
            loaded = maps.read()
        for lib in ['libddsc.so', 'librmw_cyclonedds', 'libfastdds.so', 'librmw_fastrtps', 'librosidl_dynamic_typesupport_fastrtps.so']:
            assert lib not in loaded, lib
        check_recording(topic)
        proc.kill()
        proc.wait(timeout=3)
        assert node.count_publishers(topic) == 0
        assert name not in node.get_node_names()
        print('abrupt process exit removed graph endpoints', flush=True)
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=3)
        node.destroy_subscription(sub)
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
