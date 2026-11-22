"""Exercise an outbound PointCloud2 route with a persistent stock Cyclone bag and Universe crop."""
import csv
import os
import re
import signal
import sqlite3
import struct
import subprocess
import sys
import time
from pathlib import Path

import rclpy
from ament_index_python.packages import get_package_prefix
from rclpy.serialization import deserialize_message
from sensor_msgs.msg import PointCloud2

mode, target = sys.argv[1:]
assert mode in ("healthy", "restart")
out = Path(target).resolve()
out.mkdir()
limit = out / "limit"
limit.write_text("0")
count = 150
children = {}


def binary(pkg, exe):
    return Path(get_package_prefix(pkg)) / "lib" / pkg / exe


env = dict(os.environ, RMW_IMPLEMENTATION="rmw_agnocast", AGNOCAST_RMW_TOPIC="/input:/output",
           AGNOCAST_RMW_HOST_CLOUD="1", ENABLE_AGNOCAST="0", AGNOCAST_BRIDGE_MODE="OFF",
           AGNOCAST_NO_DISCOVERY_AGENT="1", BENCH_LIFECYCLE_LIMIT=str(limit),
           LD_PRELOAD=str(Path(get_package_prefix("agnocastlib")) / "lib/libagnocast_heaphook.so"))


def start(name, args, base=env, **options):
    with (out / (name + ".log")).open("x") as log:
        children[name] = subprocess.Popen(args, env=dict(base, **options), stdout=log, stderr=log)


def stop(name, sig=signal.SIGINT):
    child = children[name]
    child.send_signal(sig)
    code = child.wait(timeout=8)
    assert code == (-sig if sig == signal.SIGKILL else 0), (name, code)
    del children[name]


def wait(test, what, timeout=12):
    end = time.monotonic() + timeout
    while not test():
        for name, child in children.items():
            if child.poll() is not None:
                raise RuntimeError((name, child.returncode))
        if time.monotonic() >= end:
            raise TimeoutError(what)
        time.sleep(.01)


def sink():
    path = out / "crop.csv"
    if not path.exists():
        return []
    return [tuple(map(int, row)) for row in csv.reader(path.read_text().splitlines()) if len(row) == 2]


def bridge(i):
    start(f"route{i}", [binary("agnocast_rmw_loan_probe", "route_bridge"), "a2r",
                        "cloud", "/input", "/mirror"])


def checkpoint(seq, expected):
    wait(lambda: len(sink()) > seq, f"crop frame {seq}")
    log = (out / "writer.log").read_text()
    routes = re.search(rf"^routes seq={seq} input_be=(\d+) input_rel=(\d+) output_be=(\d+)$", log, re.M)
    assert routes and tuple(map(int, routes.groups())) == (expected, expected, 1), (seq, routes)
    sent = [int(s) for s in re.findall(r"^sent \d+ (\d+)$", log, re.M)]
    freed = set(map(int, re.findall(r"host_payload_reclaimed=(\d+)", log)))
    live = [i for i, stamp in enumerate(sent) if stamp not in freed]
    assert live == list(range(len(sent)-len(live), len(sent))) and 4 <= len(live) <= 6, (seq, live)
    print(f"checkpoint seq={seq} input_readers={expected}/{expected} output_writers=1 live={len(live)}", flush=True)


def read_bag():
    template = bytearray().join(struct.pack("<fffBBH", 2. if i % 2 else .5, (i%4)/8, (i%8)/16,
                                           0, 1, i) for i in range(32768))
    db, = (out / "bag").glob("*.db3")
    rows = []
    with sqlite3.connect(f"file:{db}?mode=ro", uri=True) as conn:
        for name, received, data in conn.execute(
                "SELECT name,timestamp,data FROM messages JOIN topics ON topics.id=topic_id ORDER BY messages.id"):
            msg = deserialize_message(data, PointCloud2)
            payload = bytes(msg.data)
            seq = payload[12]
            template[12::16] = bytes([seq])*32768
            assert name == "/mirror" and payload == template
            assert (msg.header.frame_id, msg.width, msg.height, msg.point_step, msg.row_step) == (
                "base_link", 32768, 1, 16, 524288)
            assert stamps[seq] == msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec
            rows.append((seq, received))
    with (out / "bag-frames.csv").open("w") as data:
        csv.writer(data, lineterminator="\n").writerows(rows)
    return rows


