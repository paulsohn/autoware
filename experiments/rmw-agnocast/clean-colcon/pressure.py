"""Sample Linux pressure stall totals once a second without instrumenting the ROS nodes."""
import csv
import sys
import time
from pathlib import Path

scope = Path("/sys/fs/cgroup") / Path("/proc/self/cgroup").read_text().strip().split("::")[-1].lstrip("/")
files = [(f"{label}_{resource}", root / pattern.format(resource)) for label, root, pattern in
         (("host", Path("/proc/pressure"), "{}"), ("scope", scope, "{}.pressure"))
         for resource in ("cpu", "io", "memory")]
with Path(sys.argv[1]).open("x", buffering=1) as log:
    writer = csv.writer(log, lineterminator="\n")
    writer.writerow(["wall_ns", "monotonic_ns"] + [name + "_some_us" for name, _ in files])
    while True:
        counts = []
        for _, path in files:
            line = next(s for s in path.read_text().splitlines() if s.startswith("some "))
            counts.append(int(line.split("total=")[1]))
        writer.writerow([time.time_ns(), time.monotonic_ns(), *counts])
        time.sleep(1)
