"""Summarize a verified four-arm CIE rehearsal with a paired native control per round."""
import csv
from math import ceil
from pathlib import Path
import re
from statistics import median
import sys

logs = Path(sys.argv[1])
with (logs / "lidar-o3-ages.csv").open() as stream:
    rows = list(csv.DictReader(stream))
out = csv.writer(sys.stdout, lineterminator="\n")
out.writerow(("arm", "round", "full_groups", "partial_groups", "full_p50_ms", "full_p95_ms",
              "partial_p50_ms", "partial_p95_ms", "launch_cpu_s", "min_host_mib"))


def timing(ages):
    values = sorted(ages)
    return [round(x / 1e6, 3) for x in (median(values), values[ceil(.95*len(values))-1])]


for arm in ("native", "default", "cyclone", "zenoh"):
    all_full, all_partial, cpus, memory = [], [], [], []
    for round_id in range(1, 6):
        run = [r for r in rows if r["arm"] == arm and int(r["round"]) == round_id]
        full, partial = ([int(r["age_ns"]) for r in run if int(r["seq"]) >= 4 and r["kind"] == kind]
                         for kind in ("full", "partial"))
        if len(full) != 10 or len(partial) != 10:
            raise ValueError((arm, round_id, "unexpected group count"))
        log = (logs / f"{arm}-{round_id}.log").read_text()
        if arm == "default" and "Rehearsal RMW " not in log:
            raise ValueError("missing loaded RMW verification")
        user, system = re.search(r"graph_cpu ([\d.]+) ([\d.]+)", log).groups()
        cpus.append(round(float(user)+float(system), 3))
        memory.append(int(re.search(r"Minimum host MemAvailable: (\d+)", log)[1]))
        out.writerow([arm, round_id, 12, 12, *timing(full), *timing(partial), cpus[-1], memory[-1]])
        all_full.extend(full)
        all_partial.extend(partial)
    out.writerow([arm, "pooled", 60, 60, *timing(all_full), *timing(all_partial),
                  median(cpus), min(memory)])
