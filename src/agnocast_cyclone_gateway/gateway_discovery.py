"""Exchange native endpoint inventories on the gateway network; reconcile same-name routes."""
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time

CONTROL = "/_rmw_agnocast/discovery_v2"
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
    from rmw_agnocast_discovery_msgs.msg import DiscoveryEndpoint
    from rosgraph_msgs.msg import TypeHash
    rows = set()
    for topic, _ in node.get_topic_names_and_types():
        if (not topic.startswith(prefix) or topic in ("/rosout", "/parameter_events", CONTROL)
                or any(part.startswith("_") for part in topic.split("/"))):
            continue
        for role, lookup in ((DiscoveryEndpoint.PUBLISHER, node.get_publishers_info_by_topic),
                             (DiscoveryEndpoint.SUBSCRIPTION, node.get_subscriptions_info_by_topic)):
            for info in lookup(topic):
                qos = info.qos_profile
                if (info.node_name.startswith(("manual_gateway_", "gateway_discovery_"))
                        or qos.durability != DurabilityPolicy.VOLATILE
                        or qos.reliability not in (ReliabilityPolicy.RELIABLE, ReliabilityPolicy.BEST_EFFORT)):
                    continue
                digest = info.topic_type_hash
                reliability = (DiscoveryEndpoint.RELIABLE if qos.reliability == ReliabilityPolicy.RELIABLE
                               else DiscoveryEndpoint.BEST_EFFORT)
                rows.add((topic, info.topic_type, max(0, digest.version), digest.value, role, reliability))
    return [DiscoveryEndpoint(topic_name=topic, type_name=cls,
                type_hash=TypeHash(version=version, value=list(value)), role=role, reliability=qos)
            for topic, cls, version, value, role, qos in sorted(rows)]


def endpoint_rows(endpoints):
    from rmw_agnocast_discovery_msgs.msg import DiscoveryEndpoint
    roles = {DiscoveryEndpoint.PUBLISHER: "pub", DiscoveryEndpoint.SUBSCRIPTION: "sub"}
    policies = {DiscoveryEndpoint.RELIABLE: "reliable", DiscoveryEndpoint.BEST_EFFORT: "best_effort"}
    return [(ep.topic_name, ep.type_name, (ep.type_hash.version, bytes(ep.type_hash.value)),
             roles[ep.role], policies[ep.reliability]) for ep in endpoints]


def wanted_routes(local, remote, fixed):
    # Require matching type identity and compatible reliability. Refuse ambiguous directions or types.
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
    from rclpy.serialization import deserialize_message
    from rmw_agnocast_discovery_msgs.msg import IslandSnapshot
    island = (Path("/proc/sys/kernel/random/boot_id").read_text().strip(), os.stat("/proc/self/ns/ipc").st_ino)
    pub = node.create_publisher(IslandSnapshot, CONTROL, 10)
    peers, active, retry = {}, {}, {}
    def receive(payload):
        if len(payload) > MAX_PACKET:
            return
        try:
            message = deserialize_message(payload, IslandSnapshot)
        except RuntimeError:
            return  # An unreadable announcement must not end discovery for other islands.
        origin = (message.host_uuid, message.ipc_ns_inode)
        if origin == island or message.generation < peers.get(origin, (0, 0, ()))[1]:
            return
        try:
            rows = endpoint_rows(message.endpoints)
        except KeyError:
            return  # Unknown role or reliability in this protocol version.
        peers[origin] = (time.monotonic(), message.generation, rows)
    node.create_subscription(IslandSnapshot, CONTROL, receive, 10, raw=True)
    local = []
    announcement = None
    def reconcile():
        nonlocal local, announcement
        while True:
            try:
                payload = channel.recv(MAX_PACKET)
            except BlockingIOError:
                break
            announcement = deserialize_message(payload, IslandSnapshot)
            local = endpoint_rows(announcement.endpoints)
        now = time.monotonic()
        if announcement is not None:
            pub.publish(announcement)
        for key in list(peers):
            if now-peers[key][0]>LEASE:
                del peers[key]
        remote = {row for _, _, rows in peers.values() for row in rows}
        prefix = config.get("discovery_namespace", "/").rstrip("/") + "/"
        remote.update(endpoint_rows(snapshot(node, prefix)))
        wanted = wanted_routes(local, remote, config["fixed"])
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
            from rclpy.serialization import serialize_message
            from rmw_agnocast_discovery_msgs.msg import IslandSnapshot
            message = IslandSnapshot(host_uuid=Path("/proc/sys/kernel/random/boot_id").read_text().strip(),
                                     ipc_ns_inode=os.stat("/proc/self/ns/ipc").st_ino,
                                     generation=time.monotonic_ns())
            def scan():
                message.endpoints = snapshot(node, prefix)
                data = serialize_message(message)
                if len(data)>MAX_PACKET:
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
