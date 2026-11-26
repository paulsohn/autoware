"""Sequential gateway laptop replay with the prepared, device-free Lyrical peer."""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import signal
import subprocess as sp
import time
import threading

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
parser.add_argument("cases", nargs="+", choices=["absent","idle","active","restart","crash","late","conflict","ingress","stall","partition","writer-stall","sustained","sustained-control","dual-active","dual-writer-stall","dual-restart"])
parser.add_argument("--churn", choices=["native", "configured", "configured-matched", "discovered"],
                    help="apply the same endpoint workload to a sustained-control run")
parser.add_argument("--discovery-control", action="store_true",
                    help="advertise the recorder export with dynamic discovery running")
args = parser.parse_args()
if (args.churn or args.discovery_control) and args.cases != ["sustained-control"]:
    parser.error("discovery and churn controls require the sustained-control case")
args.output.mkdir()
peer = os.getenv("GATEWAY_BENCH_PEER", "awpilot0-stock")
env = dict(os.environ, LYRICAL_ROOT=Path("build/agnocast-lyrical/universe-root").read_text().strip(),
           BUFFER_RUNTIME_ONLY="1")
host = ["experiments/buffer/run-lyrical.sh", "timeout","-k","5","100","bash",
        "experiments/rmw-agnocast/manual-gateway/host.sh"]
remote = ["docker","exec","-e","ROS_DOMAIN_ID=173",peer,"bash","-ec",
          'source /opt/ros/lyrical/setup.bash; exec "$@"',"bash"]
sp.run(remote+["bash","-ec",'test ! -e /dev/agnocast; test -z "${LD_PRELOAD:-}"'],check=True)

if args.churn:
    network = env["GATEWAY_BENCH_RMW"]
    remote[2:2] = ["-e", "RMW_IMPLEMENTATION=" + network,
                    "-e", "CYCLONEDDS_URI=" + env["GATEWAY_PEER_PROFILE"],
                    "-e", "ZENOH_CONFIG_OVERRIDE=" + env["ZENOH_CONFIG_OVERRIDE"]]

