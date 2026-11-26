#!/usr/bin/env python3
"""One manually owned, directed ROS topic route between two process-local RMWs."""
from gateway_config import configured_routes

import argparse
import ctypes
import fcntl
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
from contextlib import ExitStack

NATIVE = "rmw_agnocast"
CYCLONE = "rmw_cyclonedds_cpp"
ZENOH = "rmw_zenoh_cpp"
HEADER = struct.Struct("!I")
MAX_BYTES = 64 * 1024 * 1024


def follow_parent(pid, signum):
    # An unexpected supervisor death must also release both ROS endpoints.
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(1, signum, 0, 0, 0) != 0:  # Linux PR_SET_PDEATHSIG
        raise OSError(ctypes.get_errno(), "prctl(PR_SET_PDEATHSIG)")
    if os.getppid() != pid:
        raise RuntimeError("gateway supervisor disappeared during startup")


def run_endpoint(args, role):
    follow_parent(args.parent_pid, signal.SIGKILL)
    import rclpy
    from rclpy.executors import ExternalShutdownException
    from rclpy.qos import QoSProfile, ReliabilityPolicy
    from rclpy.utilities import get_rmw_implementation_identifier
    from rosidl_runtime_py.utilities import get_message

    inferred = args.type == "?"
    cls = None if inferred else get_message(args.type)
    channel = socket.socket(fileno=0)
    rx = role == "source"
    exporting = rx and args.direction == "export"
    if rx and not exporting:
        channel.settimeout(3)
    topic = args.source if rx else args.destination
    reliability = args.input_reliability if rx else args.output_reliability
    qos = QoSProfile(depth=args.depth, reliability=ReliabilityPolicy[reliability.upper()])
    rclpy.init(args=[])
    node = rclpy.create_node(f"manual_gateway_{role}_{os.getpid()}")
    expected = os.environ["RMW_IMPLEMENTATION"]
    assert get_rmw_implementation_identifier() == expected
    if expected == NATIVE and "libddsc.so" in Path("/proc/self/maps").read_text():
        raise RuntimeError("native endpoint loaded Cyclone")
    count = byte_count = 0
    cpu_start = time.process_time()
    sub = None
    thread = None
    errors = []
    stopping = threading.Event()

    pending = None
    changed = threading.Condition()
    dropped = taken = 0

    def transmit(data):
        nonlocal count, byte_count
        channel.sendall(HEADER.pack(len(data)))
        channel.sendall(data)
        count += 1
        byte_count += len(data)

    def forward(data):
        nonlocal pending, dropped, taken
        if not 0 < len(data) <= MAX_BYTES:
            raise ValueError("serialized message outside gateway frame limit")
        taken += 1
        if not exporting:
            transmit(data)
            return
        # Keep the most recent waiting cloud while a DDS write is blocked.
        with changed:
            dropped += int(pending is not None)
            pending = data
            changed.notify()

    def send_pending():
        nonlocal pending
        try:
            while True:
                with changed:
                    changed.wait_for(lambda: stopping.is_set() or pending is not None)
                    if stopping.is_set():
                        return
                    data, pending = pending, None
                transmit(data)
                del data
        except OSError as exc:
            if not stopping.is_set():
                errors.append(exc)
                guard.trigger()

    def activate(active):
        nonlocal sub
        if active and sub is None:
            sub = node.create_subscription(cls, topic, forward, qos, raw=True)
            print(f"source armed topic={topic} taken={taken}", flush=True)
        elif not active and sub is not None:
            node.destroy_subscription(sub)
            sub = None
            print(f"source idle topic={topic} taken={taken}", flush=True)

    def demand():
        if select.select([channel], [], [], 0)[0]:
            value = channel.recv(256)
            if not value:
                raise EOFError("destination disconnected")
            activate(value[-1:] == b"1")

    def receive():
        nonlocal count, byte_count
        try:
            with channel.makefile("rb") as stream:
                while not stopping.is_set():
                    header = stream.read(HEADER.size)
                    if len(header) != HEADER.size:
                        raise EOFError("source disconnected")
                    size, = HEADER.unpack(header)
                    if not 0 < size <= MAX_BYTES:
                        raise ValueError("invalid gateway frame length")
                    data = stream.read(size)
                    if len(data) != size:
                        raise EOFError("incomplete serialized message")
                    pub.publish(data)
                    count += 1
                    byte_count += size
        except Exception as exc:
            if not stopping.is_set():
                errors.append(exc)
                guard.trigger()

    def report_error():
        if errors:
            raise errors[0]

    matched = None
    last_demand = 0.0

    def advertise_demand():
        nonlocal matched, last_demand
        now = time.monotonic()
        active = pub.get_subscription_count() > 0
        if active:
            last_demand = now
        elif matched and now - last_demand < .25:
            return
        if active != matched:
            channel.sendall(b"1" if active else b"0")
            matched = active
            print(f"destination demand={int(active)} topic={topic}", flush=True)

    def signal_ready():
        fd = int(os.environ.pop("AGNOCAST_ENDPOINT_READY_FD"))
        os.write(fd,b"+")
        os.close(fd)

    def discover():
        # Initialization must finish even when the initializing application is the first publisher.
        signal_ready()
        if rx:
            previous = None
            while rclpy.ok():
                types = sorted({info.topic_type for info in node.get_publishers_info_by_topic(topic)})
                if len(types) == 1:
                    encoded = types[0].encode()
                    channel.sendall(HEADER.pack(len(encoded)) + encoded)
                    return types[0]
                if previous != types:
                    print(f"waiting for one publisher type on {topic}: {types}", flush=True)
                    previous = types
                rclpy.spin_once(node, timeout_sec=.1)
        else:
            while rclpy.ok():
                if select.select([channel], [], [], .1)[0]:
                    header = channel.recv(HEADER.size, socket.MSG_WAITALL)
                    if len(header) != HEADER.size:
                        raise EOFError("source closed before type discovery")
                    size, = HEADER.unpack(header)
                    if not 0 < size <= 65536:
                        raise ValueError("invalid type name length")
                    data = channel.recv(size, socket.MSG_WAITALL)
                    if len(data) != size:
                        raise EOFError("incomplete type name")
                    return data.decode()

    try:
        if inferred:
            args.type = discover()
            if args.type is None:
                return
            cls = get_message(args.type)
        lazy = args.direction == "export" and not args.always_on
        if rx:
            if exporting:
                guard = node.create_guard_condition(report_error)
                thread = threading.Thread(target=send_pending)
                thread.start()
            if lazy:
                node.create_timer(.05, demand)
            else:
                activate(True)
        else:
            pub = node.create_publisher(cls, topic, qos)
            guard = node.create_guard_condition(report_error)
            thread = threading.Thread(target=receive)
            thread.start()
            if lazy:
                node.create_timer(.05, advertise_demand)
        print(f"ready role={role} rmw={expected} type={args.type} pid={os.getpid()}", flush=True)
        if not inferred:
            signal_ready()
        rclpy.spin(node)
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        with changed:
            stopping.set()
            changed.notify()
        channel.shutdown(socket.SHUT_RDWR)
        if thread:
            thread.join()
        channel.close()
        print(f"stats role={role} samples={count} bytes={byte_count} cpu_s={time.process_time()-cpu_start:.6f}", flush=True)
        if exporting:
            print(f"export overwritten={dropped}", flush=True)
        node.destroy_node()
        rclpy.try_shutdown()


