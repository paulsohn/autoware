#!/usr/bin/env python3
"""Measure the original Universe crop-box with a single transport on both edges."""
import argparse
import csv
import os
from pathlib import Path
import resource
import signal
import subprocess
import tempfile
import time

from ament_index_python.packages import get_package_prefix

parser = argparse.ArgumentParser()
parser.add_argument("transport", choices=["cyclone", "agnocast", "zenoh", "rmw-agnocast"])
parser.add_argument("csv", type=Path)
parser.add_argument("--resources", type=Path)
parser.add_argument("--buffer", choices=["cpu", "shmem", "cuda"], default="cpu")
parser.add_argument("--ros-cie", action="store_true", help="use the mixed-capable CIE for the ROS crop")
args = parser.parse_args()
if args.ros_cie and args.transport == "agnocast":
    parser.error("native Agnocast already uses its Agnocast-only CIE")
if args.buffer != "cpu" and args.transport != "zenoh":
    parser.error("the explicit buffer matrix uses the Zenoh descriptor transport")
out = args.csv.resolve()
if out.exists():
    raise FileExistsError(out)
bin_dir = Path(get_package_prefix("universe_crop_benchmark")) / "lib/universe_crop_benchmark"
rmw = "rmw_zenoh_cpp" if args.transport == "zenoh" else "rmw_cyclonedds_cpp"
if args.transport == "rmw-agnocast":
    rmw = "rmw_agnocast"
env = dict(os.environ, RMW_IMPLEMENTATION=rmw, BENCH_BUFFER=args.buffer,
           ENABLE_AGNOCAST=str(int(args.transport == "agnocast")), AGNOCAST_BRIDGE_MODE="OFF",
           AGNOCAST_NO_DISCOVERY_AGENT="1")
if args.transport == "rmw-agnocast":
    env.update(AGNOCAST_RMW_TOPIC="/input:/output", AGNOCAST_RMW_BUFFER_CDR="1")
if args.transport in ("agnocast", "rmw-agnocast"):
    for prefix in (get_package_prefix("agnocastlib"), get_package_prefix("rclcpp")):
        hook = Path(prefix) / "lib/libagnocast_heaphook.so"
        if hook.is_file():
            env["LD_PRELOAD"] = str(hook)
            break
    else:
        raise FileNotFoundError("libagnocast_heaphook.so in Agnocast or ROS install prefix")
config = (Path(__file__).resolve().parents[1] / "universe/autoware_universe/sensing/"
          "autoware_pointcloud_preprocessor/config/crop_box_filter_node.param.yaml")
children = []
with tempfile.TemporaryDirectory() as tmp:
    samples = Path(tmp) / "samples.csv"
    def launch(argv, role):
        proc = subprocess.Popen(argv, env=dict(env, BENCH_ROLE=role, BENCH_CSV=str(samples)))
        children.append(proc)

    try:
        if args.transport == "zenoh":
            launch([Path(get_package_prefix(rmw)) / "lib/rmw_zenoh_cpp/rmw_zenohd"], "router")
            time.sleep(0.6)
            if children[-1].poll() is not None:
                raise RuntimeError("Zenoh router startup failed")
        launch([bin_dir / "endpoint"], "sub")
        launch([bin_dir / ("crop_cie" if args.ros_cie else "crop"), "--ros-args", "--params-file", config,
                "-p", "input_frame:=base_link", "-p", "output_frame:=base_link"], "filter")
        time.sleep(2)
        launch([bin_dir / "endpoint"], "pub")
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            for child in children:
                if child.poll() is not None:
                    raise RuntimeError(f"{child.args} exited {child.returncode}")
            if samples.exists():
                with samples.open() as f:
                    rows = list(csv.reader(f))
                if len(rows) == 60 and [int(r[0]) for r in rows] == list(range(60)):
                    break
            time.sleep(0.1)
        else:
            raise TimeoutError("missing one or more verified frames")
    finally:
        for child in reversed(children):
            if child.poll() is None:
                child.send_signal(signal.SIGINT)
        for child in children:
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()

    for child in children:
        if child.returncode not in (0, -signal.SIGINT):
            raise RuntimeError(f"{child.args} exited {child.returncode} during shutdown")
    with out.open("xb") as f:
        f.write(samples.read_bytes())

if args.resources:
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    with args.resources.open("x") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(("verified_frames", "child_user_s", "child_system_s", "largest_child_maxrss_kib"))
        writer.writerow((60, usage.ru_utime, usage.ru_stime, usage.ru_maxrss))
