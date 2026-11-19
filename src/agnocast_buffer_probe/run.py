"""Run inside run-lyrical.sh after sourcing the colcon probe overlay."""
import os
from pathlib import Path
import subprocess
import tempfile
import time
import uuid
from ament_index_python.packages import get_package_prefix

exe = Path(get_package_prefix("agnocast_buffer_probe")) / "lib/agnocast_buffer_probe/probe"
hook = Path(get_package_prefix("agnocastlib")) / "lib/libagnocast_heaphook.so"
router = Path(get_package_prefix("rmw_zenoh_cpp")) / "lib/rmw_zenoh_cpp/rmw_zenohd"
env = dict(os.environ, RMW_IMPLEMENTATION="rmw_zenoh_cpp", AGNOCAST_BRIDGE_MODE="OFF",
           AGNOCAST_NO_DISCOVERY_AGENT="1", AGNOCAST_BUFFER_PROBE_TOPIC="/probe_" + uuid.uuid4().hex)
with tempfile.TemporaryDirectory() as tmp:
    children = {}
    try:
        children["router"] = subprocess.Popen([router], cwd=tmp, env=env)
        time.sleep(0.6)
        for role in ("fast", "delayed", "cpu", "stale", "drop", "shmem", "decline", "capacity", "publisher"):
            children[role] = subprocess.Popen([exe, role], cwd=tmp, env=env if role in ("cpu", "shmem", "decline") else dict(env, LD_PRELOAD=str(hook)))
        deadline = time.monotonic() + 35
        while not (Path(tmp) / "kill_ready").exists():
            for role, child in children.items():
                if child.poll() not in (None, 0):
                    raise RuntimeError(f"{role} exited {child.returncode}")
            if time.monotonic() > deadline:
                raise TimeoutError("reader did not acquire and verify its loan")
            time.sleep(0.02)
        children["delayed"].kill()
        assert children["delayed"].wait(timeout=3) == -9
        (Path(tmp) / "reader_killed").touch()
        for role in ("publisher", "fast", "cpu", "stale", "drop", "shmem", "decline", "capacity"):
            if children[role].wait(timeout=25):
                raise RuntimeError(f"{role} failed")
    finally:
        for child in children.values():
            if child.poll() is None:
                child.terminate()
        for child in children.values():
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