def route_directory():
    directory = Path(f"/dev/shm/agnocast-gateway-{os.getuid()}")
    directory.mkdir(mode=0o700, exist_ok=True)
    info = directory.stat()
    if directory.is_symlink() or info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise RuntimeError(f"expected private route directory {directory}")
    return directory


def acquire_route(stack, endpoints):
    directory = route_directory()
    domain = int(os.environ.get("ROS_DOMAIN_ID", "0"))
    for index, (rmw, topic) in enumerate(endpoints):
        key = hashlib.sha256(f"{domain}/{rmw}/{topic}".encode()).hexdigest()
        lock = stack.enter_context((directory / key).open("a"))
        try:
            fcntl.flock(lock, (fcntl.LOCK_EX if index else fcntl.LOCK_SH) | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError(f"conflicting route for {topic} on {rmw}") from None


def control_address(args):
    network_config = (["CYCLONEDDS_URI"] if args.network_rmw == CYCLONE else
                      ["ZENOH_SESSION_CONFIG_URI","ZENOH_CONFIG_OVERRIDE","ROS_AUTOMATIC_DISCOVERY_RANGE","ROS_STATIC_PEERS"])
    settings = [os.getuid(), int(os.environ.get("ROS_DOMAIN_ID", "0")), os.stat("/dev/agnocast").st_rdev,
                os.stat("/dev/shm").st_dev, args.network_rmw, [os.environ.get(key) for key in network_config],
                args.direction, args.type, args.source, args.destination, args.depth,
                args.input_reliability, args.output_reliability, args.always_on, str(args.heaphook.resolve(strict=True))]
    return "\0agnocast-" + hashlib.sha256(json.dumps(settings).encode()).hexdigest()


def lock_before_start(file, operation):
    deadline = time.monotonic()+8
    while True:
        try:
            fcntl.flock(file, operation | fcntl.LOCK_NB)
            break
        except BlockingIOError:
            if time.monotonic() >= deadline:
                raise TimeoutError("gateway startup lock timed out")
            time.sleep(.03)


def join(address, words):
    # Serialize only bootstrap. The persistent route locks also protect manual starts.
    with (route_directory() / (address[1:]+".lock")).open("a") as lock:
        lock_before_start(lock, fcntl.LOCK_EX)
        deadline = time.monotonic()+8
        child = None
        try:
            while time.monotonic()<deadline:
                with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as client:
                    try:
                        client.connect(address)
                    except ConnectionRefusedError:
                        if child is None:
                            child = subprocess.Popen([sys.executable,__file__,*words],
                                stdin=subprocess.DEVNULL, stdout=sys.stderr, start_new_session=True)
                        elif child.poll() is not None:
                            # A racing manual start can still win the route.
                            if time.monotonic()+1 < deadline:
                                deadline = time.monotonic()+1
                        time.sleep(.03)
                        continue
                    try:
                        creds = client.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12)
                        if struct.unpack("3i",creds)[1] != os.getuid():
                            raise PermissionError("gateway owner belongs to another user")
                        request = b"R" if os.getenv("AGNOCAST_GATEWAY_RECOVERY_LEASE") == "1" else b"+"
                        socket.send_fds(client,[request],[0])
                        client.settimeout(8)
                        ack = client.recv(1)
                    except (BrokenPipeError, ConnectionResetError):
                        continue
                    if ack == b"+":
                        return
                    if not ack:
                        continue
                    raise RuntimeError("owner declined gateway lease")
            raise TimeoutError("compatible gateway unavailable")
        except BaseException:
            if child and child.poll() is None:
                child.terminate()
            raise


