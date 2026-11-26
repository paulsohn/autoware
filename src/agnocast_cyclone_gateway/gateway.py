#!/usr/bin/env python3
"""One manually owned, directed ROS topic route between two process-local RMWs."""
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


def run_endpoint(args, role):
    # An unexpected supervisor death must also release both ROS endpoints.
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(1, signal.SIGKILL, 0, 0, 0) != 0:  # Linux PR_SET_PDEATHSIG
        raise OSError(ctypes.get_errno(), "prctl(PR_SET_PDEATHSIG)")
    if os.getppid() != args.parent_pid:
        raise RuntimeError("gateway supervisor disappeared during startup")
    import rclpy
    from rclpy.executors import ExternalShutdownException
    from rclpy.qos import QoSProfile, ReliabilityPolicy
    from rclpy.utilities import get_rmw_implementation_identifier
    from rosidl_runtime_py.utilities import get_message

    cls = get_message(args.type)
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

    try:
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
        fd = int(os.environ.pop("AGNOCAST_ENDPOINT_READY_FD"))
        os.write(fd,b"+")
        os.close(fd)
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


def join(args, words):
    address = control_address(args)
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
                            child = subprocess.Popen([sys.executable,__file__,*words,"--automatic"],
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


def supervise(args):
    stacks = (NATIVE, args.network_rmw) if args.direction == "export" else (args.network_rmw, NATIVE)
    children = []
    stop = False

    def stopping(*_):
        nonlocal stop
        stop = True

    address = control_address(args)
    phase_file = route_directory() / (address[1:]+".phase")
    # The old supervisor holds this lock through endpoint teardown and route-lock release.
    # A healthy owner keeps it unlocked so conflicting launchers fail promptly.
    with phase_file.open("a") as phase, ExitStack() as cleanup:
        lock_before_start(phase, fcntl.LOCK_SH)
        acquire_route(cleanup, zip(stacks, (args.source, args.destination)))
        listener = cleanup.enter_context(socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET))
        listener.bind(address)
        listener.listen(16)
        fcntl.flock(phase, fcntl.LOCK_UN)
        print(f"owner pid={os.getpid()} automatic={args.automatic}",flush=True)
        leases = []
        ends = socket.socketpair()
        ready_read,ready_write = os.pipe()
        cleanup.callback(os.close,ready_read)
        signal.signal(signal.SIGINT, stopping)
        signal.signal(signal.SIGTERM, stopping)
        try:
            for role, rmw, end in zip(("source", "destination"), stacks, ends):
                env = dict(os.environ, RMW_IMPLEMENTATION=rmw)
                env.pop("RMW_AGNOCAST_GATEWAY_CONFIG",None)
                env["AGNOCAST_ENDPOINT_READY_FD"] = str(ready_write)
                hooks = [h for h in env.get("LD_PRELOAD", "").replace(":", " ").split()
                         if "agnocast_heaphook" not in h]
                if rmw == NATIVE:
                    hooks.insert(0, str(args.heaphook.resolve(strict=True)))
                env["LD_PRELOAD"] = ":".join(hooks)
                children.append(subprocess.Popen(
                    [sys.executable, __file__, *sys.argv[1:], "--endpoint", role, "--parent-pid", str(os.getpid())],
                    stdin=end, env=env, pass_fds=(ready_write,), start_new_session=True))
            for end in ends:
                end.close()
            ready = 0
            deadline = time.monotonic()+8
            joined = False
            while not stop:
                for child in children:
                    if child.poll() is not None:
                        raise RuntimeError(f"endpoint exited early (status {child.returncode})")
                if ready<2:
                    if select.select([ready_read],[],[],.05)[0]:
                        ready += len(os.read(ready_read,2-ready))
                    elif time.monotonic()>deadline:
                        raise TimeoutError("gateway endpoints failed to become ready")
                    continue
                readable = select.select([listener,*leases],[],[],.05)[0]
                if listener in readable:
                    accept_lease(listener,leases,args.automatic)
                    joined |= bool(leases)
                for lease in list(leases):
                    if lease in readable:
                        leases.remove(lease)
                        lease.close()
                if args.automatic and not leases and (joined or time.monotonic()>deadline):
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


def route_arguments(config):
    """Translate the public file schema into the same validated route as a manual launch."""
    if isinstance(config, list) and all(isinstance(word, str) for word in config):
        print("Deprecated gateway argument array; migrate to a JSON route object", file=sys.stderr)
        return config
    if not isinstance(config, dict):
        raise ValueError("expected a JSON route object")
    required = ("direction", "type", "source", "destination", "heaphook")
    unknown = config.keys() - {*required, "network_rmw", "always_on", "qos"}
    if unknown:
        raise ValueError(f"unknown route fields: {', '.join(sorted(unknown))}")
    for key in required:
        if not isinstance(config.get(key), str) or not config[key] or config[key].startswith("-"):
            raise ValueError(f"{key} requires a nonempty route value")
    words = [config[key] for key in ("direction", "type", "source", "destination")]
    words.append("--heaphook=" + config["heaphook"])
    if "network_rmw" in config:
        if not isinstance(config["network_rmw"], str):
            raise ValueError("network_rmw requires an RMW name")
        words.append("--network-rmw=" + config["network_rmw"])
    always_on = config.get("always_on", False)
    if not isinstance(always_on, bool):
        raise ValueError("always_on requires a boolean")
    if always_on:
        words.append("--always-on")
    qos = config.get("qos", {})
    if not isinstance(qos, dict) or qos.keys() - {"depth", "input_reliability", "output_reliability"}:
        raise ValueError("qos accepts depth, input_reliability and output_reliability")
    for key, value in qos.items():
        if key == "depth":
            if type(value) is not int or value < 1:
                raise ValueError("qos.depth requires a positive integer")
        elif not isinstance(value, str):
            raise ValueError(f"qos.{key} requires a reliability name")
        words.append(f"--{key.replace('_', '-')}={value}")
    return words


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
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
    joining = words[:1]==["--join"]
    if joining:
        config, domain = words[1:]
        os.environ["ROS_DOMAIN_ID"] = str(int(domain))
        try:
            words = route_arguments(json.loads(Path(config).read_text()))
        except (OSError, ValueError) as exc:
            parser.error(f"{config}: {exc}")
    args = parser.parse_args(words)
    if args.depth < 1 or any(not t.startswith("/") for t in (args.source, args.destination)):
        parser.error("use a positive depth and absolute topic names")
    if joining:
        if args.automatic or args.endpoint or args.parent_pid is not None:
            parser.error("private endpoint arguments cannot be configured")
        join(args,words)
    elif args.endpoint:
        run_endpoint(args, args.endpoint)
    else:
        supervise(args)
