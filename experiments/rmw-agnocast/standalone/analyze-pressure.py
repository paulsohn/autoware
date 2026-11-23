"""Summarize the sequential bench-pressure.sh and bench-churn.sh batches."""
import csv
import math
import re
import sys
from pathlib import Path
from statistics import median

pressure, churn, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)


def write(name, header, rows):
    with (output / name).open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(header.split())
        writer.writerows(rows)


frames, launches, summary, lifecycle = [], [], [], []
for mode, rounds in (("native", 4), ("standalone", 4), ("cyclone", 2), ("zenoh", 2)):
    for run in range(1, rounds + 1):
        log = (pressure / f"{mode}-{run}.log").read_text()

        def counter(key):
            return int(re.search(rf"{key}=(\d+)", log)[1])

        start = max(counter("near_stall_start_ns"), counter("second_stall_start_ns"))
        end = min(counter("near_stall_end_ns"), counter("second_stall_end_ns"))
        assert end > start and "verified 32 synchronized clouds and metadata" in log
        views, copies = map(int, re.search(r"sync_input views=(\d+) copies=(\d+)", log).groups())
        assert (views, copies) == ((80, 0) if mode in ("native", "standalone") else (0, 80))
        cpu = sum(map(float, re.search(r"graph_cpu (\S+) (\S+)", log).groups()))
        memory = int(re.search(r"MemAvailable: (\d+) MiB", log)[1])
        launches.append((mode, run, round(cpu, 3), memory, end-start,
                         counter("competitor_cpu_ns"), counter("second_cpu_ns"), views, copies))
        samples = re.findall(r"\bsample,(\d+),(\w+),(\d+)", log)
        branches = re.findall(r"\bbranch,(\d+),(\w+),([01]),(\d+),(\d+)", log)
        assert sorted(int(s) for s, _, _ in samples) == list(range(32))
        for branch in ("0", "1"):
            assert sorted(int(s) for s, _, b, _, _ in branches if b == branch) == list(range(32))
        by_key = {(s, k, b): (int(stamp), int(age)) for s, k, b, stamp, age in branches}
        for seq, kind, age in samples:
            stamp, near = by_key[seq, kind, "0"]
            far_stamp, far = by_key[seq, kind, "1"]
            assert stamp == far_stamp
            frames.append((mode, run, int(seq), kind, int(start <= stamp <= end), int(age), near, far))
    for window in ("steady", "overlap"):
        selected = [f for f in frames if f[0] == mode and f[2] >= 4 and f[3] == "full"
                    and (window == "steady" or f[4])]
        for idx, stage in enumerate(("sync", "near", "far"), 5):
            values = sorted(f[idx] / 1e6 for f in selected)
            summary.append((mode, window, stage, len(values), round(median(values), 3),
                            round(values[math.ceil(.95 * len(values))-1], 3)))
    if mode != "native":
        for run in (1, 2):
            log = (churn / f"{mode}-{run}.log").read_text()
            result = re.search(r"raw=(\d+)/(\d+) crop=(\d+) single_first=0 steady_ranges=(.*?) shutdown_ms=([\d.]+)", log)
            assert result and result[1] == result[2]
            lifecycle.append((mode, run, result[1], result[3], result[4], result[5]))
write("pressure-frames.csv", "mode run seq kind overlap sync_ns near_ns far_ns", frames)
write("pressure-launches.csv", "mode run cpu_s min_host_mib overlap_ns timer1_cpu_ns timer2_cpu_ns views copies", launches)
write("pressure-summary.csv", "mode window stage frames median_ms p95_ms", summary)
write("component-churn.csv", "mode run raw crop steady_seq_ranges idle_shutdown_ms", lifecycle)
