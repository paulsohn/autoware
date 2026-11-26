"""One gateway and an ordinary ROS installation on the other side of the wire."""
import json
import shlex
import subprocess
import sys
import time
from pathlib import Path

ROOT = "/stock_probe/"
TYPE = "std_msgs/msg/Int32"
N = 40


def worker(role, topic):
    import rclpy
    from std_msgs.msg import Int32

    tag = topic.split("/")[-1]
    rclpy.init()
    node = rclpy.create_node("stock_probe_" + role)
    values = []
    if role == "writer":
        pub = node.create_publisher(Int32, topic, 64)
    elif role == "reader":
        node.create_subscription(Int32, topic, lambda msg: values.append(msg.data), 64)
    observed = {}
    Path("/tmp/ready-" + tag + role).touch()
    deadline = time.monotonic() + 25
    while time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=.02)
        if role == "graph":
            for key, count in (("publisher", node.count_publishers(topic)),
                               ("subscriber", node.count_subscribers(topic))):
                if count and key not in observed:
                    observed[key] = time.monotonic_ns()
        if Path("/tmp/send-" + tag).exists() and role == "writer":
            for i in range(N):
                pub.publish(Int32(data=i))
                time.sleep(.025)
            break
        if Path("/tmp/stop-" + tag).exists() and role != "writer":
            break
    else:
        raise RuntimeError("worker timed out " + tag)
    print(json.dumps(observed if role == "graph" else values), flush=True)
    node.destroy_node()
    rclpy.shutdown()


def decode(topic):
    import rosbag2_py
    from rclpy.serialization import deserialize_message
    from std_msgs.msg import Int32
    reader = rosbag2_py.SequentialReader()
    reader.open(rosbag2_py.StorageOptions(uri="/tmp/bag-" + topic.split("/")[-1],
                                         storage_id="mcap"), rosbag2_py.ConverterOptions("", ""))
    values = []
    while reader.has_next():
        name, data, _ = reader.read_next()
        if name == topic:
            values.append(deserialize_message(data, Int32).data)
    print(json.dumps(values), flush=True)


def exercise(backend, islands, run, docker, hook, out):
    network = "rmw_" + ("cyclonedds" if backend == "cyclone" else backend) + "_cpp"
    cases = [(policy, mode, order) for policy in ("dynamic", "configured")
             for mode in ("explicit", "all") for order in ("early", "late")]
    config = dict(dynamic_discovery=True, network_rmw=network, heaphook=hook,
                  discovery_namespace="/stock_probe",
                  exports=[dict(topic=ROOT + "_".join(case), type=TYPE)
                           for case in cases if case[0] == "configured"])
    subprocess.run(["docker", "exec", "-i", islands[0], "tee", "/tmp/stock.json"],
                   input=json.dumps(config), text=True, stdout=subprocess.DEVNULL, check=True)
    gateway = run(0, "gateway", "/ws/build/manual-gateway/install/agnocast_cyclone_gateway/lib/"
                  "agnocast_cyclone_gateway/topic_gateway --config /tmp/stock.json")
    result = {}

    def launch(i, label, command):
        return run(i, label, command, native=i == 0,
                   environment=("-e", "RMW_IMPLEMENTATION=" + network) if i else ())

    def wait_file(i, name, proc):
        end = time.monotonic() + 12
        while subprocess.run(["docker", "exec", islands[i], "test", "-e", "/tmp/" + name]).returncode:
            assert proc.poll() is None and time.monotonic() < end, name
            time.sleep(.1)

    def start(i, tag, role):
        path = "/fixture/" if i else "/ws/experiments/rmw-agnocast/islands/"
        proc = launch(i, tag + role, f"python3 {path}stock.py {role} {ROOT}{tag}")
        wait_file(i, "ready-" + tag + role, proc)
        return proc

    def finish(proc, tag):
        assert proc.wait(timeout=15) == 0, tag
        for line in reversed((out / f"{backend}-{tag}.log").read_text().splitlines()):
            try:
                return json.loads(line)
            except ValueError:
                continue
        raise AssertionError(tag)

    def touch(i, name):
        docker("exec", islands[i], "touch", "/tmp/" + name)

    for direction in ("export", "import"):
        wi = int(direction == "import")
        reader = start(1-wi, direction, "reader")
        writer = start(wi, direction, "writer")
        time.sleep(3)
        touch(wi, "send-" + direction)
        finish(writer, direction + "writer")
        time.sleep(.4)
        touch(1-wi, "stop-" + direction)
        result[direction] = finish(reader, direction + "reader")
        assert result[direction] == list(range(N)), (direction, result)

    for policy, mode, order in cases:
        tag = "_".join((policy, mode, order))
        topic = ROOT + tag
        graph = start(1, tag, "graph")
        def recorder():
            arg = "-a" if mode == "all" else "--topics " + topic
            command = f'echo $$ > /tmp/pid-{tag}; exec ros2 bag record -s mcap -o /tmp/bag-{tag} {arg}'
            proc = launch(1, tag + "bag", "bash -c " + shlex.quote(command))
            wait_file(1, "bag-" + tag, proc)
            return proc
        if order == "early":
            bag = recorder()
            time.sleep(1)
            writer = start(0, tag, "writer")
        else:
            writer = start(0, tag, "writer")
            time.sleep(1)
            bag = recorder()
        time.sleep(3)
        send_ns = time.monotonic_ns()
        touch(0, "send-" + tag)
        finish(writer, tag + "writer")
        time.sleep(.5)
        docker("exec", islands[1], "kill", "-INT", docker("exec", islands[1], "cat", "/tmp/pid-" + tag))
        assert bag.wait(timeout=8) == 0, tag
        touch(1, "stop-" + tag)
        seen = finish(graph, tag + "graph")
        values = finish(launch(1, tag + "decode", "python3 /fixture/stock.py decode " + topic),
                        tag + "decode")
        result[tag] = dict(graph_ns=seen, send_signal_ns=send_ns, values=values)
        (out / f"{backend}-stock.json").write_text(json.dumps(result, indent=2))
        print(backend, tag, len(values), seen, flush=True)
        assert gateway.poll() is None
        if policy == "configured" or seen:
            assert values == list(range(N)), tag
        else:
            assert values == [], tag


if __name__ == "__main__":
    role, topic = sys.argv[1:]
    if role == "decode":
        decode(topic)
    else:
        worker(role, topic)