try:
    rclpy.init()
    graph = rclpy.create_node("restart_driver")
    endpoint = binary("universe_crop_benchmark", "endpoint")
    config = Path("src/universe/autoware_universe/sensing/autoware_pointcloud_preprocessor/"
                  "config/crop_box_filter_node.param.yaml")
    start("crop", [binary("universe_crop_benchmark", "crop"), "--ros-args", "--params-file", config,
                   "-p", "input_frame:=base_link", "-p", "output_frame:=base_link"], AGNOCAST_RMW_DIRECT_WAIT="1")
    start("sink", [endpoint], BENCH_ROLE="sub", BENCH_CSV=str(out / "crop.csv"))
    start("writer", [endpoint, "--ros-args", "-p", "period_ms:=50"], BENCH_ROLE="pub",
          BENCH_NATIVE_COUNTS="1", BENCH_TRACE_STAMPS="1", AGNOCAST_RMW_AUDIT_RELEASE="1")
    bridge(0)
    start("recorder", ["ros2", "bag", "record", "--topics", "/mirror", "-s", "sqlite3",
                       "--max-cache-size", "0", "--disable-keyboard-controls", "-o", str(out / "bag")],
          base=os.environ)
    wait(lambda: graph.count_subscribers("/input") == 2 and graph.count_subscribers("/output") == 1
         and graph.count_publishers("/output") == 1 and graph.count_subscribers("/mirror") == 1,
         "initial matches")
    time.sleep(.4)
    limit.write_text(str(count))
    events = []
    for i, seq in enumerate((35, 95)):
        checkpoint(seq, 2)
        killed = time.time_ns()
        if mode == "restart":
            stop(f"route{i}", signal.SIGKILL)
        checkpoint(seq+12, 1 if mode == "restart" else 2)
        restarted = time.time_ns()
        if mode == "restart":
            bridge(i+1)
        events.append((killed, restarted))
        checkpoint(seq+34, 2)
    wait(lambda: len(sink()) == count, "final crop")
    time.sleep(.4)
    stop("recorder")
    stamps = dict((int(i), int(s)) for i, s in re.findall(r"^sent (\d+) (\d+)$",
                  (out/"writer.log").read_text(), re.M))
    assert list(stamps) == list(range(count))
    deliveries = sink()
    assert [i for i, _ in deliveries] == list(range(count))
    arrivals = [stamps[i]+lat for i, lat in deliveries]
    print(f"local={count}/{count} largest_callback_interval_ms={max(b-a for a,b in zip(arrivals,arrivals[1:]))/1e6:.3f}")
    rows = read_bag()
    ids = [i for i,_ in rows]
    assert ids == sorted(set(ids)) and ids[0] == 0 and ids[-1] == count-1
    gaps = [(a,b) for a,b in zip(ids,ids[1:]) if b != a+1]
    print(f"bag={len(ids)}/{count} gaps={gaps} (last_good,first_recovered)")
    if mode == "restart":
        assert len(gaps) == len(events)
        for (a,b), (killed,restarted) in zip(gaps, events):
            receive = dict(rows)[b]
            assert stamps[a] <= killed and stamps[b] >= restarted and receive >= restarted
            print(f"before={a} after={b} missing={b-a-1} kill_ns={killed} restart_ns={restarted} "
                  f"first_bag_ns={receive} restart_to_record_ms={(receive-restarted)/1e6:.3f}")
    else:
        assert not gaps
    for name in list(children):
        stop(name)
finally:
    for child in children.values():
        if child.poll() is None:
            child.kill()
        child.wait()
    if rclpy.ok():
        rclpy.shutdown()
