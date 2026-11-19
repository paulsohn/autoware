#!/usr/bin/env python3
"""Measure the original Universe crop-box with a single transport on both edges."""
import argparse
import csv
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

from ament_index_python.packages import get_package_prefix

parser = argparse.ArgumentParser()
parser.add_argument("transport", choices=["cyclone", "agnocast"])
parser.add_argument("csv", type=Path)
args = parser.parse_args()
out = args.csv.resolve()
if out.exists():
    raise FileExistsError(out)
bin_dir = Path(get_package_prefix("universe_crop_benchmark")) / "lib/universe_crop_benchmark"
env = dict(os.environ, RMW_IMPLEMENTATION="rmw_cyclonedds_cpp",
           ENABLE_AGNOCAST=str(int(args.transport == "agnocast")), AGNOCAST_BRIDGE_MODE="OFF",
           AGNOCAST_NO_DISCOVERY_AGENT="1")
if args.transport == "agnocast":
    env["LD_PRELOAD"] = str(Path(get_package_prefix("rclcpp")) / "lib/libagnocast_heaphook.so")
config = (Path(__file__).resolve().parents[1] / "universe/autoware_universe/sensing/"
          "autoware_pointcloud_preprocessor/config/crop_box_filter_node.param.yaml")
children = []
with tempfile.TemporaryDirectory() as tmp:
    samples = Path(tmp) / "samples.csv"
    def launch(argv, role):
        proc = subprocess.Popen(argv, env=dict(env, BENCH_ROLE=role, BENCH_CSV=str(samples)))
        children.append(proc)

    try:
        launch([bin_dir / "endpoint"], "sub")
        launch([bin_dir / "crop", "--ros-args", "--params-file", config,
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
                    out.write_bytes(samples.read_bytes())
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
