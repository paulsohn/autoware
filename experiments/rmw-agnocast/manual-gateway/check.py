"""Sequential gateway laptop replay with the prepared, device-free Lyrical peer."""
import argparse
import csv
import os
from pathlib import Path
import re
import signal
import subprocess as sp
import time
import threading

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
parser.add_argument("cases", nargs="+", choices=["absent","idle","active","restart","crash","late","conflict","ingress","stall","partition","writer-stall","sustained","sustained-control"])
args = parser.parse_args()
args.output.mkdir()
peer = "awpilot0-stock"
env = dict(os.environ, LYRICAL_ROOT=Path("build/agnocast-lyrical/universe-root").read_text().strip(),
           BUFFER_RUNTIME_ONLY="1")
host = ["experiments/buffer/run-lyrical.sh", "timeout","-k","5","100","bash",
        "experiments/rmw-agnocast/manual-gateway/host.sh"]
remote = ["docker","exec","-e","ROS_DOMAIN_ID=173",peer,"bash","-ec",
          'source /opt/ros/lyrical/setup.bash; exec "$@"',"bash"]
sp.run(remote+["bash","-ec",'test ! -e /dev/agnocast; test -z "${LD_PRELOAD:-}"'],check=True)

for index, mode in enumerate(args.cases):
    case = args.output / f"{index}-{mode}"
    case.mkdir()
    children = []
    rec = None
    owned_gateways = {}
    bag = f"/tmp/manual-bag-{os.getpid()}-{index}"
    recpid = bag + ".pid"

    sustained = mode in ("sustained", "sustained-control")
    pressure = sustained or mode in ("stall", "partition", "writer-stall")
    run_env = env.copy()
    peer_cmd = remote.copy()
    if mode == "partition":
        # Shorten the lease so the six-second live fixture spans expiry and rediscovery.
        def profile(side):
            return Path(f"build/lyrical-peer-ready/{side}.xml").read_text().replace(
                "<SPDPInterval>500ms", "<LeaseDuration>1s</LeaseDuration><SPDPInterval>200ms").strip()
        run_env["GATEWAY_CYCLONE_PROFILE"] = profile("host")
        peer_cmd[2:2] = ["-e", "CYCLONEDDS_URI=" + profile("peer")]

    def interrupt_recorder(sig, *, check=True):
        sp.run(["docker","exec",peer,"sh","-ec",'kill -"$1" "$(cat "$2")"',"sh",sig,recpid],check=check)

    def monitor_rss(pids, stop):
        with (case/"rss.csv").open("w", buffering=1) as output:
            writer = csv.writer(output)
            writer.writerow(("time_ns","combined_rss_kib"))
            while not stop.is_set():
                readings = [re.search(r"^VmRSS:\s+(\d+)", Path(f"/proc/{pid}/status").read_text(), re.M)
                            for pid in pids]
                if not all(readings):
                    return
                writer.writerow((time.time_ns(),sum(int(m[1]) for m in readings)))
                stop.wait(.05)

    def start(name, cmd):
        with (case/(name+".log")).open("x") as log:
            p = sp.Popen(cmd, env=run_env, stdout=log, stderr=sp.STDOUT)
        children.append(p)
        return p

    def wait(test, proc, seconds=18):
        end = time.monotonic()+seconds
        while not test():
            if proc.poll() is not None or time.monotonic()>end:
                raise RuntimeError(f"{mode} readiness failure: {proc.args}, status={proc.poll()}")
            time.sleep(.03)

    def done(p):
        if p.wait(timeout=40):
            raise RuntimeError(f"failed {p.args}")

    def contains(name, marker):
        return marker in (case/(name+".log")).read_text()

    def gateway(name):
        route = (["import","geometry_msgs/msg/PoseArray","/peer/poses","/manual/local_poses"]
                 if mode == "ingress" else ["export","sensor_msgs/msg/PointCloud2","/output","/manual/cloud"])
        p = start(name,host+["gateway",str(case/(name+".pid")),*route])
        owned_gateways[name] = p
        wait(lambda:contains(name,"ready role=source") and contains(name,"ready role=destination"), p)
        return p

    def stop_gateway(name,p):
        pid=int((case/(name+".pid")).read_text())
        os.kill(pid,signal.SIGINT)
        done(p)

    try:
        if mode == "conflict":
            relay=gateway("gateway")
            for label, route in (("duplicate",["export","/output","/manual/cloud"]),
                                 ("reciprocal",["import","/manual/cloud","/output"])):
                collision=start(label,host+["gateway",str(case/(label+".pid")),route[0],
                                           "sensor_msgs/msg/PointCloud2",*route[1:]])
                assert collision.wait(timeout=10) != 0 and contains(label,"conflicting route")
            stop_gateway("gateway",relay)
            print(mode,"duplicate and reciprocal routes rejected",flush=True)
            continue
        if mode == "ingress":
            reader=start("reader",host+["ingress-reader"])
            wait(lambda:contains("reader","reader ready"),reader)
            relay=gateway("gateway")
            wait(lambda:contains("gateway","source armed"),relay)
            done(start("poses",remote+["bash","-ec",'touch /tmp/poses-go; exec python3 /tmp/poses.py']))
            done(reader)
            stop_gateway("gateway",relay)
            print(mode,"48/48",flush=True)
            continue
        if mode != "absent":
            relay=gateway("gateway")
        if not sustained:
            graph=start("graph",host+["graph",str(case)])
            wait(lambda:(case/"sink-ready").exists(),graph)
        if mode=="late":
            (case/"go").touch()
            wait(lambda:(case/"seen-8").exists(),graph)
        if mode in ("active","restart","crash","late") or pressure:
            rec=start("recorder",peer_cmd+["bash","-ec",
                'echo $$ > "$1"; exec ros2 bag record --topics /manual/cloud -s sqlite3 --max-cache-size 0 --disable-keyboard-controls -o "$2"',
                "bash",recpid,bag])
            wait(lambda:contains("gateway","source armed"),relay)
        stop_monitor = threading.Event()
        if pressure:
            pids = re.findall(r"ready role=.* pid=(\d+)",(case/"gateway.log").read_text())
            assert len(pids) == 2
            pids.append((case/"gateway.pid").read_text().strip())
            monitor = threading.Thread(target=monitor_rss,args=(pids,stop_monitor),daemon=True)
            monitor.start()
        if sustained:
            graph=start("graph",host+["sustained",str(case/"pipeline")])
            wait(lambda:(case/"pipeline"/"sink-ready").exists(),graph,30)
            time.sleep(12)
            if mode != "sustained-control":
                interrupt_recorder("STOP")
            try:
                (case/"fault-start").write_text(str(time.time_ns()))
                time.sleep(8)
            finally:
                if mode != "sustained-control":
                    interrupt_recorder("CONT")
                (case/"fault-end").write_text(str(time.time_ns()))
        (case/"go").touch()
        if pressure and not sustained:
            wait(lambda:(case/"seen-12").exists(),graph)
            if mode == "partition":
                import sys
                sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"network-peer"))
                from peer_link import link
                iface = link(peer,"down")
            elif mode == "writer-stall":
                writer_pid = int(re.search(r"ready role=destination .* pid=(\d+)",(case/"gateway.log").read_text())[1])
                os.kill(writer_pid,signal.SIGSTOP)
            else:
                interrupt_recorder("STOP")
            try:
                (case/"fault-start").write_text(str(time.time_ns()))
                restore_after = 52 if mode == "writer-stall" else 36
                wait(lambda:(case/f"seen-{restore_after}").exists(),graph)
            finally:
                if mode == "partition":
                    assert link(peer,"up") == iface
                elif mode == "writer-stall":
                    os.kill(writer_pid,signal.SIGCONT)
                else:
                    interrupt_recorder("CONT")
                (case/"fault-end").write_text(str(time.time_ns()))
        if mode in ("restart","crash"):
            wait(lambda:(case/"seen-16").exists(),graph)
            if mode == "crash":
                workers = re.findall(r"ready role=.* pid=(\d+)", (case/"gateway.log").read_text())
                assert len(workers)==2
                os.kill(int((case/"gateway.pid").read_text()),signal.SIGKILL)
                assert relay.wait(timeout=7) != 0
                def dead(pid):
                    path=Path("/proc")/pid/"stat"
                    return not path.exists() or path.read_text().split(") ")[1].startswith("Z")
                wait(lambda:all(dead(pid) for pid in workers), graph)
            else:
                stop_gateway("gateway",relay)
            wait(lambda:(case/"seen-24").exists(),graph)
            relay=gateway("replacement")
        if mode=="late":
            wait(lambda:(case/"seen-36").exists(),graph)
        else:
            if sustained:
                assert graph.wait(timeout=90) == 0
            else:
                done(graph)
        if pressure:
            stop_monitor.set()
            monitor.join()
            assert len((case/"rss.csv").read_text().splitlines()) > 40
        if mode in ("active","restart","crash","late") or pressure:
            interrupt_recorder("INT")
            done(rec)
            sp.run(["docker","cp",peer+":"+bag,str(case/"bag")],check=True)
            name = "replacement" if mode in ("restart","crash") else "gateway"
            wait(lambda:contains(name,"source idle"),relay)
            if mode=="late":
                done(graph)
        if mode != "absent":
            stop_gateway("replacement" if mode in ("restart","crash") else "gateway",relay)
        done(start("verify",host+["verify",str(case)]))
        if sustained:
            print(mode,(case/"verify.log").read_text().splitlines()[0],flush=True)
            continue
        with (case/"delivery.csv").open() as f:
            seen=[int(r["sequence"]) for r in csv.DictReader(f) if r["peer_recorded"]=="1"]
        if pressure:
            assert seen and seen[0]==0 and seen[-1]==59,seen
            if mode == "writer-stall":
                assert int(re.search(r"export overwritten=(\d+)",(case/"gateway.log").read_text())[1]) > 0
        if mode=="active":
            assert seen==list(range(60)),seen
        elif mode=="late":
            assert 8 <= seen[0] < 30 and 35 <= seen[-1] < 59 and seen==list(range(seen[0],seen[-1]+1)), seen
            log=(case/"gateway.log").read_text()
            idle=int(re.search(r"source idle topic=.* taken=(\d+)",log)[1])
            assert contains("gateway",f"stats role=source samples={idle} bytes=")
        elif mode in ("restart","crash"):
            assert seen and seen[0]==0 and seen[-1]==59 and 20 not in seen,seen
        elif not pressure:
            assert not seen
            if mode=="idle":
                assert contains("gateway","stats role=source samples=0 bytes=0")
        print(mode,"local=60",f"peer={len(seen)}",flush=True)
    finally:
        if rec is not None and rec.poll() is None:
            interrupt_recorder("CONT", check=False)
            interrupt_recorder("INT", check=False)
        for name, process in owned_gateways.items():
            pidfile = case/(name+".pid")
            if process.poll() is None and pidfile.exists():
                try:
                    os.kill(int(pidfile.read_text()),signal.SIGINT)
                except ProcessLookupError:
                    pass
        for p in reversed(children):
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(timeout=6)
                except sp.TimeoutExpired:
                    p.kill()
                    p.wait()
