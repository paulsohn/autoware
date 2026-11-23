"""Run sequentially on the idle awpilot0-stock peer. Supply a fresh build directory."""
import csv
import json
import os
import re
import signal
import subprocess as sp
import sys
import time
from pathlib import Path
from peer_link import link

recipe = Path("experiments/rmw-agnocast/network-peer")
out = Path(sys.argv[1])
out.mkdir()
ready = Path("build/lyrical-peer-ready")
peer = "awpilot0-stock"
net = "awpilot0"
peer_ip = sp.check_output(["docker", "inspect", "-f", "{{(index .NetworkSettings.Networks \"awpilot0\").IPAddress}}", peer], text=True).strip()
assert peer_ip
# Six seconds of source data. Advertise a shorter lease only for these participants.
def profile(side):
    return (ready / (side + ".xml")).read_text().replace("<SPDPInterval>500ms",
        "<LeaseDuration>1s</LeaseDuration><SPDPInterval>200ms")
(out / "host.xml").write_text(profile("host"))
env = dict(os.environ, ROS_DOMAIN_ID="84", BUFFER_RUNTIME_ONLY="1",
           LYRICAL_ROOT=Path("build/agnocast-lyrical/universe-root").read_text().strip())

def docker(*args):
    sp.run(["docker", *args], timeout=20, check=True, stdout=sp.DEVNULL)

