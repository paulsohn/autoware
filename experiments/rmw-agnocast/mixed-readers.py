"""Run inside the guarded Lyrical overlay. Keep one writer alive across all reader exits."""
import math
import os
import re
import signal
import sqlite3
import statistics
import subprocess
import sys
import time
from pathlib import Path

from ament_index_python.packages import get_package_prefix

bins = Path(get_package_prefix("agnocast_rmw_loan_probe")) / "lib/agnocast_rmw_loan_probe"
hook = Path(get_package_prefix("agnocastlib")) / "lib/libagnocast_heaphook.so"
out = Path(sys.argv[1]).resolve()
out.mkdir()
os.chdir(out)
mode = sys.argv[2]
assert mode in ("native", "rmw", "cyclone", "zenoh")
native = mode == "native"
bags = sys.argv[3:] == ["--bags"]
assert not bags or mode == "rmw"
children = {}
env = dict(os.environ, RMW_IMPLEMENTATION="rmw_cyclonedds_cpp" if native else "rmw_agnocast",
           AGNOCAST_RMW_TOPIC="/ownership", AGNOCAST_RMW_HOST_CLOUD="1", AGNOCAST_RMW_HOST_VIEW="1",
           AGNOCAST_RMW_HOST_LOAN="1", AGNOCAST_BRIDGE_MODE="OFF", AGNOCAST_NO_DISCOVERY_AGENT="1",
           LD_PRELOAD=str(hook), ROS_DISABLE_LOANED_MESSAGES="0")
env["CYCLONEDDS_URI"] = ('<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/>'
                         '</Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery>'
                         '<ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="127.0.0.1"/>'
                         '</Peers></Discovery></Domain></CycloneDDS>')


def start(name, role):
    args = [bins / "mixed_cloud", mode, role]
    child_env = env.copy()
    if mode in ("cyclone", "zenoh"):
        del child_env["LD_PRELOAD"]
        child_env["RMW_IMPLEMENTATION"] = "rmw_cyclonedds_cpp" if mode == "cyclone" else "rmw_zenoh_cpp"
        child_env["ROS_DISABLE_LOANED_MESSAGES"] = "1"
    if role in ("recorder", "bag"):
        del child_env["LD_PRELOAD"]
        child_env["RMW_IMPLEMENTATION"] = "rmw_cyclonedds_cpp"
        if role == "bag":
            args = ["ros2", "bag", "record", "--topics", "/mirror", "-o", name,
                    "-s", "sqlite3", "--max-cache-size", "0", "--disable-keyboard-controls"]
    elif role == "bridge":
        args = [bins / "route_bridge", "a2r", "cloud", "/ownership", "/mirror"]
        child_env["BRIDGE_READY_FILE"] = "bridge-ready"
    elif role == "writer" and mode == "rmw":
        child_env["AGNOCAST_RMW_AUDIT_RELEASE"] = "1"
    with open(name + ".log", "xb") as log:
        children[name] = subprocess.Popen(args, env=child_env, stdout=log, stderr=log)


def stop(name, sig=signal.SIGINT):
    p = children.pop(name)
    p.send_signal(sig)
    try:
        code = p.wait(timeout=8)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
        raise
    assert code in ((-sig,) if sig == signal.SIGKILL else (0, -sig)), (name, code)


def wait(test, seconds=10):
    end = time.monotonic() + seconds
    while not test():
        for name, p in children.items():
            if p.poll() is not None:
                raise RuntimeError(f"{name} exited {p.returncode}")
        if time.monotonic() > end:
            raise TimeoutError(test)
        time.sleep(.02)


def rows(name, tag="got"):
    text = Path(name + ".log").read_text(errors="replace")
    return [tuple(map(int, m)) for m in re.findall(rf"^{tag} (\d+) (\d+) (\d+)$", text, re.M)]


def phase(name, first, last, reader="cpu"):
    delays = [d / 1e6 for i, d, addr in rows(reader) if first <= i <= last]
    assert len(delays) == last - first + 1, (name, reader, first, last, len(delays))
    delays.sort()
    print(f"{name} {reader} seq={first}..{last} n={len(delays)} "
          f"p50_ms={statistics.median(delays):.3f} p95_ms={delays[math.ceil(.95*len(delays))-1]:.3f}",
          flush=True)


def check_rows(name, sent, same_address):
    frames = rows(name)
    ids = [i for i, _, _ in frames]
    assert ids and ids == list(range(ids[0], ids[-1]+1)), (name, ids)
    assert all((addr == sent[i][1]) == same_address for i, _, addr in frames), name


def reclaimed():
    return set(map(int, re.findall(r"host_payload_reclaimed=(\d+)", Path("writer.log").read_text())))


