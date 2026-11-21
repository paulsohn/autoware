#!/usr/bin/env python3
"""Measure the original Universe crop-box with a single transport on both edges."""
import argparse
import csv
import os
from pathlib import Path
import re
import resource
import signal
import subprocess
import tempfile
import time

from ament_index_python.packages import get_package_prefix

parser = argparse.ArgumentParser()
parser.add_argument("transport", choices=["cyclone", "agnocast", "zenoh", "rmw-agnocast", "rmw-delegated", "mixed", "fixed"])
parser.add_argument("csv", type=Path)
parser.add_argument("--typed-input", action="store_true")
parser.add_argument("--payload-scope", action="store_true", help="allocate shared point bytes before borrowing at publication")
parser.add_argument("--host-loan", action="store_true", help="transfer both outputs through publisher loans")
parser.add_argument("--host-view", action="store_true", help="read host-cloud payloads through a retained loan")
parser.add_argument("--host-cloud", action="store_true", help="use the same-host CPU PointCloud2 representation")
parser.add_argument("--bridge", choices=["idle", "active"], help="mirror the native crop output to Cyclone DDS")
parser.add_argument("--branched", action="store_true", help="tap the first crop and validate a second crop downstream")
parser.add_argument("--period-ms", type=int, default=100, help="requested sensor publication period in milliseconds")
parser.add_argument("--gaps", choices=["short-first", "long-first", "steady"])
parser.add_argument("--gap-profile", action="store_true")
parser.add_argument("--resources", type=Path)
parser.add_argument("--buffer", choices=["cpu", "shmem", "cuda"], default="cpu")
parser.add_argument("--ros-cie", action="store_true", help="use the mixed-capable CIE for the ROS crop")
args = parser.parse_args()
if args.ros_cie and args.transport == "agnocast":
    parser.error("native Agnocast already uses its Agnocast-only CIE")
if args.buffer != "cpu" and args.transport != "zenoh":
    parser.error("the explicit buffer matrix uses the Zenoh descriptor transport")
if args.gap_profile and not args.gaps:
    parser.error("--gap-profile requires --gaps")
if args.period_ms <= 0:
    parser.error("--period-ms must be positive")
if args.branched and args.bridge:
    parser.error("the branched experiment measures local paths only")
if args.bridge and not args.payload_scope:
    parser.error("--bridge requires --payload-scope")
if args.payload_scope:
    args.host_loan = True
if args.host_loan:
    if args.transport != "fixed":
        parser.error("--host-loan uses the fixed wait control")
    args.host_view = True
if args.host_view:
    args.host_cloud = True
if args.host_cloud and args.transport not in ("mixed", "fixed"):
    parser.error("--host-cloud requires a mixed or fixed RMW reader")
if args.ros_cie and args.transport in ("mixed", "fixed"):
    parser.error("mixed and fixed use the standard single-threaded executor")
out = args.csv.resolve()
if out.exists():
    raise FileExistsError(out)
bin_dir = Path(get_package_prefix("universe_crop_benchmark")) / "lib/universe_crop_benchmark"
rmw = "rmw_zenoh_cpp" if args.transport == "zenoh" else "rmw_cyclonedds_cpp"
if args.transport in ("rmw-agnocast", "rmw-delegated", "mixed", "fixed"):
    rmw = "rmw_agnocast"
env = dict(os.environ, RMW_IMPLEMENTATION=rmw, BENCH_BUFFER=args.buffer,
           ENABLE_AGNOCAST=str(int(args.transport == "agnocast")), AGNOCAST_BRIDGE_MODE="OFF",
           AGNOCAST_NO_DISCOVERY_AGENT="1")
if args.transport in ("rmw-agnocast", "mixed", "fixed"):
    env.update(AGNOCAST_RMW_TOPIC="/input:/output:/stage1" if args.branched else "/input:/output", AGNOCAST_RMW_BUFFER_CDR="1")
if args.transport == "rmw-delegated":
    env.update(AGNOCAST_RMW_TOPIC="", AGNOCAST_RMW_BUFFER_CDR="1")
if args.transport in ("agnocast", "rmw-agnocast", "rmw-delegated", "mixed", "fixed"):
    for prefix in (get_package_prefix("agnocastlib"), get_package_prefix("rclcpp")):
        hook = Path(prefix) / "lib/libagnocast_heaphook.so"
        if hook.is_file():
            env["LD_PRELOAD"] = str(hook)
            break
    else:
        raise FileNotFoundError("libagnocast_heaphook.so in Agnocast or ROS install prefix")
config = (Path(__file__).resolve().parents[1] / "universe/autoware_universe/sensing/"
          "autoware_pointcloud_preprocessor/config/crop_box_filter_node.param.yaml")
