"""Run from the colcon worktree with Docker group access. Argument is a fresh output directory."""
import json
import os
import re
import signal
import subprocess as sp
import sys
import time
from ipaddress import ip_address
from pathlib import Path

if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != "--prepare-only"):
    raise SystemExit("usage: batch.py FRESH_OUTPUT_DIRECTORY [--prepare-only]")
prepare_only = len(sys.argv) == 3
keep_peer = False
out = Path(sys.argv[1])
out.mkdir()
recipe = Path(__file__).parent
image = os.getenv("PEER_IMAGE", "local/lyrical-cyclone-peer:v1")
net = "awpilot0" if prepare_only else f"awpeer{os.getpid()}"
peer = net + "-stock"
env = dict(os.environ, LYRICAL_ROOT=Path("build/agnocast-lyrical/universe-root").read_text().strip(),
           ROS_DOMAIN_ID="84", BUFFER_RUNTIME_ONLY="1")
profile = """<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="{iface}"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><MaxAutoParticipantIndex>32</MaxAutoParticipantIndex><Peers><Peer Address="{own}"/><Peer Address="{other}"/></Peers><SPDPInterval>500ms</SPDPInterval></Discovery></Domain></CycloneDDS>"""
children = []
created = False
network_created = False


def run(*args):
    return sp.check_output(args, text=True, timeout=30).strip()


def docker(*args):
    return run("docker", *args)


def peer_command(*args):
    return ["docker","exec",peer,"bash","-ec",'source /opt/ros/lyrical/setup.bash; exec "$@"',"bash",*args]


def host(mode, *args):
    return ["experiments/buffer/run-lyrical.sh","timeout","-k","5","45","bash",str((recipe/"host.sh").resolve().relative_to(Path.cwd())),mode,str(case),*args]


def start(name, cmd):
    with (case/(name+".log")).open("x") as log:
        proc = sp.Popen(cmd, stdout=log,stderr=sp.STDOUT,env=env)
    children.append(proc)
    return proc


def wait_for(check, proc, timeout=15):
    end = time.monotonic()+timeout
    while not check():
        if proc.poll() is not None or time.monotonic()>end:
            raise RuntimeError(f"condition failed for {proc.args} exit={proc.poll()}")
        time.sleep(.015)


def done(proc):
    if proc.wait(timeout=50) != 0: raise RuntimeError(proc.args)


def counters():
    base=Path("/sys/class/net")/net/"statistics"
    return {k:int((base/k).read_text()) for k in ("tx_bytes","rx_bytes")}


def cloud(mode):
    proc = start("host", host(mode))
    wait_for(lambda:(case/"sink-ready").exists(),proc)
    before=counters()
    (case/"go").touch()
    wait_for(lambda:(case/"seen-4").exists(),proc)
    events={}
    if mode=="idle":
        done(proc)
        done(start("readback",host("verify","idle")))
        after=counters()
        assert not (case/"forward.csv").read_text().strip()
        assert "received=0 forwarded=0" in (case/"relay.log").read_text()
        (case/"network.json").write_text(json.dumps(dict(before=before,after=after,received=[]),indent=2)+"\n")
        return
    topic="/output" if mode=="unrouted" else "/dds/synchronized"
    bag="/bags/"+case.name
    # Retain the actual rosbag process PID across the Docker network reattachment.
    events["recorder_launch"]=time.time_ns()
    rec=start("recorder",peer_command("bash","-c",'echo $$ > /tmp/recorder-pid; exec ros2 bag record --topics "$1" -s sqlite3 --max-cache-size 0 --disable-keyboard-controls -o "$2"',"bash",topic,bag))
    wait_for(lambda:f"Subscribed to topic '{topic}'" in (case/"recorder.log").read_text(),rec)
    if mode=="reconnect":
        wait_for(lambda:(case/"seen-16").exists(),proc)
        events["disconnect_begin"]=time.time_ns()
        docker("network","disconnect",net,peer)
        events["disconnect_end"]=time.time_ns()
        wait_for(lambda:(case/"seen-32").exists(),proc)
        events["connect_begin"]=time.time_ns()
        docker("network","connect","--ip",peer_ip,net,peer)
        events["connect_end"]=time.time_ns()
    done(proc)
    after=counters()
    docker("exec",peer,"sh","-c",'kill -INT "$(cat /tmp/recorder-pid)"')
    done(rec)
    docker("cp",peer+":"+bag,str(case/"bag"))
    done(start("readback",host("verify")))
    import csv
    with (case/"audit.csv").open() as f: rows=list(csv.DictReader(f))
    seq=[int(r["sequence"]) for r in rows if r["bag_ns"]]
    log=(case/"relay.log").read_text() if (case/"relay.log").exists() else ""
    matches=re.findall(r"\[(\d+\.\d+)\].*matches=1 ",log)
    print(case.name, "recorded",seq,"interface_bytes",{k:after[k]-before[k] for k in after},flush=True)
    report=dict(events=events, before=before,after=after, received=seq,matched=matches)
    (case/"network.json").write_text(json.dumps(report,indent=2)+"\n")
    if mode=="unrouted": assert not seq
    else:
        assert seq and matches and after["tx_bytes"]>before["tx_bytes"]+1000000
        if mode=="healthy": assert seq==list(range(seq[0],60))
        else: assert seq[0]<16 and seq[-1]==59 and set(range(18,30)).isdisjoint(seq)


