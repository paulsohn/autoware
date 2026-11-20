"""Run a matched Path writer/reader pair and verify its loan addresses."""
import csv
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile

probe, hook, mode, prefix = sys.argv[1:]
env = dict(os.environ, LD_PRELOAD=hook)
env.pop("AGNOCAST_RMW_DIRECT_WAIT", None)
env.pop("AGNOCAST_RMW_HANDOFF_FILE", None)
env.pop("AGNOCAST_RMW_DIRECT_TRACE", None)
with tempfile.TemporaryDirectory() as directory:
    reader_env = env.copy()
    native_trace = mode == "native-traced"
    direct_trace = mode == "direct-stages"
    writer_mode = "native" if mode in ("native", "native-traced") else "rmw"
    if native_trace or direct_trace:
        library = Path("build/rmw-agnocast/path-trace-lib").resolve()
        if not (library / "libagnocast.so").is_file():
            raise FileNotFoundError(library / "libagnocast.so")
        reader_env["LD_LIBRARY_PATH"] = str(library) + ":" + env["LD_LIBRARY_PATH"]
    single = mode in ("single", "single-traced")
    if mode in ("rendezvous", "single-traced") or direct_trace:
        reader_env["AGNOCAST_RMW_DIRECT_TRACE"] = str(Path(directory) / "rendezvous")
    if mode in ("direct", "rendezvous") or direct_trace or single:
        reader_env["AGNOCAST_RMW_DIRECT_WAIT"] = "1"
        mode = "rmw-traced" if direct_trace else "rmw"
    if single:
        mode = "single"
    if mode == "traced":
        reader_env["AGNOCAST_RMW_HANDOFF_FILE"] = str(Path(directory) / "handoff")
        mode = "rmw"
    reader = subprocess.Popen([probe, mode, "reader"], cwd=directory, env=reader_env)
    try:
        subprocess.run([probe, writer_mode, "writer"], cwd=directory, env=env, check=True, timeout=30)
        if reader.wait(timeout=5):
            raise RuntimeError("reader failed")
    finally:
        if reader.poll() is None:
            reader.kill()
            reader.wait()

    def records(role):
        lines = (Path(directory) / role).read_text().splitlines()
        return [list(map(int, line.split())) for line in lines]

    tx, rx = records("writer"), records("reader")
    if len(tx) != 60 or len(rx) != 60:
        raise ValueError("expected 60 delivered frames")
    with open(prefix + ".csv", "x") as f:
        out = csv.writer(f, lineterminator="\n")
        for seq, (a, b) in enumerate(zip(tx, rx)):
            if len(a) != 4 or len(b) != 4 or a[1:] != b[1:] or not all(a[1:]) or b[0] <= a[0]:
                raise ValueError(f"bad loan addresses or timestamps at frame {seq}")
            out.writerow((seq, b[0] - a[0]))

    if native_trace or direct_trace:
        core_rows = records("native-stages")
        if len(core_rows) != len(tx) or len({r[0] for r in core_rows}) != len(core_rows):
            raise ValueError("missing or duplicate native acquisition")
        with open(prefix + "-core.csv", "x") as f:
            out = csv.writer(f, lineterminator="\n")
            out.writerow(("frame", "entry_id", "publish", "wake", "acquired", "callback"))
            for seq, ((entry, address, wake, acquired), a, b) in enumerate(zip(core_rows, tx, rx)):
                if address != a[1] or not a[0] <= wake <= acquired <= b[0]:
                    raise ValueError(f"inconsistent native stages at frame {seq}")
                out.writerow((seq, entry, a[0], wake, acquired, b[0]))

    if "AGNOCAST_RMW_DIRECT_TRACE" in reader_env:
        rows = records("rendezvous")
        if len(rows) != len(tx) or len({row[0] for row in rows}) != len(rows):
            raise ValueError("missing or duplicate direct trace entries")
        with open(prefix + "-stages.csv", "x") as f:
            out = csv.writer(f, lineterminator="\n")
            out.writerow(("frame", "entry_id", "publish", "wake", "interrupted", "acquired",
                          "drained", "helper_return", "synchronized", "ready", "callback"))
            for seq, (a, b, row) in enumerate(zip(tx, rx, rows)):
                if len(row) != 16:
                    raise ValueError(f"frame {seq} did not follow a blocking native wake")
                entry, addr, acquired, _, _, _, _, ready, take, taken, _, wake, interrupted, drained, helper, sync = row
                chain = [a[0], wake, interrupted, acquired, drained, sync, ready, take, taken, b[0]]
                if addr != a[1] or chain != sorted(chain) or not 0 < helper <= sync:
                    raise ValueError(f"inconsistent direct wait at frame {seq}")
                if direct_trace:
                    core_entry, core_address, core_wake, core_loan = core_rows[seq]
                    if (entry, addr) != (core_entry, core_address) or not interrupted <= core_wake <= core_loan <= acquired:
                        raise ValueError(f"direct and native stages disagree at frame {seq}")
                out.writerow((seq, entry, *chain[:5], helper, sync, ready, b[0]))

    if "AGNOCAST_RMW_HANDOFF_FILE" in reader_env:
        rows = records("handoff")
        if len(rows) != len(tx) or len({row[0] for row in rows}) != len(rows):
            raise ValueError("missing or duplicate native entries")
        with open(prefix + "-handoff.csv", "x") as f:
            out = csv.writer(f, lineterminator="\n")
            out.writerow(("frame", "entry_id", "publish_ns", "native_ns", "signal_ns",
                          "signalled_ns", "wait_start_ns", "dds_return_ns", "ready_ns",
                          "take_enter_ns", "taken_ns", "callback_ns", "blocking"))
            for seq, (a, b, row) in enumerate(zip(tx, rx, rows)):
                entry, addr, native, signal, signalled, start, returned, ready, take, taken, blocking = row
                chain = [a[0], native, signal, signalled, ready, take, taken, b[0]]
                if addr != a[1] or chain != sorted(chain) or not 0 < start <= returned <= ready:
                    raise ValueError(f"inconsistent handoff at frame {seq}")
                out.writerow((seq, entry, *chain[:4], start, returned, *chain[4:], blocking))

usage = resource.getrusage(resource.RUSAGE_CHILDREN)
with open(prefix + "-resources.csv", "x") as f:
    out = csv.writer(f, lineterminator="\n")
    out.writerow(("verified_frames", "child_user_s", "child_system_s", "largest_child_maxrss_kib"))
    out.writerow((60, usage.ru_utime, usage.ru_stime, usage.ru_maxrss))