for key in ("BENCH_PAYLOAD_SCOPE", "AGNOCAST_RMW_HOST_LOAN", "BENCH_TYPED_INPUT", "AGNOCAST_RMW_HOST_VIEW", "AGNOCAST_VIEW_ADDRESS_FILE", "AGNOCAST_RMW_HOST_CLOUD", "AGNOCAST_RMW_DIRECT_WAIT", "AGNOCAST_RMW_PROBE_NATIVE_ONLY", "AGNOCAST_FIXED_PROBE"):
    env.pop(key, None)
if args.host_cloud:
    env.pop("AGNOCAST_RMW_BUFFER_CDR")
    env["AGNOCAST_RMW_HOST_CLOUD"] = "1"
if args.transport in ("mixed", "fixed"):
    env["AGNOCAST_RMW_DIRECT_WAIT"] = "1"
    runner = Path(get_package_prefix("agnocast_rmw_loan_probe")) / "lib/agnocast_rmw_loan_probe/crop_wait_runner"
children = []
with tempfile.TemporaryDirectory() as tmp:
    samples = Path(tmp) / "samples.csv"
    mirror = Path(tmp) / "mirror.csv"
    tap = Path(tmp) / "tap.csv"
    def launch(argv, role, label=None):
        label = label or role
        child_env = dict(env, BENCH_ROLE=role, BENCH_CSV=str(samples))
        if args.gaps and role == "pub":
            child_env["BENCH_GAP_STAMPS"] = str(out.with_suffix(".stamps"))
            if args.gaps == "steady":
                child_env["BENCH_STEADY_GAPS"] = "1"
            if args.gaps == "long-first":
                child_env["BENCH_LONG_FIRST"] = "1"
        if args.gap_profile and args.transport == "fixed" and os.getenv("BENCH_HOP_TRACE") == "1":
            if role == "pub":
                child_env["BENCH_HOP_WRITER"] = str(out.with_suffix(".writer"))
            if label == "filter":
                child_env["BENCH_HOP_READER"] = str(out.with_suffix(".reader"))
                if os.getenv("BENCH_RECEIVE_TRACE") == "1":
                    trace_lib = Path("build/rmw-agnocast/path-trace-lib").resolve()
                    child_env["LD_LIBRARY_PATH"] = str(trace_lib) + ":" + child_env["LD_LIBRARY_PATH"]
                    child_env["BENCH_RECEIVE_EVENTS"] = str(out.with_suffix(".receive"))
                    probe = "receive_executor_trace" if os.getenv("BENCH_EXECUTOR_SPLIT") == "1" else "receive_rcl_trace"
                    rcl_trace = Path(get_package_prefix("agnocast_rmw_loan_probe")) / "lib" / f"lib{probe}.so"
                    if not rcl_trace.is_file() or not (trace_lib / "libagnocast.so").is_file():
                        raise FileNotFoundError("Build the Agnocast receive probe and RCL interposer first")
                    child_env["LD_PRELOAD"] += ":" + str(rcl_trace)
                    child_env["BENCH_RCL_EVENTS"] = str(out.with_suffix(".rcl"))
        if args.gap_profile and label == "filter":
            child_env["BENCH_CROP_PROFILE"] = str(out.with_suffix(".profile"))
        if label == "tap":
            child_env["BENCH_CSV"] = str(tap)
        if (args.typed_input or args.host_view) and role in ("sub", "filter"):
            child_env["BENCH_TYPED_INPUT"] = "1"
        if args.host_loan and role in ("pub", "filter"):
            child_env["AGNOCAST_RMW_HOST_LOAN"] = "1"
            if args.payload_scope:
                child_env["BENCH_PAYLOAD_SCOPE"] = "1"
        if args.host_view and role in ("sub", "filter"):
            child_env["AGNOCAST_RMW_HOST_VIEW"] = "1"
        if role in ("sub", "filter") and args.transport in ("mixed", "fixed"):
            component = ("libuniverse_crop.so", "autoware::pointcloud_preprocessor::CropBoxFilterComponent") if role == "filter" else ("libuniverse_endpoint.so", "benchmark::Endpoint")
            argv = [runner, *component, *argv[1:]]
            if args.transport == "fixed":
                child_env["AGNOCAST_FIXED_PROBE"] = "1"
        if role == "observer":
            child_env["RMW_IMPLEMENTATION"] = "rmw_cyclonedds_cpp"
            child_env["BENCH_CSV"] = str(mirror)
            child_env.pop("LD_PRELOAD", None)
        proc = subprocess.Popen(argv, env=child_env, stderr=logs.get(label))
        children.append(proc)

    logs = {role: tempfile.TemporaryFile(mode="w+") for role in ("filter", "sub")} if args.transport == "fixed" else {}
    if args.host_loan:
        logs["pub"] = tempfile.TemporaryFile(mode="w+")
    if args.branched and args.transport == "fixed":
        for label in ("tap", "filter2"):
            logs[label] = tempfile.TemporaryFile(mode="w+")
    if args.bridge:
        logs["bridge"] = tempfile.TemporaryFile(mode="w+")
    try:
        if args.bridge:
            launch([runner.with_name("route_bridge"), "a2r", "cloud", "/output", "/mirror/output"], "bridge")
            if args.bridge == "active":
                launch([bin_dir / "endpoint", "--ros-args", "-r", "output:=/mirror/output"], "observer")
        if args.transport == "zenoh":
            launch([Path(get_package_prefix(rmw)) / "lib/rmw_zenoh_cpp/rmw_zenohd"], "router")
            time.sleep(0.6)
            if children[-1].poll() is not None:
                raise RuntimeError("Zenoh router startup failed")
        launch([bin_dir / "endpoint"], "sub")
        crop = [bin_dir / ("crop_cie" if args.ros_cie else "crop"), "--ros-args", "--params-file", config,
                "-p", "input_frame:=base_link", "-p", "output_frame:=base_link"]
        if args.branched:
            launch([bin_dir / "endpoint", "--ros-args", "-r", "__node:=tap", "-r", "output:=/stage1"], "sub", "tap")
            launch([*crop, "-r", "__node:=second_crop", "-r", "input:=/stage1"], "filter", "filter2")
            crop.extend(["-r", "output:=/stage1"])
        launch(crop, "filter")
        time.sleep(2)
        launch([bin_dir / "endpoint", "--ros-args", "-p", f"period_ms:={args.period_ms}"], "pub")
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            for child in children:
                if child.poll() is not None:
                    raise RuntimeError(f"{child.args} exited {child.returncode}")
            if samples.exists():
                with samples.open() as f:
                    rows = list(csv.reader(f))
                if len(rows) == 60 and [int(r[0]) for r in rows] == list(range(60)):
                    if args.bridge != "active" or (mirror.exists() and
                            [int(row[0]) for row in csv.reader(mirror.read_text().splitlines())] == list(range(60))):
                        if not args.branched or (tap.exists() and
                                [int(row[0]) for row in csv.reader(tap.read_text().splitlines())] == list(range(60))):
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

        summaries = {}
        loan_counts = {}
        for role, log in logs.items():
            log.seek(0)
            output = log.read()
            print(output, end="", flush=True)
            if role == "bridge":
                expected = 60 if args.bridge == "active" else 0
                if not re.search(rf"received=60 forwarded={expected}$", output, re.MULTILINE):
                    raise RuntimeError("bridge counts differ from verified frame counts")
                log.close()
                continue
            matches = re.findall(r"fixed_wait arms=\d+ completions=\d+ native=(\d+) both=\d+ native_interrupts=(\d+) stop_interrupts=(\d+)", output)
            log.close()
            if args.host_loan and role in ("pub", "filter", "filter2"):
                loan_counts[role] = re.findall(r"host-cloud publisher loans=(\d+) private_copies=(\d+)", output)
            if role != "pub":
                summaries[role] = matches
    for role, counts in loan_counts.items():
        expected = "60" if args.payload_scope else "59"
        if counts != [(expected, "0")]:
            raise RuntimeError(f"{role}: expected {expected} publisher loans and zero private payload copies")
    for role, matches in summaries.items():
        if len(matches) != 1 or matches[0][1:] != ("0", "1") or not 51 <= int(matches[0][0]) <= 59:
            raise RuntimeError(f"{role}: unexpected fixed wake counts {matches}")
        print(f"verified fixed wait {role}: {matches[0][0]} native wakes")
    for child in children:
        if child.returncode not in (0, -signal.SIGINT):
            raise RuntimeError(f"{child.args} exited {child.returncode} during shutdown")
    if args.bridge == "active":
        with out.with_name(out.stem + "-observer.csv").open("xb") as f:
            f.write(mirror.read_bytes())
    if args.branched:
        with out.with_name(out.stem + "-tap.csv").open("xb") as f:
            f.write(tap.read_bytes())
    with out.open("xb") as f:
        f.write(samples.read_bytes())

if args.resources:
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    with args.resources.open("x") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(("verified_frames", "child_user_s", "child_system_s", "largest_child_maxrss_kib"))
        writer.writerow((60, usage.ru_utime, usage.ru_stime, usage.ru_maxrss))
