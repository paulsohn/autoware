#!/usr/bin/env python3
"""Summarize only complete pipeline runs, dropping ten warm-up frames."""
import csv
import math
from pathlib import Path
from statistics import median
import sys

root = Path(sys.argv[1])
def percentile(values, rank):
    return sorted(values)[math.ceil(rank * len(values)) - 1]

print("backend,round,frames,median_ms,p95_ms,max_ms")
for backend in sys.argv[2:] or ("cyclone-cpu", "zenoh-cpu", "zenoh-shmem", "zenoh-cuda"):
    pooled = []
    for run in range(1, 4):
        with (root / f"{backend}-{run}.csv").open() as f:
            rows = [[int(v) for v in row] for row in csv.reader(f)]
        if [r[0] for r in rows] != list(range(60)) or any(len(r) != 2 or r[1] <= 0 for r in rows):
            raise ValueError(f"incomplete run: {backend}-{run}")
        values = [ns / 1e6 for _, ns in rows[10:]]
        pooled.extend(values)
        print(f"{backend},{run},50,{median(values):.3f},{percentile(values,.95):.3f},{max(values):.3f}")
    print(f"{backend},all,150,{median(pooled):.3f},{percentile(pooled,.95):.3f},{max(pooled):.3f}")