try:
    docker("network","create","--internal","-o","com.docker.network.bridge.name="+net,net)
    network_created=True
    host_ip=docker("network","inspect","-f","{{(index .IPAM.Config 0).Gateway}}",net)
    peer_ip=str(ip_address(host_ip)+1)
    for file,iface,own,other in (("host",net,host_ip,peer_ip),("peer","eth0",peer_ip,host_ip)):
        (out/(file+".xml")).write_text(profile.format(iface=iface,own=own,other=other)+"\n")
    docker("run","-d","--pull=never","--name",peer,"--network",net,"--ip",peer_ip,"--ipc","private",
           "--memory","384m","--memory-swap","384m","--cap-drop","ALL","--security-opt","no-new-privileges",
           "-e","RMW_IMPLEMENTATION=rmw_cyclonedds_cpp","-e","ROS_DOMAIN_ID=84","-e","CYCLONEDDS_URI=file:///tmp/peer.xml",
           image,"sleep","infinity")
    created=True
    docker("cp",str(out/"peer.xml"),peer+":/tmp/peer.xml")
    docker("cp",str(recipe/"poses.py"),peer+":/tmp/poses.py")
    docker("exec",peer,"mkdir","/bags")
    preflight=run(*peer_command("bash","-ec",'''test ! -e /dev/agnocast && test -z "${LD_PRELOAD:-}" && test "$AMENT_PREFIX_PATH" = /opt/ros/lyrical
for p in rmw_cyclonedds_cpp rosbag2_storage_sqlite3 ros2bag sensor_msgs geometry_msgs; do ros2 pkg prefix "$p"; done
readlink /proc/self/ns/net /proc/self/ns/ipc
cat /sys/class/net/eth0/mtu /sys/fs/cgroup/memory.max
dpkg-query -W ros-lyrical-rmw-cyclonedds-cpp ros-lyrical-cyclonedds ros-lyrical-rosbag2'''))
    (out/"peer.txt").write_text(docker("inspect","-f","{{.Image}} mounts={{len .Mounts}} ipc={{.HostConfig.IpcMode}}",peer)+"\n"+preflight+"\nhost namespaces\n"+run("readlink","/proc/self/ns/net","/proc/self/ns/ipc")+"\n")
    if prepare_only:
        print(f"peer={peer} bridge={net} host={host_ip} peer_ip={peer_ip} domain=84")
        print(f"host_profile={out.resolve()}/host.xml peer_profile=/tmp/peer.xml")
        keep_peer=True
        sys.exit(0)
    case=out/"plain"
    case.mkdir()
    baseline=counters()
    for direction in ("outbound","inbound"):
        topic="/peer_test/"+direction
        echo=["topic","echo","--no-daemon","--once",topic,"std_msgs/msg/String"]
        pub=["topic","pub","--once","-w","1",topic,"std_msgs/msg/String","{data: "+direction+"}"]
        sub=start(direction+"-echo",peer_command("ros2",*echo) if direction=="outbound" else host("cli",*echo))
        writer=start(direction+"-pub",host("cli",*pub) if direction=="outbound" else peer_command("ros2",*pub))
        done(writer)
        done(sub)
        assert "data: "+direction in (case/(direction+"-echo.log")).read_text()
    (case/"network.json").write_text(json.dumps(dict(before=baseline,after=counters()),indent=2)+"\n")
    print("plain DDS both directions",baseline,counters(),flush=True)
    case=out/"ingress"
    case.mkdir()
    writer=start("peer-writer",peer_command("python3","/tmp/poses.py"))
    reader=start("native-reader",host("ingress"))
    wait_for(lambda:(case/"reader.ready").exists() and "adapter=serialized" in (case/"relay.log").read_text(),reader)
    docker("exec",peer,"touch","/tmp/poses-go")
    done(writer)
    (case/"finished").touch()
    done(reader)
    assert len((case/"reader.csv").read_text().splitlines())==48
    print("remote PoseArray -> native reader 48/48",flush=True)
    for trial in ("unrouted","idle","healthy-1","reconnect-1","reconnect-2","healthy-2"):
        mode=trial.split("-")[0]
        case=out/trial
        case.mkdir()
        cloud(mode)
finally:
    if created and not keep_peer: docker("rm","-f",peer)
    if network_created and not keep_peer: docker("network","rm",net)
    for child in children:
        if child.poll() is None:
            try: child.wait(timeout=50)
            except sp.TimeoutExpired: child.terminate()
