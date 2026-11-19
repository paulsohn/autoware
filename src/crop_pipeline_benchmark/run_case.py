#!/usr/bin/env python3
"""Run one producer -> Autoware crop box -> verifier pipeline in separate processes."""
import argparse
import csv
import os
from pathlib import Path
import subprocess
import time

from ament_index_python.packages import get_package_prefix

parser = argparse.ArgumentParser()
parser.add_argument("rmw", choices=["rmw_zenoh_cpp", "rmw_cyclonedds_cpp"])
parser.add_argument("backend", choices=["cpu", "cuda", "shmem"])
parser.add_argument("csv", type=Path)
parser.add_argument("--two-edge", action="store_true", help="require the input backend at the sink")
parser.add_argument("--profile", type=Path, help="sample Linux CPU time and resident high water")
args = parser.parse_args()
os.environ.update(RMW_IMPLEMENTATION=args.rmw, RUST_LOG="error")
directory = Path(get_package_prefix("crop_pipeline_benchmark")) / "lib/crop_pipeline_benchmark"
children = []

def launch(argv, **kwargs):
    child = subprocess.Popen(argv, **kwargs)
    children.append(child)
    return child

def profile(processes):
    samples = {}
    ticks = os.sysconf("SC_CLK_TCK")
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        for role, proc in processes.items():
            if proc.poll() is not None:
                continue
            root = Path("/proc") / str(proc.pid)
            try:
                fields = (root / "stat").read_text().rsplit(")", 1)[1].split()
                status = (root / "status").read_text().splitlines()
            except FileNotFoundError:
                continue
            for line in status:
                if line.startswith("VmHWM:"):
                    samples[role] = (proc.pid, int(fields[11]) / ticks,
                                     int(fields[12]) / ticks, int(line.split()[1]))
        if processes["publisher"].poll() is not None and processes["sink"].poll() is not None:
            break
        time.sleep(0.05)
    if samples.keys() != processes.keys():
        raise RuntimeError("missing process resource samples")
    return samples


with args.csv.open("x") as data:
    try:
        if args.rmw == "rmw_zenoh_cpp":
            router = launch([Path(get_package_prefix(args.rmw)) / "lib/rmw_zenoh_cpp/rmw_zenohd"])
            time.sleep(0.6)
            if router.poll() is not None:
                raise RuntimeError("Zenoh router startup failed")
        options = []
        for axis in "xyz":
            options.extend(["-p", f"min_{axis}:=-1.0", "-p", f"max_{axis}:=1.0"])
        crop = launch([directory / "crop", "--ros-args", *options, "-p", "negative:=false",
                       "-p", f"input_backend:={args.backend}"])
        sink = launch([directory / "endpoint", "sub", args.backend if args.two_edge else "cpu"], stdout=data)
        source = launch([directory / "endpoint", "pub", args.backend])
        if args.profile:
            processes = {"publisher": source, "filter": crop, "sink": sink}
            if args.rmw == "rmw_zenoh_cpp":
                processes["router"] = router
            samples = profile(processes)
        for proc in [source, sink]:
            if proc.wait(timeout=45):
                raise RuntimeError(f"{proc.args} exited {proc.returncode}")
        if crop.poll() is not None:
            raise RuntimeError(f"crop exited {crop.returncode}")
        if args.profile:
            with args.profile.open("x") as output:
                writer = csv.writer(output, lineterminator="\n")
                writer.writerow(["role", "pid", "user_s", "system_s", "peak_rss_kib"])
                writer.writerows((role, *values) for role, values in samples.items())
    finally:
        for proc in reversed(children):
            if proc.poll() is None:
                proc.terminate()
        for proc in children:
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
