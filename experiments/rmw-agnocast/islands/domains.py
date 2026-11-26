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


def check_rejection(domain):
    import rclpy
    from rclpy.context import Context
    from rclpy.executors import SingleThreadedExecutor
    from std_msgs.msg import Int32

    native = int(os.environ["ROS_DOMAIN_ID"])
    config = os.environ.pop("RMW_AGNOCAST_GATEWAY_CONFIG")
    original = Context()
    rclpy.init(context=original, domain_id=native)
    writer = rclpy.create_node("domain_guard_writer", context=original)
    pub = writer.create_publisher(Int32, "/domain_guard", 10)

    # Record any gateway launch attempt. The rejected context must fail before joining.
    marker = Path("/tmp/rejected-gateway-started")
    probe = Path("/tmp/domain-guard-gateway")
    probe.write_text(f"#!/bin/sh\ntouch {marker}\nexit 1\n")
    probe.chmod(0o700)
    os.environ["AGNOCAST_GATEWAY_EXECUTABLE"] = str(probe)
    os.environ["RMW_AGNOCAST_GATEWAY_CONFIG"] = config
    expected = f"requested ROS domain {domain}, but this process is registered with Agnocast in domain {native}"
    for environment in (native, domain):
        os.environ["ROS_DOMAIN_ID"] = str(environment)
        rejected = Context()
        try:
            rclpy.init(context=rejected, domain_id=domain)
        except RuntimeError as exc:
            assert expected in str(exc), str(exc)
        else:
            raise AssertionError("mismatched context initialized")
        assert not marker.exists()
    del os.environ["RMW_AGNOCAST_GATEWAY_CONFIG"]

    # Even with the mutated environment, a second explicitly aligned context is supported.
    second = Context()
    rclpy.init(context=second, domain_id=native)
    reader = rclpy.create_node("domain_guard_reader", context=second)
    received = []
    reader.create_subscription(Int32, "/domain_guard", lambda msg: received.append(msg.data), 10)
    executor = SingleThreadedExecutor(context=second)
    executor.add_node(reader)
    deadline = time.monotonic()+5
    while not pub.get_subscription_count():
        assert time.monotonic()<deadline
        time.sleep(.02)
    for value in range(10):
        pub.publish(Int32(data=value))
        while len(received)<=value:
            assert time.monotonic()<deadline, received
            executor.spin_once(timeout_sec=.05)
    assert received == list(range(10))
    executor.shutdown()
    reader.destroy_node()
    second.shutdown()
    writer.destroy_node()
    original.shutdown()
    print(expected, "twice; no gateway attempt; aligned contexts delivered 10/10", flush=True)


def worker(role, domain, override):
    import rclpy
    from rmw_agnocast_discovery_msgs.msg import IslandSnapshot
    from std_msgs.msg import Int32

    if override:
        check_rejection(domain)
        return
    rclpy.init()
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
        override = (d==18 and ((mode=="writer-override" and role=="writer") or
                               (mode=="reader-override" and role=="remote")))
        env = ["-e","ROS_DOMAIN_ID="+str(17 if override else d)]
        if not manual and role in ("writer","remote"):
            env.extend(["-e","RMW_AGNOCAST_GATEWAY_CONFIG=/tmp/domain.json","-e",
                        "AGNOCAST_GATEWAY_EXECUTABLE="+exe])
        if role=="observer":
            env.extend(["-e","RMW_IMPLEMENTATION="+rmw])
        cmd=f"python3 /ws/experiments/rmw-agnocast/islands/domains.py {role} {d} {int(override)}"
        proc = run(i,f"{role}-{d}",cmd,native=role!="observer",environment=env)
        if override:
            assert proc.wait(timeout=15)==0, "domain guard probe failed; see log"
        else:
            procs[role,d] = proc
    for d in DOMAINS:
        for role in ("observer","local","remote","writer"):
            start(role,d)

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

    active = DOMAINS if mode.startswith("inherited") else (17,)
    until=time.monotonic()+20
    while time.monotonic()<until:
        state=read()
        if all(state.values()) and all(
            state["observer",d]["markers"]==[f"/domain_probe/marker_{d}"] and
            state["remote",d]["matches"]>=1 and state["writer",d]["matches"]>=2
            for d in active):
            break
        time.sleep(.25)
    else:
        raise TimeoutError("domain peers did not match")
    docker("exec",islands[0],"touch","/tmp/send")
    until=time.monotonic()+6
    while time.monotonic()<until:
        state=read()
        if all(len(state[r,d].get("received",[]))>=20 for d in active for r in ("local","remote")):
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
    for d in active:
        assert state["writer",d]["sent"]
        assert state["observer",d]["markers"]==[f"/domain_probe/marker_{d}"]
        for role in ("local","remote"):
            assert state[role,d]["received"]==list(range(d*100,d*100+20))
    for (role,d), row in state.items():
        if role=="observer":
            assert set(row["markers"]) <= {f"/domain_probe/marker_{d}"}
        elif role in ("local","remote"):
            assert all(v//100==d for v in row["received"])
    print(backend, mode, "passed", flush=True)


if __name__=="__main__":
    worker(sys.argv[1],int(sys.argv[2]),bool(int(sys.argv[3])))