def verify_bag(name, sent):
    from rclpy.serialization import deserialize_message
    from sensor_msgs.msg import PointCloud2
    db, = Path(name).glob("*.db3")
    ids = []
    with sqlite3.connect(f"file:{db}?mode=ro", uri=True) as conn:
        for topic, raw in conn.execute(
                "SELECT name,data FROM messages JOIN topics ON topics.id=topic_id ORDER BY messages.id"):
            assert topic == "/mirror"
            msg = deserialize_message(raw, PointCloud2)
            payload = bytes(msg.data)
            seq = int.from_bytes(payload[:4], "little")
            block = bytes((seq+i) % 256 for i in range(256))
            assert payload[4:] == (block * 1024)[4:]
            assert msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec == sent[seq][0]
            assert (msg.header.frame_id, msg.width, msg.height, msg.point_step, msg.row_step) == (
                "lidar", 65536, 1, 4, 262144)
            ids.append(seq)
    assert len(ids) >= 20 and ids == list(range(ids[0], ids[-1]+1)), (name, ids)
    print(f"{name} stored={len(ids)} seq={ids[0]}..{ids[-1]} payloads_verified", flush=True)
    return ids


def shared_rss(pid, address):
    region = False
    for line in Path(f"/proc/{pid}/smaps").read_text().splitlines():
        match = re.match(r"^([0-9a-f]+)-([0-9a-f]+) (....)", line)
        if match:
            region = int(match[1], 16) <= address < int(match[2], 16) and match[3] == "rw-s"
        elif region and line.startswith("Rss:"):
            return int(line.split()[1])
    raise ValueError("publisher payload mapping missing")


try:
    start("cpu", "cpu")
    start("writer", "writer")
    wait(lambda: Path("cpu-ready").exists())
    time.sleep(.8)
    Path("start").touch()
    wait(lambda: len(rows("cpu")) >= 45)
    phase("baseline", 6, 45)
    if mode == "rmw":
        start("bridge", "bridge")
        wait(lambda: Path("bridge-ready").exists())
        last_stored = 0
        for cycle in range(1, 5):
            holder = f"holder-{cycle}"
            start(holder, "holder")
            wait(lambda: len(rows(holder)) >= 12)
            held = rows(holder)[0][0]
            stamp = dict((i, t) for i, t, _ in rows("writer", "sent"))[held]
            for epoch in (1, 2):
                recorder = f"recorder-{cycle}-{epoch}"
                bag = f"bag-{cycle}-{epoch}"
                if bags:
                    start(bag, "bag")
                    wait(lambda: "Subscribed to topic" in Path(bag + ".log").read_text())
                start(recorder, "recorder")
                wait(lambda: len(rows(recorder)) >= 20)
                frames = rows(recorder)
                first, last = frames[0][0], frames[-1][0]
                for name in ("cpu", holder, recorder):
                    phase(f"recording-{cycle}-{epoch}", first, last, name)
                assert stamp not in reclaimed(), "retained payload released during observation"
                stop(recorder)
                if bags:
                    stop(bag)
                    stored = verify_bag(bag, {i: (t, a) for i, t, a in rows("writer", "sent")})
                    assert first in stored and last in stored and stored[0] > last_stored
                    last_stored = stored[-1]
                if epoch == 1:
                    wait(lambda: rows("cpu")[-1][0] >= last + 6)
            sent = {i: (t, a) for i, t, a in rows("writer", "sent")}
            for name in (f"recorder-{cycle}-1", f"recorder-{cycle}-2"):
                # DDS can independently map the same virtual address; only check bytes and sequences.
                ids = [i for i, _, _ in rows(name)]
                assert ids == list(range(ids[0], ids[-1]+1))
            check_rows(holder, sent, True)
            held_addr = sent[held][1]
            assert all(addr != held_addr for i, (_, addr) in sent.items() if i > held)
            stop(holder, signal.SIGKILL if cycle % 2 == 0 else signal.SIGINT)
            last = rows("cpu")[-1][0]
            wait(lambda: rows("cpu")[-1][0] >= last + 35)
            snapshot = Path("writer.log").read_text()
            released = set(map(int, re.findall(r"host_payload_reclaimed=(\d+)", snapshot)))
            sent = [tuple(map(int, m)) for m in re.findall(r"^sent (\d+) (\d+) (\d+)$", snapshot, re.M)]
            old = [i for i, t, _ in sent[:-10] if t not in released]
            live = sum(t not in released for _, t, _ in sent)
            assert stamp in released and not old and 4 <= live <= 6, (cycle, old, live)
            rss = shared_rss(children["writer"].pid, sent[-1][2])
            print(f"drain cycle={cycle} exit={'kill' if cycle%2==0 else 'graceful'} held={held} "
                  f"held_reclaimed={stamp in released} old_outstanding={old} "
                  f"live_payloads={live} shared_rss_KiB={rss}", flush=True)
        stop("bridge")
    Path("stop").touch()
    assert children["writer"].wait(timeout=8) == 0
    del children["writer"]
    time.sleep(.2)
    stop("cpu")
    sent = {i: (t, a) for i, t, a in rows("writer", "sent")}
    check_rows("cpu", sent, native)
    assert len(rows("cpu")) == len(sent), "always-on reader missed frames"
    if mode == "rmw":
        assert f"publisher loans={len(sent)} private_copies=0" in Path("writer.log").read_text()
    print(f"continuous_reader={len(sent)}/{len(sent)}", flush=True)
finally:
    for name in list(children):
        if children[name].poll() is None:
            children[name].kill()
        children[name].wait()
