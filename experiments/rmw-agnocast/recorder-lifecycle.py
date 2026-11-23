"""Run in the pilot overlay with a fresh directory and the stock Cyclone RMW."""
import os
import re
import signal
import sqlite3
import subprocess
import sys
import time
from pathlib import Path

from ament_index_python.packages import get_package_prefix
from rclpy.serialization import deserialize_message
from sensor_msgs.msg import PointCloud2

mode, directory = sys.argv[1:]
assert mode in ("demand", "always")
out = Path(directory).resolve()
out.mkdir()
topic = "/dds/synchronized"
children = {}
env = dict(os.environ, ROS_LOG_DIR=str(out / "ros"), SYNC_RECORDER_STEPS="1", SYNC_ROUTE_PILOT="1")


def start(name, cmd, **extra):
    with (out / f"{name}.log").open("x") as log:
        children[name] = subprocess.Popen(cmd, stdout=log, stderr=log, env=dict(env, **extra),
                                          start_new_session=True)


def log(name):
    return (out / f"{name}.log").read_text()


def wait(predicate):
    deadline = time.monotonic() + 12
    while not predicate():
        if time.monotonic() >= deadline:
            raise TimeoutError(predicate)
        for name, process in children.items():
            if name != "graph" and process.poll() is not None:
                raise RuntimeError(f"{name} exited with {process.returncode}")
        time.sleep(.01)


def stop(name):
    proc = children[name]
    proc.send_signal(signal.SIGINT)
    assert proc.wait(timeout=8) == 0, name
    del children[name]


def record(name):
    start(name, ["ros2", "bag", "record", "--topics", topic, "-s", "sqlite3",
                 "--max-cache-size", "0", "--disable-keyboard-controls", "-o", str(out / name)])
    wait(lambda: f"Subscribed to topic '{topic}'" in log(name))


def matched(count, taken):
    if mode == "always":
        return
    wait(lambda: f"matches={count} received={taken} forwarded={taken}" in log("relay"))


def release(first):
    (out / f"release-{first}").touch()
    wait(lambda: (out / f"seen-{first + 4}").exists())
    time.sleep(.15)  # Allow the final volatile DDS sample to reach the recorder before SIGINT.


try:
    executable = Path(get_package_prefix("agnocast_rmw_loan_probe")) / "lib/agnocast_rmw_loan_probe/route_bridge"
    hook = Path(get_package_prefix("agnocastlib")) / "lib/libagnocast_heaphook.so"
    start("relay", [executable, "a2r", "cloud", "/output", topic], LD_PRELOAD=str(hook),
          RMW_IMPLEMENTATION="rmw_agnocast", AGNOCAST_RMW_HOST_CLOUD="1",
          AGNOCAST_NO_DISCOVERY_AGENT="1", AGNOCAST_BRIDGE_MODE="OFF",
          **({"AGNOCAST_BRIDGE_DEMAND": "1"} if mode == "demand" else {}),
          BRIDGE_READY_FILE=str(out / "relay-ready"))
    wait(lambda: (out / "relay-ready").exists())
    start("graph", ["ros2", "launch", "universe_sync_benchmark", "three_lidar.launch.py",
                    "mode:=stock", f"fixture_directory:={out}"])
    wait(lambda: (out / "sink-ready").exists())
    listing = subprocess.check_output(["ros2", "topic", "list", "-t", "--no-daemon",
                                       "--spin-time", "1"], env=env, text=True, timeout=5)
    assert f"{topic} [sensor_msgs/msg/PointCloud2]" in listing
    (out / "go").touch()
    release(0)
    record("A")
    matched(1, 0)
    release(4)
    record("B")
    matched(2, 4)
    release(8)
    stop("A")
    matched(1, 8)
    release(12)
    stop("B")
    matched(0, 12)
    release(16)
    record("C")
    matched(1, 12)
    release(20)
    assert children["graph"].wait(timeout=12) == 0 and "verified route pilot groups=24" in log("graph")
    del children["graph"]
    stop("C")
    matched(0, 16)
    ticks = Path(f"/proc/{children['relay'].pid}/stat").read_text().rsplit(")", 1)[1].split()
    cpu = (int(ticks[11]) + int(ticks[12])) / os.sysconf("SC_CLK_TCK")
    stop("relay")
    expected_takes = 16 if mode == "demand" else 24
    assert log("relay").rstrip().endswith(f"received={expected_takes} forwarded=16")
    print(f"{mode} relay_cpu_s={cpu:.3f} received={expected_takes} forwarded=16")
finally:
    for proc in children.values():
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGINT)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()

stamps = {int(t): i for i, t in enumerate((out / "stamps").read_text().split())}
assert len(stamps) == 24
for name, expected in (("A", range(4, 12)), ("B", range(8, 16)), ("C", range(20, 24))):
    found = []
    db, = (out / name).glob("*.db3")
    with sqlite3.connect(f"file:{db}?mode=ro", uri=True) as conn:
        for (raw,) in conn.execute("select data from messages join topics on topics.id=topic_id "
                                   "where name=? order by messages.id", (topic,)):
            cloud = deserialize_message(raw, PointCloud2)
            seq = stamps[cloud.header.stamp.sec * 10**9 + cloud.header.stamp.nanosec]
            assert cloud == deserialize_message((out / f"local-{seq}").read_bytes(), PointCloud2)
            found.append(seq)
    assert found == list(expected), (name, found)
    print(f"{name} verified sequences {found}")
print("local=24/24", *re.findall(r"matches=\d+ received=\d+ forwarded=\d+", log("relay")), sep="\n")
