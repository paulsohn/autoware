"""Inside a guarded Lyrical overlay. Exercise one relay configuration."""
import csv
import math
import os
import resource
import signal
import statistics
import subprocess
import sys
import time
from pathlib import Path

from ament_index_python.packages import get_package_prefix

case, output = sys.argv[1:]
out = Path(output).resolve()
out.mkdir()
os.chdir(out)
bin_dir = Path(get_package_prefix("agnocast_rmw_loan_probe")) / "lib/agnocast_rmw_loan_probe"
hook = Path(get_package_prefix("agnocastlib")) / "lib/libagnocast_heaphook.so"
native = dict(RMW_IMPLEMENTATION="rmw_agnocast", LD_PRELOAD=str(hook),
              AGNOCAST_BRIDGE_MODE="OFF", AGNOCAST_NO_DISCOVERY_AGENT="1",
              AGNOCAST_RMW_TOPIC="/fallback/in:/fallback/out",
              AGNOCAST_RMW_HOST_CLOUD="1", AGNOCAST_RMW_BUFFER_CDR="1")
dds = dict(RMW_IMPLEMENTATION="rmw_cyclonedds_cpp")
names = dict(path="nav_msgs/msg/Path", poses="geometry_msgs/msg/PoseArray",
             cloud="sensor_msgs/msg/PointCloud2", image="sensor_msgs/msg/Image")
children = {}


def start(name, args, env):
    with open(name + ".log", "x") as log:
        children[name] = subprocess.Popen(args, stdout=log, stderr=log, env=dict(os.environ, **env))


def ready(name):
    end = time.monotonic() + 8
    while not Path(name + ".ready").exists():
        if children["reader" if name == "silent" else name].poll() is not None or time.monotonic() > end:
            raise RuntimeError(f"{name} failed or timed out")
        time.sleep(.01)


def finish(name):
    assert children[name].wait(timeout=15) == 0, name


def negative(type_name, topic, reason, direction="a2r", env=None):
    result = subprocess.run([bin_dir / "route_bridge", direction, type_name, topic, "/unused"], env=dict(os.environ, **(native if env is None else env)),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=10)
    print(result.stdout.strip())
    assert result.returncode == 1 and reason in result.stdout


def run():
    if case == "no-writer":
        negative("auto", "/absent", "no discoverable publisher type")
        return
    if case == "no-backend":
        without_cdr = {k: v for k, v in native.items() if k != "AGNOCAST_RMW_BUFFER_CDR"}
        negative(names["image"], "/input", "foreign Buffer", "r2a", without_cdr)
        return
    if case == "ambiguous":
        for kind in ("poses", "path"):
            Path("writer.ready").unlink(missing_ok=True)
            start(kind, [bin_dir / "type_fallback_peer", "writer", kind, "/ambiguous"], dds)
            deadline = time.monotonic() + 5
            while not Path("writer.ready").exists():
                if time.monotonic() > deadline or children[kind].poll() is not None:
                    raise RuntimeError("ambiguous publisher startup failed")
                time.sleep(.01)
        negative("auto", "/ambiguous", "ambiguous publisher types")
        return
    direction, kind, adapter, rep = case.split("-")
    sensor_image = kind == "sensorimage"
    incompatible = adapter == "incompatible"
    if sensor_image:
        kind = "image"
        os.environ["FALLBACK_SENSOR_QOS"] = "1"
    if kind == "largecloud":
        kind = "cloud"
        os.environ["FALLBACK_LARGE_CLOUD"] = "1"
    source = native if direction == "a2r" else dds
    dest = dds if direction == "a2r" else native
    for role, topic, env in (("writer", "/fallback/in", source), ("reader", "/fallback/out", dest),
                             ("witness", "/fallback/out", source)):
        start(role, [bin_dir / "type_fallback_peer", ("silent" if role == "reader" and incompatible else role), kind, topic], env)
        ready(role if not (role == "reader" and incompatible) else "silent")
    relay_env = dict(native, BRIDGE_READY_FILE=str(out / "relay.ready"),
                     AGNOCAST_BRIDGE_PROFILE=str(out / "service.csv"))
    if adapter == "forced":
        relay_env["AGNOCAST_BRIDGE_FORCE_GENERIC"] = "1"
    if direction == "a2r":
        relay_env["AGNOCAST_BRIDGE_DEMAND"] = "1"
    configured = names[kind] if adapter == "typed" else "auto"
    qos = "reliable" if incompatible else "sensor" if sensor_image else "default"
    start("relay", [bin_dir / "route_bridge", direction, configured, "/fallback/in", "/fallback/out", qos], relay_env)
    ready("relay")
    expected = "typed" if (adapter == "typed" or (adapter == "auto" and kind == "cloud")) else "serialized"
    assert f"route_type={names[kind]} adapter={expected}" in Path("relay.log").read_text()
    cli = case == "a2r-poses-auto-1"
    if cli:
        negative("no_such_interface_pkg/msg/Missing", "/absent", "no_such_interface_pkg")
        listing = subprocess.check_output(["ros2", "topic", "list", "-t", "--no-daemon", "--spin-time", "0.5"],
                                         text=True, env=dict(os.environ, **dds), timeout=5)
        assert f"/fallback/out [{names[kind]}]" in listing
        info = subprocess.check_output(["ros2", "topic", "info", "-v", "--no-daemon",
                                       "--spin-time", "0.5", "/fallback/out"], env=dict(os.environ, **dds),
                                      text=True, timeout=5)
        assert names[kind] in info and "agnocast_route_bridge" in info
        start("echo", ["ros2", "topic", "echo", "--once", "--field", "header.frame_id", "/fallback/out", names[kind]], dds)
    time.sleep(.7 if cli else .4)
    Path("go").touch()
    for name in ("writer", "reader", "witness"):
        finish(name)
    if cli:
        finish("echo")
        assert "lidar_" in Path("echo.log").read_text()
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    children["relay"].send_signal(signal.SIGINT)
    finish("relay")
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    print(f"relay_process_cpu_s={after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime:.4f}")
    if incompatible:
        assert "received=0 forwarded=0" in Path("relay.log").read_text()
        print(f"{case} incompatible=0 wrong_leg=0")
        return
    assert "received=48 forwarded=48" in Path("relay.log").read_text()
    with Path("reader.csv").open() as f:
        arrivals = list(csv.reader(f))
    assert [int(row[0]) for row in arrivals] == list(range(48))
    with Path("service.csv").open() as f:
        service = list(csv.reader(f))
    assert len(service) == 48
    # Align by sequence. Exclude eight warm-up samples from each distribution.
    for metric, data in (("e2e", [int(row[1]) for row in arrivals[8:]]),
                         ("relay", [int(end)-int(begin) for _,begin,end in service[8:]])):
        print(f"{metric}_us median={statistics.median(data)/1000:.1f} p95={sorted(data)[math.ceil(.95*len(data))-1]/1000:.1f}")
    print(f"{case} type={names[kind]} adapter={expected} exact=48 wrong_leg=0")


try:
    run()
finally:
    for proc in children.values():
        if proc.poll() is None:
            proc.send_signal(signal.SIGINT)
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