for index, mode in enumerate(args.cases):
    case = args.output / f"{index}-{mode}"
    case.mkdir()
    dual = mode.startswith("dual-")
    mode = mode.removeprefix("dual-")
    monitors = []
    dual_stop = threading.Event()
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

    def monitor_rss(pids, stop, filename="rss.csv"):
        with (case/filename).open("w", buffering=1) as output:
            writer = csv.writer(output)
            writer.writerow(("time_ns","combined_rss_kib"))
            while not stop.is_set():
                try:
                    readings = [re.search(r"^VmRSS:\s+(\d+)", Path(f"/proc/{pid}/status").read_text(), re.M)
                                for pid in pids]
                except (FileNotFoundError, ProcessLookupError):
                    return
                if not all(readings):
                    return
                writer.writerow((time.time_ns(),sum(int(m[1]) for m in readings)))
                stop.wait(.05)

    def monitor_gateway(pid, stop):
        group = Path("/sys/fs/cgroup") / Path(f"/proc/{pid}/cgroup").read_text().strip().split("::")[1].lstrip("/")
        with (case/"gateway-usage.csv").open("w", buffering=1) as stream:
            writer=csv.writer(stream)
            writer.writerow(("ns","cpu_usec","rss_kib","processes","native_scan_ticks","network_scan_ticks"))
            while not stop.is_set():
                cpu=int(dict(line.split() for line in (group/"cpu.stat").read_text().splitlines())["usage_usec"])
                rss=[]
                scan_ticks={"native":0, "network":0}
                for item in (group/"cgroup.procs").read_text().split():
                    try:
                        proc=Path("/proc",item)
                        argv=(proc/"cmdline").read_bytes().split(b"\0")
                        if len(argv)>2 and argv[1].endswith(b"/gateway_discovery.py"):
                            role=argv[2].decode()
                            stat=(proc/"stat").read_text().rsplit(")",1)[1].split()
                            scan_ticks[role] += int(stat[11])+int(stat[12])
                        m=re.search(r"^VmRSS:\s+(\d+)",Path("/proc",item,"status").read_text(),re.M)
                    except (FileNotFoundError,ProcessLookupError):
                        continue
                    if m: rss.append(int(m[1]))
                writer.writerow((time.time_ns(),cpu,sum(rss),len(rss),scan_ticks["native"],scan_ticks["network"]))
                stop.wait(.25)

    def start(name, cmd):
        with (case/(name+".log")).open("x") as log:
            p = sp.Popen(cmd, env=(dict(run_env, BUFFER_RUNTIME_ONLY="0") if args.churn and name=="gateway" else run_env), stdout=log, stderr=sp.STDOUT)
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
                 if mode == "ingress" or name == "import" else ["export","sensor_msgs/msg/PointCloud2","/output","/manual/cloud"])
        if args.discovery_control or args.churn:
            config = case/"gateway.json"
            exports=[dict(topic="/output", remap_to="/manual/cloud", type="sensor_msgs/msg/PointCloud2")]
            if args.churn == "configured-matched":
                exports.extend(dict(topic="/churn/active"+str(i), type="std_msgs/msg/Int32",
                    qos=dict(input_reliability="reliable", output_reliability="reliable")) for i in (1,2))
            config.write_text(json.dumps(dict(dynamic_discovery=args.churn in (None,"discovered"),
                network_rmw=env.get("GATEWAY_BENCH_RMW","rmw_cyclonedds_cpp"),
                heaphook="/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so",
                exports=exports)))
            route = ["--config", str(config)]
        p = start(name,host+["gateway",str(case/(name+".pid")),*route])
        owned_gateways[name] = p
        wait(lambda:contains(name,"ready role=source") and contains(name,"ready role=destination"), p)
        if dual:
            pids = re.findall(r"ready role=.* pid=(\d+)", (case/(name+".log")).read_text())
            pids.append((case/(name+".pid")).read_text().strip())
            monitor = threading.Thread(target=monitor_rss,args=(pids,dual_stop,name+"-rss.csv"),daemon=True)
            monitors.append(monitor)
            monitor.start()
        return p

    def stop_gateway(name,p):
        pid=int((case/(name+".pid")).read_text())
        os.kill(pid,signal.SIGINT)
        done(p)

    def start_churn_clock(graph):
        wait(lambda:(case/"pipeline"/"sink-ready").exists(),graph,30)
        tmp = case/"clock.tmp"
        tmp.write_text(str(time.time_ns()))
        tmp.rename(case/"churn-start")

    try:
        if args.churn:
            churn_workers=[]
            for role in ("native","network"):
                proc=start("churn-"+role,host+["churn",str(case),role,args.churn])
                churn_workers.append(proc)
                wait(lambda:(case/("ready-"+role)).exists(),proc)
            if args.churn == "native":
                graph=start("graph",host+["sustained",str(case/"pipeline")])
                start_churn_clock(graph)
                assert graph.wait(timeout=90)==0
                assert contains("graph","bag=600/600")
                assert contains("graph","steady_tracks=596/596")
                for worker in churn_workers:
                    done(worker)
                continue
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
        if dual:
            reader=start("reader",host+["ingress-reader","240"])
            wait(lambda:contains("reader","reader ready"),reader)
            incoming=gateway("import")
            peer_script = bag + "-poses.py"
            sp.run(["docker","cp","experiments/rmw-agnocast/network-peer/poses.py",peer+":"+peer_script],check=True)
            poses=start("poses",remote+["bash","-ec",'touch /tmp/poses-go; exec python3 "$1" 240',"bash",peer_script])
            wait(lambda:contains("reader","pose 0 "),reader)
        stop_monitor = threading.Event()
        if pressure:
            pids = re.findall(r"ready role=.* pid=(\d+)",(case/"gateway.log").read_text())
            if not args.churn:
                assert len(pids) == 2
            pids.append((case/"gateway.pid").read_text().strip())
            if args.churn:
                monitor = threading.Thread(target=monitor_gateway,args=(pids[-1],stop_monitor),daemon=True)
            else:
                monitor = threading.Thread(target=monitor_rss,args=(pids,stop_monitor),daemon=True)
            monitor.start()
        if sustained:
            graph=start("graph",host+["sustained",str(case/"pipeline")])
            if args.churn:
                start_churn_clock(graph)
            else:
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
            if dual:
                (case/"fault-start").write_text(str(time.time_ns()))
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
            if dual:
                wait(lambda:contains("replacement","source armed"),relay)
                (case/"fault-end").write_text(str(time.time_ns()))
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
            assert len((case/("gateway-usage.csv" if args.churn else "rss.csv")).read_text().splitlines()) > 40
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
        if dual:
            done(poses)
            done(reader)
            stop_gateway("import",incoming)
            if mode != "active":
                times = [int(t) for t in re.findall(r"^pose \d+ (\d+)",(case/"reader.log").read_text(),re.M)]
                a,b = (int((case/("fault-"+edge)).read_text()) for edge in ("start","end"))
                assert times[0]<a and times[-1]>b and any(a<t<b for t in times)
            print("import verified 240/240",flush=True)
        done(start("verify",host+["verify",str(case)]))
        if sustained:
            if args.churn:
                for worker in churn_workers:
                    done(worker)
                assert len((case/"peer-sequences").read_text().split())==600
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
        dual_stop.set()
        for monitor in monitors:
            monitor.join()
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