def run_case(mode):
    case = out / mode
    case.mkdir()
    (case / "bag").mkdir()
    processes = {}
    events = {}
    disconnected = False
    suspended = False
    down_index = None
    pidfile = "/tmp/recovery-" + str(os.getpid()) + ".pid"
    def mark(label):
        events[label] = time.time_ns()
    def start(name, cmd):
        with (case / (name + ".log")).open("x") as log:
            processes[name] = sp.Popen(cmd, env=env, stdout=log, stderr=sp.STDOUT)
    def host(name, action, *args):
        start(name, ["experiments/buffer/run-lyrical.sh", "timeout", "-k", "3", "35",
                     "bash", str(recipe / "host.sh"), action, str(case), *args])
    def finish(name):
        assert processes[name].wait(timeout=40) == 0, name
        del processes[name]
    def wait(predicate):
        deadline = time.monotonic() + 15
        while not predicate():
            for name, proc in processes.items():
                if proc.poll() is not None:
                    raise RuntimeError(f"{mode}: {name} exited {proc.returncode}")
            if time.monotonic() > deadline:
                raise TimeoutError(f"{mode}: {list(events)}")
            time.sleep(.015)
    def seen(count):
        wait(lambda: (case / f"seen-{count}").exists())
    def matched(name, count, occurrence=1):
        wait(lambda: (case / (name + ".log")).read_text().count(f"matches={count} ") >= occurrence)
    def recorder(name):
        bag = f"/bags/{out.name}-{mode}-{name}"
        start(name, ["docker", "exec", "-e", "CYCLONEDDS_URI=" + profile("peer").strip(), peer,
            "bash", "-ec", 'source /opt/ros/lyrical/setup.bash; echo $$ > "$1"; exec ros2 bag record --topics /dds/synchronized -s sqlite3 --max-cache-size 0 --disable-keyboard-controls -o "$2"', "bash", pidfile, bag])
        return bag
    def stop_rec(name, bag):
        docker("exec", peer, "bash", "-ec", 'kill -INT "$(cat "$1")"', "bash", pidfile)
        finish(name)
        docker("cp", peer + ":" + bag, str(case / "bag" / name))
        # Remove only this test bag after securing the local copy.
        assert list((case / "bag" / name).glob("*.db3"))
        docker("exec", peer, "rm", "-r", bag)
    def stop_relay(name):
        os.kill(int((case / (name + ".pid")).read_text()), signal.SIGINT)
        finish(name)

    try:
        host("graph", "unrouted")
        host("relay1", "recovery-relay", "relay1")
        name = "rec1"
        bag = recorder(name)
        wait(lambda: (case / "sink-ready").exists())
        matched("relay1", 1)
        (case / "go").touch()
        seen(12)
        mark("interrupt_begin")
        if mode in ("lease", "lease-restart"):
            docker("network", "disconnect", net, peer)
            disconnected = True
            matched("relay1", 0)
        elif mode == "link":
            down_index = link(peer, "down")
            matched("relay1", 0)
        elif mode == "suspend":
            docker("exec", peer, "bash", "-ec", 'kill -STOP "$(cat "$1")"', "bash", pidfile)
            suspended = True
            matched("relay1", 0)
        elif mode == "recorder":
            stop_rec(name, bag)
            matched("relay1", 0)
        else:
            stop_relay("relay1")
        mark("inactive_observed")
        seen(32)
        mark("restore_begin")
        if mode in ("lease", "lease-restart"):
            docker("network", "connect", "--ip", peer_ip, net, peer)
            disconnected = False
            if mode == "lease-restart":
                stop_rec(name, bag)
                name = "rec2"
                bag = recorder(name)
                matched("relay1", 1, 2)
        elif mode == "link":
            restored_index = link(peer, "up")
            assert restored_index == down_index
            down_index = None
            matched("relay1", 1, 2)
        elif mode == "suspend":
            docker("exec", peer, "bash", "-ec", 'kill -CONT "$(cat "$1")"', "bash", pidfile)
            suspended = False
        elif mode == "recorder":
            name = "rec2"
            bag = recorder(name)
            matched("relay1", 1, 2)
        else:
            host("relay2", "recovery-relay", "relay2")
            matched("relay2", 1)
        if mode in ("lease", "suspend"):
            deadline = time.monotonic() + 12
            while (case / "relay1.log").read_text().count("matches=1 ") < 2 and time.monotonic() < deadline:
                time.sleep(.02)
        rematched = mode not in ("lease", "suspend") or (case / "relay1.log").read_text().count("matches=1 ") >= 2
        mark("rematch_observed" if rematched else "rematch_timeout")
        finish("graph")
        stop_relay("relay2" if mode == "gateway" else "relay1")
        stop_rec(name, bag)
        traces = sorted(case.glob("relay*.csv"))
        (case / "forward.csv").write_text("".join(p.read_text() for p in traces))
        host("verify", "verify")
        finish("verify")
        rows = list(csv.DictReader((case / "audit.csv").open()))
        received = [int(r["sequence"]) for r in rows if r["bag_ns"]]
        last = max(i for i in received if i < 32)
        first = min((i for i in received if i >= 32), default=None)
        expected = list(range(last + 1))
        if first is not None:
            expected.extend(range(first, 60))
        assert received == expected, received
        if mode not in ("lease", "suspend"):
            assert first is not None
        transitions = []
        for log in sorted(case.glob("relay*.log")):
            transitions.extend(re.findall(r"\[(\d+\.\d+)\].*matches=(\d+) received=(\d+) forwarded=(\d+)", log.read_text()))
        if mode != "gateway":
            assert [t[1] for t in transitions] == (["1", "0", "1"] if rematched else ["1", "0"])
            if rematched:
                assert transitions[1][2:] == transitions[2][2:]
            else:
                final_counts = re.search(r"\]: received=(\d+) forwarded=(\d+)", (case / "relay1.log").read_text())
                assert final_counts and final_counts.groups() == transitions[1][2:]
        lost_forward = [int(r["sequence"]) for r in rows if r["forwarded"] == "1" and not r["bag_ns"]]
        result = dict(events=events, transitions=transitions, local=len(rows), recorded=len(received),
                      last_before=last, first_after=first, forwarded_without_bag=lost_forward,
                      restore_to_bag_ms=(int(rows[first]["bag_ns"])-events["restore_begin"])/1e6 if first is not None else None)
        (case / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(mode, result, flush=True)
    finally:
        if down_index is not None:
            link(peer, "up")
        if suspended:
            docker("exec", peer, "bash", "-ec", 'kill -CONT "$(cat "$1")"', "bash", pidfile)
        if disconnected:
            docker("network", "connect", "--ip", peer_ip, net, peer)
        if any(n.startswith("rec") for n in processes):
            docker("exec", peer, "bash", "-c", 'test ! -e "$1" || kill -INT "$(cat "$1")" 2>/dev/null || true', "bash", pidfile)
        for proc in processes.values():
            proc.wait(timeout=42)
        docker("exec", peer, "rm", "-f", pidfile)

trials = ("lease", "recorder", "gateway", "suspend", "lease-restart", "link")
for trial in sys.argv[2:] or trials:
    if trial not in trials:
        raise ValueError(trial)
    run_case(trial)
