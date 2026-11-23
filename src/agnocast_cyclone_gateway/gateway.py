#!/usr/bin/env python3
"""One manually owned, directed ROS topic route between two process-local RMWs."""
import argparse
import ctypes
import fcntl
import hashlib
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
DDS = "rmw_cyclonedds_cpp"
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
    if rx:
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

    def forward(data):
        nonlocal count, byte_count
        if not 0 < len(data) <= MAX_BYTES:
            raise ValueError("serialized message outside gateway frame limit")
        channel.sendall(HEADER.pack(len(data)))
        channel.sendall(data)
        count += 1
        byte_count += len(data)

    def activate(active):
        nonlocal sub
        if active and sub is None:
            sub = node.create_subscription(cls, topic, forward, qos, raw=True)
            print(f"source armed topic={topic} taken={count}", flush=True)
        elif not active and sub is not None:
            node.destroy_subscription(sub)
            sub = None
            print(f"source idle topic={topic} taken={count}", flush=True)

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
        rclpy.spin(node)
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        stopping.set()
        channel.shutdown(socket.SHUT_RDWR)
        if thread:
            thread.join()
        channel.close()
        print(f"stats role={role} samples={count} bytes={byte_count} cpu_s={time.process_time()-cpu_start:.6f}", flush=True)
        node.destroy_node()
        rclpy.try_shutdown()


def acquire_route(stack, endpoints):
    domain = int(os.environ.get("ROS_DOMAIN_ID", "0"))
    directory = Path(f"/dev/shm/agnocast-gateway-{os.getuid()}")
    directory.mkdir(mode=0o700, exist_ok=True)
    info = directory.stat()
    if directory.is_symlink() or info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise RuntimeError(f"expected private route directory {directory}")
    for index, (rmw, topic) in enumerate(endpoints):
        key = hashlib.sha256(f"{domain}/{rmw}/{topic}".encode()).hexdigest()
        lock = stack.enter_context((directory / key).open("a"))
        try:
            fcntl.flock(lock, (fcntl.LOCK_EX if index else fcntl.LOCK_SH) | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError(f"conflicting route for {topic} on {rmw}") from None


def supervise(args):
    stacks = (NATIVE, DDS) if args.direction == "export" else (DDS, NATIVE)
    children = []
    stop = False

    def stopping(*_):
        nonlocal stop
        stop = True

    with ExitStack() as cleanup:
        acquire_route(cleanup, zip(stacks, (args.source, args.destination)))
        ends = socket.socketpair()
        signal.signal(signal.SIGINT, stopping)
        signal.signal(signal.SIGTERM, stopping)
        try:
            for role, rmw, end in zip(("source", "destination"), stacks, ends):
                env = dict(os.environ, RMW_IMPLEMENTATION=rmw)
                hooks = [h for h in env.get("LD_PRELOAD", "").replace(":", " ").split()
                         if "agnocast_heaphook" not in h]
                if rmw == NATIVE:
                    hooks.insert(0, str(args.heaphook.resolve(strict=True)))
                env["LD_PRELOAD"] = ":".join(hooks)
                children.append(subprocess.Popen(
                    [sys.executable, __file__, *sys.argv[1:], "--endpoint", role, "--parent-pid", str(os.getpid())],
                    stdin=end, env=env, start_new_session=True))
            for end in ends:
                end.close()
            while not stop:
                for child in children:
                    if child.poll() is not None:
                        raise RuntimeError(f"endpoint exited early (status {child.returncode})")
                time.sleep(.05)
        finally:
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("direction", choices=("export", "import"))
    parser.add_argument("type", metavar="PACKAGE/msg/TYPE")
    parser.add_argument("source")
    parser.add_argument("destination")
    parser.add_argument("--heaphook", type=Path, required=True)
    parser.add_argument("--depth", type=int, default=10)
    parser.add_argument("--always-on", action="store_true", help="arm export even without a DDS reader")
    for side, default in (("input", "best_effort"), ("output", "reliable")):
        parser.add_argument(f"--{side}-reliability", choices=("best_effort", "reliable"), default=default)
    parser.add_argument("--endpoint", choices=("source", "destination"), help=argparse.SUPPRESS)
    parser.add_argument("--parent-pid", type=int, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.depth < 1 or any(not t.startswith("/") for t in (args.source, args.destination)):
        parser.error("use a positive depth and absolute topic names")
    if args.endpoint:
        run_endpoint(args, args.endpoint)
    else:
        supervise(args)
