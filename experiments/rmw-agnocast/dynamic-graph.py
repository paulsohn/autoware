"""Run one dynamic graph in a guarded Lyrical overlay with Cyclone or Zenoh control services."""
import csv
import os
import re
import signal
import subprocess
import sys
import time
from pathlib import Path

import rclpy
from ament_index_python.packages import get_package_prefix
from composition_interfaces.srv import ListNodes, LoadNode, UnloadNode
from rclpy.parameter import Parameter

mode, target = sys.argv[1:]
assert mode in ("stock", "direct", "scoped", "cyclone", "zenoh")
out = Path(target).resolve()
out.mkdir()
limit_path = out / "limit"
env = dict(os.environ, BENCH_LIFECYCLE_LIMIT=str(limit_path), ENABLE_AGNOCAST="0")
if mode in ("stock", "direct", "scoped"):
    env.update(RMW_IMPLEMENTATION="rmw_agnocast", AGNOCAST_RMW_TOPIC="/input:/output",
               AGNOCAST_RMW_HOST_CLOUD="1", AGNOCAST_BRIDGE_MODE="OFF", AGNOCAST_NO_DISCOVERY_AGENT="1",
               LD_PRELOAD=str(Path(get_package_prefix("agnocastlib")) / "lib/libagnocast_heaphook.so"))


def binary(pkg, exe):
    return Path(get_package_prefix(pkg)) / "lib" / pkg / exe


children = {}
def start(name, args, **extra):
    with (out / (name + ".log")).open("x") as log:
        children[name] = subprocess.Popen(args, env=dict(env, **extra), stdout=log, stderr=log)


def healthy():
    for name, child in children.items():
        if child.poll() is not None:
            raise RuntimeError(f"{name} exited {child.returncode}")


def wait(test, what, timeout=8):
    end = time.monotonic() + timeout
    while not test():
        healthy()
        if time.monotonic() >= end:
            raise TimeoutError(what)
        time.sleep(.01)


def limit(count):
    assert 0 <= count <= 250   # Sequence is carried in the UINT8 intensity field.
    tmp = limit_path.with_suffix(".tmp")
    tmp.write_text(str(count))
    tmp.replace(limit_path)


def ids(name):
    path = out / (name + ".csv")
    if not path.exists():
        return []
    with path.open() as data:
        return [int(row[0]) for row in csv.reader(data) if len(row) == 2]


def last(name):
    seqs = ids(name)
    return seqs[-1] if seqs else -1


def call(client, request):
    future = client.call_async(request)
    rclpy.spin_until_future_complete(node, future, timeout_sec=5)
    if not future.done():
        raise TimeoutError(client.srv_name)
    result = future.result()
    if hasattr(result, "success") and not result.success:
        raise RuntimeError(result.error_message)
    return result


def sleeping(stage):
    # Sample the main executor thread separately from DDS/native receive threads.
    proc = Path(f"/proc/{children['container'].pid}/task/{children['container'].pid}")
    for _ in range(3):
        wait(lambda: (proc / "stat").read_text().split(") ")[1][0] == "S", stage)
        time.sleep(.025)
    print(f"{stage} main_thread_wait={ (proc / 'wchan').read_text().strip() }", flush=True)


def stop(name):
    child = children[name]
    t = time.monotonic()
    child.send_signal(signal.SIGINT)
    status = child.wait(timeout=5)
    assert status == 0, (name, status)
    del children[name]
    return (time.monotonic() - t) * 1000


