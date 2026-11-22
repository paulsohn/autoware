"""Inside the guarded Lyrical overlay. Run one three-LiDAR case and verify its bag."""
import math
import os
import re
import resource
import signal
import sqlite3
import statistics
import subprocess
import sys
import time
from pathlib import Path

from rclpy.serialization import deserialize_message
from sensor_msgs.msg import PointCloud2

mode, directory = sys.argv[1:]
assert mode in ("local", "healthy", "ingress", "egress")
out = Path(directory).resolve()
out.mkdir()
children = {}
env = dict(os.environ, SYNC_ROUTE_PILOT="1", ENABLE_AGNOCAST="0", ROS_LOG_DIR=str(out/"ros"))
if mode != "local":
    env["SYNC_DDS_TOP"] = "1"
if mode == "ingress":
    env["SYNC_INGRESS_FAULT"] = "1"


def start(name, cmd):
    with (out / (name + ".log")).open("x") as log:
        children[name] = subprocess.Popen(cmd, env=env, stdout=log, stderr=log,
                                          start_new_session=True)


def log(name):
    return (out / (name + ".log")).read_text(errors="replace")


def wait(test, seconds=25):
    deadline = time.monotonic() + seconds
    while not test():
        for name, child in children.items():
            if child.poll() is not None:
                raise RuntimeError(f"{name} exited {child.returncode}")
        if time.monotonic() > deadline:
            raise TimeoutError(test)
        time.sleep(.005)


def stop(name):
    proc = children[name]
    proc.send_signal(signal.SIGINT)
    assert proc.wait(timeout=10) == 0, name
    del children[name]


def events(name):
    return [(int(t), int(pid), status) for t,pid,status in re.findall(
        r"^event (\d+) (\d+) (started|-?\d+)$", log(name), re.M)]


try:
    if mode != "local":
        supervisor = Path(__file__).with_name("supervise-route.py")
        start("ingress", [sys.executable, supervisor, "r2a", "/dds/top", "/raw/sync/top"])
        start("egress", [sys.executable, supervisor, "a2r", "/output", "/dds/synchronized"])
        start("recorder", ["ros2", "bag", "record", "--topics", "/dds/synchronized", "/dds/top",
                           "-s", "sqlite3", "--max-cache-size", "0", "--disable-keyboard-controls",
                           "-o", str(out/"bag")])
    start("graph", ["ros2", "launch", "universe_sync_benchmark", "three_lidar.launch.py",
                    "mode:=stock", f"fixture_directory:={out}"])
    wait(lambda: (out/"sink-ready").exists())
    if mode != "local":
        wait(lambda: all(f"Subscribed to topic '{t}'" in log("recorder")
                         for t in ("/dds/top", "/dds/synchronized")))
    (out/"go").touch()
    wait(lambda: (out/"fault-window").exists())
    if mode in ("ingress", "egress"):
        start_event, = events(mode)
        killed = time.time_ns()
        os.kill(start_event[1], signal.SIGKILL)
        wait(lambda: len(events(mode)) == 3)
    graph = children["graph"]
    assert graph.wait(timeout=35) == 0 and "verified route pilot groups=24" in log("graph")
    del children["graph"]
    if mode != "local":
        stop("recorder")
        for name in ("ingress", "egress"):
            stop(name)
            statuses = [s for _,_,s in events(name)]
            assert statuses == (["started","-9","started","0"] if name == mode else ["started","0"])
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    print(f"graph_and_sidecars_cpu_s={usage.ru_utime + usage.ru_stime:.3f}")
finally:
    for child in children.values():
        if child.poll() is None:
            os.killpg(child.pid, signal.SIGINT)
            try:
                child.wait(timeout=8)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()

samples = [(int(seq), kind, int(ns)/1e6) for seq,kind,ns in
           re.findall(r"sample,(\d+),(full|partial),(\d+)", log("graph"))]
assert [i for i,_,_ in samples] == list(range(24))
for kind in ("full", "partial"):
    values = sorted(value for _,k,value in samples if k == kind)
    print(f"{kind} count={len(values)} median_ms={statistics.median(values):.3f} "
          f"p95_ms={values[math.ceil(.95*len(values))-1]:.3f}")
if mode == "local":
    sys.exit(0)

stamps = list(map(int, (out/"stamps").read_text().split()))
by_stamp = {t:i for i,t in enumerate(stamps)}
db, = (out/"bag").glob("*.db3")
mirrored, top = [], []
with sqlite3.connect(f"file:{db}?mode=ro", uri=True) as conn, (out/"recorded.csv").open("w") as audit:
    audit.write("topic,sequence,source_stamp_ns,bag_ns\n")
    for topic, data, received in conn.execute(
            "SELECT name,data,timestamp FROM messages JOIN topics ON topics.id=topic_id ORDER BY messages.id"):
        msg = deserialize_message(data, PointCloud2)
        stamp = msg.header.stamp.sec*10**9 + msg.header.stamp.nanosec
        if topic == "/dds/top":
            seq = by_stamp[stamp-2_000_000]
            assert msg.width == 32768 and msg.data[12] == seq
            top.append(seq)
        else:
            assert topic == "/dds/synchronized"
            seq = by_stamp[stamp]
            local = deserialize_message((out/f"local-{seq}").read_bytes(), PointCloud2)
            assert msg == local, (seq, "bag differs from local cloud")
            mirrored.append(seq)
        audit.write(f"{topic},{seq},{stamp},{received}\n")
assert top == list(range(0,24,2))
assert mirrored == sorted(set(mirrored))
missing = sorted(set(range(24))-set(mirrored))
missing_top = [seq for seq,k,_ in samples if seq % 2 == 0 and k == "partial"]
assert bool(missing_top) == (mode == "ingress")
assert bool(missing) == (mode == "egress")
print(f"local=24/24 mirrored={len(mirrored)}/24 input_top=12/12 mirror_missing={missing} top_timeouts_on_even={missing_top}")
if mode in ("ingress", "egress"):
    restarted = events(mode)[2][0]
    losses = missing_top if mode == "ingress" else missing
    step = 2 if mode == "ingress" else 1
    assert losses == list(range(losses[0], losses[-1]+step, step))
    assert all(killed < stamps[i] < restarted+200_000_000 for i in losses)
    first = losses[-1]+step
    assert first < 24 and stamps[first] >= restarted
    print(f"kill_ns={killed} respawn_ns={restarted} first_recovered_seq={first} source_stamp_ns={stamps[first]}")
    if mode == "ingress":
        assert samples[first][1] == "full" and first in mirrored
        print(f"first_complete_members_ns={[stamps[first]+i*1_000_000 for i in range(3)]}")
