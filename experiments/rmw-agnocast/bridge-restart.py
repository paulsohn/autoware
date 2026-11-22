"""Exercise outbound and inbound PointCloud2 route restart with a persistent Universe crop."""
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

mode, target = sys.argv[1:3]
ingress = sys.argv[3:] == ["--inbound"]
assert ingress or len(sys.argv) == 3
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


def inbound():
    import ctypes

    ioctl = ctypes.CDLL("libagnocast_ioctl_wrapper.so")
    query = ioctl.get_agnocast_pub_nodes
    query.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_int), ctypes.c_uint32]
    query.restype = ctypes.c_void_p
    ioctl.free_agnocast_topic_info_ret.argtypes = [ctypes.c_void_p]
    ioctl.free_agnocast_topic_info_ret.restype = None

    def writers():
        n = ctypes.c_int()
        wire = b"/input/_rmw_agnocast_v2/sensor_msgs/msg/PointCloud2/host_cloud/best_effort"
        ptr = query(wire, ctypes.byref(n), int(os.environ["ROS_DOMAIN_ID"]))
        ioctl.free_agnocast_topic_info_ret(ptr)
        return n.value

    def cyclone_readers():
        return {bytes(info.endpoint_gid) for info in graph.get_subscriptions_info_by_topic("/mirror")
                if info.node_name == "agnocast_route_bridge"}

    def source_gids():
        return {bytes(info.endpoint_gid) for info in graph.get_publishers_info_by_topic("/mirror")}

    def launch_route(index):
        start(f"route{index}", [binary("agnocast_rmw_loan_probe", "route_bridge"), "r2a",
                                "cloud", "/mirror", "/input"])

    def frames(file):
        path = out / file
        return [int(row[0]) for row in csv.reader(path.read_text().splitlines()) if len(row) == 2]

    def allow(n):
        tmp = limit.with_suffix(".tmp")
        tmp.write_text(str(n))
        tmp.replace(limit)

    def blocked():
        proc = Path(f"/proc/{children['crop'].pid}")
        for _ in range(3):
            wait(lambda: (proc/"stat").read_text().split(") ")[1][0] == "S" and
                 "poll" in (proc/"wchan").read_text(), "crop blocked in direct wait")
            time.sleep(.025)

    dds = dict(os.environ, BENCH_LIFECYCLE_LIMIT=str(limit), ENABLE_AGNOCAST="0")
    start("witness", [endpoint, "--ros-args", "-r", "output:=/mirror"], base=dds,
          BENCH_ROLE="sub", BENCH_RAW_READER="1", BENCH_CSV=str(out/"witness.csv"))
    start("writer", [endpoint, "--ros-args", "-r", "input:=/mirror", "-p", "period_ms:=50"],
          base=dds, BENCH_ROLE="pub", BENCH_TRACE_STAMPS="1")
    launch_route(0)
    wait(lambda: writers() == 1 and cyclone_readers() and graph.count_subscribers("/mirror") == 2
         and graph.count_subscribers("/input") == 1 and graph.count_publishers("/output") == 1
         and graph.count_subscribers("/output") == 1, "initial route")
    source = source_gids()
    assert len(source) == 1
    time.sleep(.4)
    allow(80)
    wait(lambda: 30 in frames("crop.csv"), "continuous baseline")
    if mode == "restart":
        stop("route0", signal.SIGKILL)
        wait(lambda: writers() == 0, "old native writer removal")
    wait(lambda: 42 in frames("witness.csv"), "DDS witness during first interval")
    restart_ns = time.time_ns()
    if mode == "restart":
        launch_route(1)
    wait(lambda: writers() == 1 and 79 in frames("crop.csv"), "end of continuous stream")
    steady = frames("crop.csv")
    gaps = [(a, b) for a, b in zip(steady, steady[1:]) if b != a+1]
    assert steady[0] == 0 and steady == sorted(set(steady))
    if mode == "restart":
        assert len(gaps) == 1 and gaps[0][0] >= 30 and gaps[0][1] > 42, gaps
    else:
        assert not gaps
    assert frames("witness.csv") == list(range(80))
    before = cyclone_readers()
    if mode == "restart":
        stop("route1", signal.SIGKILL)
        wait(lambda: writers() == 0, "idle writer removal")
        launch_route(2)
        wait(lambda: writers() == 1 and cyclone_readers() - before, "replacement on both sides")
    blocked()
    print("idle crop wait channel:", (Path(f"/proc/{children['crop'].pid}")/"wchan").read_text().strip())
    assert source_gids() == source
    assert frames("crop.csv") == steady
    allow(81)
    wait(lambda: frames("crop.csv") == steady + [80] and frames("witness.csv") == list(range(81)),
         "isolated first cloud")
    time.sleep(.25)
    assert frames("crop.csv") == steady + [80]
    allow(100)
    wait(lambda: 99 in frames("crop.csv") and 99 in frames("witness.csv"), "continued traffic")
    assert source_gids() == source and writers() == 1
    print("source graph GID:", next(iter(source)).hex())
    for name in list(children):
        stop(name)
    assert frames("witness.csv") == list(range(100))
    assert frames("crop.csv") == steady + list(range(80,100))
    stamps = {int(i): int(s) for i,s in re.findall(r"^sent (\d+) (\d+)$", (out/"writer.log").read_text(), re.M)}
    assert list(stamps) == list(range(100))
    latencies = dict(sink())
    gids = []
    for path in sorted(out.glob("route*.log")):
        ingress, egress = re.search(r"ingress_gid=(\S+) egress_gid=(\S+)", path.read_text()).groups()
        gids.append((ingress, egress))
    assert len({egress for _, egress in gids}) == len(gids), gids
    if gaps:
        a, b = gaps[0]
        assert stamps[b] >= restart_ns
        print(f"last_good={a} first_recovered={b} missing={b-a-1} restart_ns={restart_ns} "
              f"restart_to_crop_sink_ms={(stamps[b]+latencies[b]-restart_ns)/1e6:.3f}")
    print(f"mode={mode} witness=100/100 crop={len(latencies)}/100 gaps={gaps} "
          f"isolated_seq=80 source_to_sink_ms={latencies[80]/1e6:.3f} "
          f"distinct_bridge_egress_gids={len(gids)} native_counts={'1/0/1/0/1' if gaps else '1'}")


try:
    rclpy.init()
    graph = rclpy.create_node("restart_driver")
    endpoint = binary("universe_crop_benchmark", "endpoint")
    config = Path("src/universe/autoware_universe/sensing/autoware_pointcloud_preprocessor/"
                  "config/crop_box_filter_node.param.yaml")
    start("crop", [binary("universe_crop_benchmark", "crop"), "--ros-args", "--params-file", config,
                   "-p", "input_frame:=base_link", "-p", "output_frame:=base_link"], AGNOCAST_RMW_DIRECT_WAIT="1")
    start("sink", [endpoint], BENCH_ROLE="sub", BENCH_CSV=str(out / "crop.csv"))
    if ingress:
        inbound()
        sys.exit(0)
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
