"""Exchange native endpoint inventories on the gateway network; reconcile same-name routes."""
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time

CONTROL = "/_rmw_agnocast/discovery_v1"
INTERVAL = .5
LEASE = 3.
MAX_PACKET = 65536


def start_workers(children, ends, config, ready, gateway):
    pair = socket.socketpair(type=socket.SOCK_DGRAM)
    ends.extend(pair)
    for native, channel in zip((True, False), pair):
        env = dict(os.environ)
        env.pop("RMW_AGNOCAST_GATEWAY_CONFIG", None)
        env["RMW_IMPLEMENTATION"] = "rmw_agnocast" if native else config["network_rmw"]
        hooks = [h for h in env.get("LD_PRELOAD", "").replace(":", " ").split() if "agnocast_heaphook" not in h]
        env["LD_PRELOAD"] = ":".join(([config["heaphook"]] if native else []) + hooks)
        children.append(subprocess.Popen([sys.executable, __file__, "native" if native else "network",
            json.dumps(config), str(ready), str(os.getpid()), gateway], stdin=channel, env=env,
            pass_fds=(ready,), start_new_session=True))


def snapshot(node, prefix):
    from rclpy.qos import ReliabilityPolicy, DurabilityPolicy
    rows = set()
    for topic, _ in node.get_topic_names_and_types():
        if (not topic.startswith(prefix) or topic in ("/rosout", "/parameter_events", CONTROL)
                or any(part.startswith("_") for part in topic.split("/"))):
            continue
        for role, lookup in (("pub", node.get_publishers_info_by_topic),
                             ("sub", node.get_subscriptions_info_by_topic)):
            for info in lookup(topic):
                qos = info.qos_profile
                if (info.node_name.startswith(("manual_gateway_", "gateway_discovery_"))
                        or qos.durability != DurabilityPolicy.VOLATILE
                        or qos.reliability not in (ReliabilityPolicy.RELIABLE, ReliabilityPolicy.BEST_EFFORT)):
                    continue
                rows.add((topic, info.topic_type, str(info.topic_type_hash), role, qos.reliability.name.lower()))
    return sorted(rows)


def wanted_routes(local, peers, fixed):
    # Require matching type identity and compatible reliability. Refuse ambiguous directions or types.
    remote = {tuple(row) for _, _, rows in peers.values() for row in rows}
    types = {}
    for topic, cls, identity, _, _ in [*local, *remote]:
        types.setdefault(topic,set()).add((cls,identity))
    candidates = {}
    for topic, cls, identity, role, qos in local:
        if topic in fixed or len(types[topic]) != 1:
            continue
        for name, other_cls, other_id, other_role, other_qos in remote:
            if (topic, cls, identity) != (name, other_cls, other_id) or role == other_role:
                continue
            writer, reader = (qos, other_qos) if role == "pub" else (other_qos, qos)
            if writer == "best_effort" and reader == "reliable":
                continue
            candidates.setdefault(topic, set()).add(("export" if role=="pub" else "import", cls, writer))
    return {topic: next(iter(options)) for topic, options in candidates.items() if len(options)==1}


def network_loop(node, channel, config, gateway, ready):
    import rclpy
    from std_msgs.msg import String
    island = Path("/proc/sys/kernel/random/boot_id").read_text().strip()+":"+str(os.stat("/proc/self/ns/ipc").st_ino)
    generation = time.monotonic_ns()
    pub = node.create_publisher(String, CONTROL, 10)
    peers, active, retry = {}, {}, {}
    def receive(msg):
        if len(msg.data)>MAX_PACKET:
            return
        try:
            origin, epoch, rows = json.loads(msg.data)
            if (isinstance(origin, str) and origin != island and isinstance(rows, list)
                    and type(epoch) is int and epoch >= peers.get(origin,(0,0,()))[1]
                    and all(isinstance(row, list) and len(row)==5 and all(isinstance(v,str) for v in row)
                            and row[3] in ("pub","sub") and row[4] in ("reliable","best_effort") for row in rows)):
                peers[origin] = (time.monotonic(), epoch, rows)
        except (ValueError, TypeError):
            return
    node.create_subscription(String, CONTROL, receive, 10)
    local = []
    def reconcile():
        nonlocal local
        while True:
            try:
                local = json.loads(channel.recv(MAX_PACKET))
            except BlockingIOError:
                break
        now = time.monotonic()
        pub.publish(String(data=json.dumps([island,generation,local],separators=(",",":"))))
        for key in list(peers):
            if now-peers[key][0]>LEASE:
                del peers[key]
        wanted = wanted_routes(local, peers, config["fixed"])
        for topic, (spec, proc) in list(active.items()):
            if proc.poll() is not None or wanted.get(topic)!=spec:
                if proc.poll() is None:
                    proc.terminate()
                proc.wait(timeout=8)
                del active[topic]
                retry[topic] = now+1
        for topic, spec in wanted.items():
            if topic in active or now < retry.get(topic,0):
                continue
            direction, cls, reliability = spec
            words=[sys.executable, gateway, direction, cls, topic, topic, "--network-rmw",config["network_rmw"],
                   "--heaphook",config["heaphook"], "--input-reliability",reliability,
                   "--output-reliability",reliability, "--depth",str(config.get("qos",{}).get("depth",10)),
                   "--parent-pid",str(os.getpid())]
            active[topic]=(spec,subprocess.Popen(words))
            print("discovered",direction,topic,cls,flush=True)
    node.create_timer(INTERVAL,reconcile)
    os.write(ready,b"1")
    try:
        rclpy.spin(node)
    finally:
        for _, proc in active.values():
            proc.terminate()
        for _, proc in active.values():
            proc.wait(timeout=8)


if __name__ == "__main__":
    role, raw, ready, parent, gateway = sys.argv[1:]
    import ctypes
    if ctypes.CDLL(None).prctl(1,signal.SIGTERM,0,0,0) or os.getppid()!=int(parent):
        sys.exit(1)
    import rclpy
    from rclpy.executors import ExternalShutdownException
    config=json.loads(raw)
    channel=socket.socket(fileno=0)
    channel.setblocking(False)
    rclpy.init(args=[])
    node=rclpy.create_node("gateway_discovery_"+role)
    try:
        if role=="native":
            prefix=config.get("discovery_namespace","/").rstrip("/")+"/"
            def scan():
                data=json.dumps(snapshot(node,prefix),separators=(",",":")).encode()
                if len(data)>MAX_PACKET-256:
                    raise RuntimeError("native discovery snapshot exceeds size limit")
                try:
                    channel.send(data)
                except BlockingIOError:
                    pass
            node.create_timer(INTERVAL,scan)
            os.write(int(ready),b"1")
            rclpy.spin(node)
        else:
            network_loop(node,channel,config,gateway,int(ready))
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()
