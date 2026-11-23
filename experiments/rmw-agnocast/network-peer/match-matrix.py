"""Run with sg docker in the provisioned colcon worktree, with a fresh output path."""
from pathlib import Path
import os
from peer_link import link
import subprocess as sp
import sys
import time

out = Path(sys.argv[1])
out.mkdir()
recipe = Path("experiments/rmw-agnocast/network-peer")
ready = Path("build/lyrical-peer-ready")
peer = "awpilot0-stock"
net = "awpilot0"
template = "<LeaseDuration>1s</LeaseDuration><SPDPInterval>200ms"
def profile(side):
    return (ready / (side + ".xml")).read_text().replace("<SPDPInterval>500ms", template).strip()
(out / "host.xml").write_text(profile("host"))
env = dict(os.environ, LYRICAL_ROOT=Path("build/agnocast-lyrical/universe-root").read_text().strip(),
           ROS_DOMAIN_ID="84", BUFFER_RUNTIME_ONLY="1")
ip = sp.check_output(["docker", "inspect", "-f", '{{(index .NetworkSettings.Networks "awpilot0").IPAddress}}', peer], text=True).strip()
assert ip
def wait(marker, host):
    end = time.monotonic()+13
    while not marker.exists():
        if host.poll() is not None or time.monotonic()>end:
            raise RuntimeError(str(marker))
        time.sleep(.02)

for fault in ("link", "replace"):
    for rmw in ("stock", "shim"):
        for order in ("query", "event"):
            case = out / f"{fault}-{rmw}-{order}"
            case.mkdir()
            with (case/"host.log").open("w") as h, (case/"peer.log").open("w") as p:
                host = sp.Popen(["experiments/buffer/run-lyrical.sh", "timeout", "22", "bash",
                    str(recipe/"host.sh"), "match-probe", str(case), rmw, order], env=env, stdout=h, stderr=sp.STDOUT)
                sub = sp.Popen(["docker", "exec", "-e", "CYCLONEDDS_URI="+profile("peer"), peer, "bash", "-ec",
                    'source /opt/ros/lyrical/setup.bash; echo $$ >/tmp/match-probe.pid; exec ros2 topic echo --no-daemon /match_probe std_msgs/msg/Int32'],
                    stdout=p, stderr=sp.STDOUT)
                try:
                    wait(case/"ready", host)
                    if fault == "link":
                        ifindex = link(peer, "down")
                    else:
                        sp.run(["docker", "network", "disconnect", net, peer], check=True)
                    try:
                        time.sleep(1.6)
                        (case/"dispatch").touch()
                        wait(case/"observed", host)
                    finally:
                        if fault == "link":
                            restored_index = link(peer, "up")
                            assert ifindex == restored_index
                        else:
                            sp.run(["docker", "network", "connect", "--ip", ip, net, peer], check=True)
                    assert host.wait(timeout=12) == 0
                finally:
                    sp.run(["docker", "exec", peer, "sh", "-c", 'kill -INT "$(cat /tmp/match-probe.pid)"'], check=False)
                    sub.wait(timeout=8)
                    host.wait(timeout=25)
            trace = (case/"host.log").read_text()
            messages = [line for line in (case/"peer.log").read_text().splitlines() if line.startswith("data:")]
            expected_events = [1] if order == "query" else [1, 0]
            assert f"after_dispatch 0 events {expected_events}" in trace, trace
            print(case.name, trace.strip(), messages, flush=True)
