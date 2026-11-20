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
with tempfile.TemporaryDirectory() as directory:
    reader = subprocess.Popen([probe, mode, "reader"], cwd=directory, env=env)
    try:
        subprocess.run([probe, mode, "writer"], cwd=directory, env=env, check=True, timeout=30)
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

usage = resource.getrusage(resource.RUSAGE_CHILDREN)
with open(prefix + "-resources.csv", "x") as f:
    out = csv.writer(f, lineterminator="\n")
    out.writerow(("verified_frames", "child_user_s", "child_system_s", "largest_child_maxrss_kib"))
    out.writerow((60, usage.ru_utime, usage.ru_stime, usage.ru_maxrss))