endpoint = binary("universe_crop_benchmark", "endpoint")
limit(0)
try:
    if mode == "zenoh":
        start("router", [binary("rmw_zenoh_cpp", "rmw_zenohd")])
        time.sleep(.5)
    rclpy.init()
    node = rclpy.create_node("lifecycle_driver")
    if mode == "scoped":
        start("container", [binary("agnocast_rmw_loan_probe", "lifecycle_container")],
              AGNOCAST_RMW_DIRECT_WAIT="1", AGNOCAST_FIXED_PROBE="1")
    else:
        options = {"AGNOCAST_RMW_DIRECT_WAIT": "1"} if mode == "direct" else {}
        start("container", [binary("rclcpp_components", "component_container"),
                            "--ros-args", "-r", "__node:=lifecycle"], **options)
    start("raw", [endpoint, "--ros-args", "-r", "output:=/input", "-r", "__node:=raw_witness"],
          BENCH_ROLE="sub", BENCH_RAW_READER="1", BENCH_CSV=str(out / "raw.csv"))
    start("crop", [endpoint], BENCH_ROLE="sub", BENCH_CSV=str(out / "crop.csv"))
    native_counts = {"BENCH_NATIVE_COUNTS": "1"} if mode in ("stock", "direct", "scoped") else {}
    start("writer", [endpoint, "--ros-args", "-p", "period_ms:=50"], BENCH_ROLE="pub", **native_counts)
    base = "/lifecycle/_container/"
    load = node.create_client(LoadNode, base + "load_node")
    unload = node.create_client(UnloadNode, base + "unload_node")
    listing = node.create_client(ListNodes, base + "list_nodes")
    for client in (load, unload, listing):
        wait(client.service_is_ready, client.srv_name)
    wait(lambda: node.count_subscribers("/input") == 1 and node.count_subscribers("/output") == 1,
         "initial graph")
    req = LoadNode.Request()
    req.package_name = "universe_crop_benchmark"
    req.plugin_name = "autoware::pointcloud_preprocessor::CropBoxFilterComponent"
    req.node_name = "dynamic_crop"
    params = dict(input_frame="base_link", output_frame="base_link", min_x=-1., max_x=1.,
                  min_y=-1., max_y=1., min_z=-1., max_z=1., negative=False,
                  processing_time_threshold_sec=.01)
    req.parameters = [Parameter(k, value=v).to_parameter_msg() for k, v in params.items()]
    sleeping("idle_load")
    previous_id = 0
    loaded_ranges = []
    absent_ranges = []
    for generation in range(3):
        result = call(load, req)
        assert result.unique_id > previous_id and result.full_node_name == "/dynamic_crop"
        previous_id = result.unique_id
        wait(lambda: node.count_subscribers("/input") == 2 and node.count_publishers("/output") == 1,
             "loaded graph")
        if generation == 0:
            time.sleep(.25)
            sleeping("idle_first_cloud")
            limit(1)
            wait(lambda: ids("raw") == [0] and ids("crop") == [0], "single cloud after idle load")
            time.sleep(.2)
            assert ids("raw") == ids("crop") == [0]
        # Other generations join the active publisher. Check a contiguous interval after matching.
        limit(250)
        first = last("raw") + 2
        wait(lambda: last("crop") >= first + 3, "warm crop")
        assert list(call(listing, ListNodes.Request()).unique_ids) == [previous_id]
        wait(lambda: last("crop") >= first + 16, "steady deliveries")
        loaded_ranges.append((first, last("crop")))
        if generation < 2:
            gone = UnloadNode.Request()
            gone.unique_id = previous_id
            call(unload, gone)
            assert not call(listing, ListNodes.Request()).unique_ids
            wait(lambda: node.count_subscribers("/input") == 1 and node.count_publishers("/output") == 0,
                 "unloaded graph")
            after = last("raw")
            wait(lambda: last("raw") >= after + 7, "unloaded traffic")
            assert all(seq <= after + 1 for seq in ids("crop")), "crop published after unload"
            absent_ranges.append((after+2, last("raw")))
            print(f"unloaded id={previous_id} raw_continued={after+2}..{last('raw')}", flush=True)
    # Freeze the writer on a known sequence before signalling the loaded, idle container.
    count = last("raw") + 5
    limit(count)
    wait(lambda: last("raw") == last("crop") == count-1, "last cloud")
    time.sleep(.25)
    sleeping("idle_shutdown")
    elapsed = stop("container")
    wait(lambda: node.count_subscribers("/input") == 1 and node.count_publishers("/output") == 0,
         "shutdown graph cleanup")
    before = ids("crop")
    limit(count+5)
    wait(lambda: last("raw") == count+4, "post-shutdown witness")
    absent_ranges.append((count, count+4))
    count += 5
    received = ids("crop")
    assert before == received
    assert ids("raw") == list(range(count))
    assert received == sorted(set(received)) and all(0 <= seq < count for seq in received)
    assert all(set(range(a,b+1)).issubset(received) for a,b in loaded_ranges)
    for name in ("writer", "crop", "raw"):
        stop(name)
    if native_counts:
        routes = {int(seq): (int(be), int(rel), int(pub)) for seq, be, rel, pub in
                  re.findall(r"routes seq=(\d+) input_be=(\d+) input_rel=(\d+) output_be=(\d+)",
                             (out / "writer.log").read_text())}
        assert len(routes) == count
        for intervals, expected in ((loaded_ranges, (2, 2, 1)), (absent_ranges, (1, 1, 0))):
            for a, b in intervals:
                assert all(routes[i] == expected for i in range(a,b+1)), (a, b, expected, routes)
        print("native_routes loaded=2/2/1 absent=1/1/0 (input BE readers/reliable readers/output BE writers)")
    if mode == "scoped":
        stats = re.findall(r"fixed_wait arms=(\d+) completions=(\d+) native=(\d+).*stop_interrupts=(\d+)",
                           (out / "container.log").read_text())
        assert len(stats) == 3 and all(int(wakes) >= 8 and stops == "1" for _,_,wakes,stops in stats), stats
        print("fixed_scopes arms/completions/native/stops", stats)
    print(f"mode={mode} raw={count}/{count} crop={len(received)} single_first=0 "
          f"steady_ranges={loaded_ranges} shutdown_ms={elapsed:.1f}", flush=True)
finally:
    for child in children.values():
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
    if rclpy.ok():
        rclpy.shutdown()
