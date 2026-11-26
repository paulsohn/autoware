"""Same topic on two domains, two IPC islands, and no configured routes."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time

TOPIC = "/domain_probe/data"
CONTROL = "/_rmw_agnocast/discovery_v2"
DOMAINS = (17, 18)


def worker(role, domain, override):
    import rclpy
    from rmw_agnocast_discovery_msgs.msg import IslandSnapshot
    from std_msgs.msg import Int32

    rclpy.init(domain_id=domain if override else None)
    node = rclpy.create_node(f"domain_probe_{role}_{domain}")
    assert node.context.get_domain_id() == domain
    received = []
    markers = set()
    marker = f"/domain_probe/marker_{domain}"
    if role == "writer":
        ep = node.create_publisher(Int32, TOPIC, 32)
        node.create_publisher(Int32, marker, 10)
    elif role == "observer":
        def observe(msg):
            for e in msg.endpoints:
                if e.topic_name.startswith("/domain_probe/marker_"):
                    markers.add(e.topic_name)
        ep = node.create_subscription(IslandSnapshot, CONTROL, observe, 10)
    else:
        ep = node.create_subscription(Int32, TOPIC, lambda msg: received.append(msg.data), 32)
    deadline = time.monotonic()+55
    sent = False
    name = f"/tmp/{role}-{domain}"
    while not Path("/tmp/finish").exists():
        assert time.monotonic()<deadline, name
        rclpy.spin_once(node, timeout_sec=.05)
        if role == "writer" and not sent and Path("/tmp/send").exists():
            for seq in range(20):
                ep.publish(Int32(data=domain*100+seq))
                time.sleep(.04)
            sent = True
        count = ep.get_subscription_count() if role=="writer" else ep.get_publisher_count()
        state = dict(env=int(os.environ["ROS_DOMAIN_ID"]),context=node.context.get_domain_id(),
                     matches=count,received=received,markers=sorted(markers),sent=sent)
        path = Path(name+".new")
        path.write_text(json.dumps(state))
        path.replace(name+".json")
    node.destroy_node()
    rclpy.shutdown()


def exercise(backend, islands, run, docker, hook, out, mode):
    assert mode in ("inherited","inherited-manual","writer-override","reader-override")
    rmw = "rmw_cyclonedds_cpp" if backend=="cyclone" else "rmw_zenoh_cpp"
    config = dict(dynamic_discovery=True,network_rmw=rmw,heaphook=hook,discovery_namespace="/domain_probe")
    for c in islands:
        subprocess.run(["docker","exec","-i",c,"tee","/tmp/domain.json"],input=json.dumps(config),
                       text=True,check=True,stdout=subprocess.DEVNULL)
    exe = "/ws/build/manual-gateway/install/agnocast_cyclone_gateway/lib/agnocast_cyclone_gateway/topic_gateway"
    manual = mode == "inherited-manual"
    gateways = []
    if manual:
        for d in DOMAINS:
            for i in range(len(islands)):
                gateways.append(run(i, f"manual-{i}-{d}", exe+" --config /tmp/domain.json",
                                    environment=("-e",f"ROS_DOMAIN_ID={d}")))
    procs = {}
    def start(role,d):
        i = int(role in ("remote","observer"))
        override = d==18 and ((mode=="writer-override" and role in ("writer", "paired")) or
                              (mode=="reader-override" and role=="remote"))
        env = ["-e","ROS_DOMAIN_ID="+str(17 if override else d)]
        if not manual and role in ("writer","remote"):
            env.extend(["-e","RMW_AGNOCAST_GATEWAY_CONFIG=/tmp/domain.json","-e",
                        "AGNOCAST_GATEWAY_EXECUTABLE="+exe])
        if role=="observer":
            env.extend(["-e","RMW_IMPLEMENTATION="+rmw])
        cmd=f"python3 /ws/experiments/rmw-agnocast/islands/domains.py {role} {d} {int(override)}"
        procs[role,d] = run(i,f"{role}-{d}",cmd,native=role!="observer",environment=env)
    for d in DOMAINS:
        for role in ("observer","local","remote","writer"):
            start(role,d)
    if mode=="writer-override":
        start("paired",18)

    def read():
        states={}
        for i,c in enumerate(islands):
            keys=[(role,d) for role,d in procs if int(role in ("observer","remote"))==i]
            paths=[f"/tmp/{role}-{d}.json" for role,d in keys]
            data=docker("exec",c,"sh","-c","for f in "+" ".join(paths)+
                        '; do if test -s "$f"; then cat "$f"; else echo -n "{}"; fi; echo; done')
            states.update(zip(keys,map(json.loads,data.splitlines()),strict=True))
        assert all(p.poll() is None for p in [*procs.values(),*gateways]), "worker exited (see logs)"
        return states

    until=time.monotonic()+20
    while time.monotonic()<until:
        state=read()
        if (("paired",18) not in state or state["paired",18].get("matches",0)>=1) and all(state["observer",d].get("markers")==[f"/domain_probe/marker_{d}"] and
               state["remote",d].get("matches",0)>=1 and state["writer",d].get("matches",0)>=2
               for d in DOMAINS):
            break
        time.sleep(.25)
    assert all(state.values()), "application setup incomplete"
    # Send even when a domain override prevents a match; record the observed failure.
    docker("exec",islands[0],"touch","/tmp/send")
    until=time.monotonic()+6
    while time.monotonic()<until:
        state=read()
        if all(len(state[r,d].get("received",[]))>=20 for d in DOMAINS for r in ("local","remote")):
            break
        time.sleep(.1)
    time.sleep(.5)
    state=read()
    evidence={f"{r}-{d}":v for (r,d),v in state.items()}
    (out/f"{backend}-domains.json").write_text(json.dumps(evidence,indent=2))
    for c in islands:
        docker("exec",c,"touch","/tmp/finish")
    for proc in procs.values():
        assert proc.wait(timeout=15)==0
    for d in DOMAINS:
        assert state["writer",d]["sent"]
        expected=list(range(d*100,d*100+20))
        assert state["observer",d]["markers"]==[f"/domain_probe/marker_{d}"]
        for role in ("local","remote"):
            values=state[role,d]["received"]
            print(backend,mode,d,role,"matched",state[role,d]["matches"],"received",len(values),flush=True)
            assert all(v in expected for v in values), "cross-domain delivery"
            assert values in ([], expected), "partial, duplicated or reordered sequence"
            if mode.startswith("inherited") or d==17 or (mode=="reader-override" and role=="local"):
                assert values==expected
    if mode=="writer-override":
        assert state["paired",18]["received"]==list(range(1800,1820))
        print(backend, mode, "paired native reader 20/20", flush=True)


if __name__=="__main__":
    worker(sys.argv[1],int(sys.argv[2]),bool(int(sys.argv[3])))