def accept_lease(listener,leases,automatic):
    client, _ = listener.accept()
    with client:
        creds = client.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12)
        if struct.unpack("3i",creds)[1] != os.getuid():
            return
        client.settimeout(1)
        try:
            data,fds,_,_ = socket.recv_fds(client,1,1)
        except OSError:
            return
        if not fds:
            return
        lease = socket.socket(fileno=fds[0])
        try:
            lease.sendall(b"A" if automatic and data == b"R" else b"+")
            client.sendall(b"+")
        except OSError:
            lease.close()
            return
        leases.append(lease)


def supervise(routes, address, automatic, discovery=None):
    children = []
    stop = False

    def stopping(*_):
        nonlocal stop
        stop = True

    phase_file = route_directory() / (address[1:]+".phase")
    # The old supervisor holds this lock through endpoint teardown and route-lock release.
    # A healthy owner keeps it unlocked so conflicting launchers fail promptly.
    with phase_file.open("a") as phase, ExitStack() as cleanup:
        lock_before_start(phase, fcntl.LOCK_SH)
        for args, _ in routes:
            stacks = (NATIVE, args.network_rmw) if args.direction == "export" else (args.network_rmw, NATIVE)
            acquire_route(cleanup, zip(stacks, (args.source, args.destination)))
        if discovery:
            domain = os.getenv("ROS_DOMAIN_ID", "0")
            lock = cleanup.enter_context((route_directory() / ("discovery-" + domain)).open("a"))
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                raise SystemExit(f"A discovery gateway already owns ROS domain {domain} in {route_directory()}. "
                                 "Use the running configuration to join from an RMW context, "
                                 "or stop its owner before switching configurations.") from None
        listener = cleanup.enter_context(socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET))
        listener.bind(address)
        listener.listen(16)
        fcntl.flock(phase, fcntl.LOCK_UN)
        print(f"owner pid={os.getpid()} automatic={automatic}",flush=True)
        leases = []
        ends = []
        ready_read,ready_write = os.pipe()
        cleanup.callback(os.close,ready_read)
        signal.signal(signal.SIGINT, stopping)
        signal.signal(signal.SIGTERM, stopping)
        try:
            for args, words in routes:
                stacks = (NATIVE, args.network_rmw) if args.direction == "export" else (args.network_rmw, NATIVE)
                pair = socket.socketpair()
                ends.extend(pair)
                for role, rmw, end in zip(("source", "destination"), stacks, pair):
                    env = dict(os.environ, RMW_IMPLEMENTATION=rmw)
                    env.pop("RMW_AGNOCAST_GATEWAY_CONFIG",None)
                    env["AGNOCAST_ENDPOINT_READY_FD"] = str(ready_write)
                    hooks = [h for h in env.get("LD_PRELOAD", "").replace(":", " ").split()
                             if "agnocast_heaphook" not in h]
                    if rmw == NATIVE:
                        hooks.insert(0, str(args.heaphook.resolve(strict=True)))
                    env["LD_PRELOAD"] = ":".join(hooks)
                    children.append(subprocess.Popen(
                        [sys.executable, __file__, *words, "--endpoint", role, "--parent-pid", str(os.getpid())],
                        stdin=end, env=env, pass_fds=(ready_write,), start_new_session=True))
            if discovery:
                from gateway_discovery import start_workers
                start_workers(children, ends, discovery, ready_write, __file__)
            for end in ends:
                end.close()
            ready = 0
            deadline = time.monotonic()+8
            joined = False
            while not stop:
                for child in children:
                    if child.poll() is not None:
                        raise RuntimeError(f"endpoint exited early (status {child.returncode})")
                if ready<len(children):
                    if select.select([ready_read],[],[],.05)[0]:
                        ready += len(os.read(ready_read,len(children)-ready))
                    elif time.monotonic()>deadline:
                        raise TimeoutError("gateway endpoints failed to become ready")
                    continue
                readable = select.select([listener,*leases],[],[],.05)[0]
                if listener in readable:
                    accept_lease(listener,leases,automatic)
                    joined |= bool(leases)
                for lease in list(leases):
                    if lease in readable:
                        leases.remove(lease)
                        lease.close()
                if automatic and not leases and (joined or time.monotonic()>deadline):
                    break
        finally:
            fcntl.flock(phase, fcntl.LOCK_EX)
            listener.close()
            os.close(ready_write)
            for lease in leases:
                lease.close()
            for end in ends:
                end.close()
            for child in children:
                if child.poll() is None:
                    child.send_signal(signal.SIGINT)
            for child in children:
                try:
                    child.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, epilog="Use --config FILE to launch a deployment of one or more routes.")
    parser.add_argument("direction", choices=("export", "import"))
    parser.add_argument("type", metavar="PACKAGE/msg/TYPE")
    parser.add_argument("source")
    parser.add_argument("destination")
    parser.add_argument("--network-rmw", choices=(CYCLONE, ZENOH), default=CYCLONE)
    parser.add_argument("--heaphook", type=Path, required=True)
    parser.add_argument("--depth", type=int, default=10)
    parser.add_argument("--always-on", action="store_true", help="arm export even without a network reader")
    for side, default in (("input", "best_effort"), ("output", "reliable")):
        parser.add_argument(f"--{side}-reliability", choices=("best_effort", "reliable"), default=default)
    parser.add_argument("--endpoint", choices=("source", "destination"), help=argparse.SUPPRESS)
    parser.add_argument("--parent-pid", type=int, help=argparse.SUPPRESS)
    parser.add_argument("--automatic",action="store_true",help=argparse.SUPPRESS)
    words = sys.argv[1:]
    joining = words[:1] == ["--join"]
    automatic = False
    discovery = None
    if joining or words[:1] == ["--config"]:
        if joining:
            config, domain = words[1:]
            os.environ["ROS_DOMAIN_ID"] = str(int(domain))
        elif len(words) == 2:
            config = words[1]
        else:
            parser.error("--config requires one route file")
        try:
            deployment = json.loads(Path(config).read_text())
            entries = configured_routes(deployment)
            if isinstance(deployment, dict) and deployment.get("dynamic_discovery"):
                discovery = {k: deployment[k] for k in ("heaphook","network_rmw","qos","discovery_namespace") if k in deployment}
                discovery["fixed"] = sorted({name for entry in entries for name in entry[2:4]})
                discovery.setdefault("network_rmw", CYCLONE)
                if discovery["network_rmw"] not in (CYCLONE, ZENOH):
                    raise ValueError("unsupported discovery network RMW")
                discovery["heaphook"] = str(Path(discovery["heaphook"]).resolve(strict=True))
        except (OSError, ValueError) as exc:
            parser.error(f"{config}: {exc}")
    elif words[:1] == ["--automatic-group"]:
        entries, discovery = json.loads(words[1])
        automatic = True
    else:
        entries = [words]
    routes = []
    sources, destinations = set(), set()
    for entry in entries:
        args = parser.parse_args(entry)
        if args.depth < 1 or any(not t.startswith("/") for t in (args.source, args.destination)):
            parser.error("use a positive depth and absolute topic names")
        if entry is not words and (args.automatic or args.endpoint or args.parent_pid is not None):
            parser.error("private endpoint arguments cannot be configured")
        if args.endpoint:
            run_endpoint(args, args.endpoint)
            sys.exit(0)
        src, dst = ((NATIVE,args.source),(args.network_rmw,args.destination)) if args.direction == "export" else (
            (args.network_rmw,args.source),(NATIVE,args.destination))
        if dst in destinations or dst in sources or src in destinations:
            parser.error("conflicting route: a destination must be unique and cannot be another route's source")
        sources.add(src)
        destinations.add(dst)
        routes.append((args,entry))
    addresses = sorted(control_address(args) for args, _ in routes)
    if discovery:
        addresses.append(json.dumps([discovery, os.getuid(), os.stat("/dev/shm").st_dev,
            os.getenv("ROS_DOMAIN_ID","0"), [os.getenv(k) for k in ("CYCLONEDDS_URI","ZENOH_SESSION_CONFIG_URI","ZENOH_CONFIG_OVERRIDE","ROS_AUTOMATIC_DISCOVERY_RANGE","ROS_STATIC_PEERS")]], sort_keys=True))
    address = addresses[0] if len(routes)==1 and not discovery else "\0agnocast-group-"+hashlib.sha256(
        json.dumps(addresses).encode()).hexdigest()
    if joining:
        join(address,["--automatic-group",json.dumps([entries, discovery])])
    else:
        if routes and args.parent_pid is not None:
            follow_parent(args.parent_pid, signal.SIGTERM)
        supervise(routes,address,automatic or bool(routes and args.automatic),discovery)
