"""Summarize the six counterbalanced, 60-frame Path handoff runs."""
import csv
import math
from pathlib import Path
from statistics import median
import sys

root = Path(sys.argv[1])
def report(label, values):
    print(f"{label:33s} {median(values)/1000:7.2f} {sorted(values)[math.ceil(.95*len(values))-1]/1000:7.2f}")

print("interval                             median     p95 (us)")
for mode in ("native", "rmw", "traced"):
    values = []
    for run in range(1, 7):
        with (root / f"{mode}-{run}.csv").open() as f:
            rows = [tuple(map(int, row)) for row in csv.reader(f)]
        assert [seq for seq, _ in rows] == list(range(60))
        values.extend(ns for _, ns in rows[10:])
    report(mode, values)

frames = []
for run in range(1, 7):
    with (root / f"traced-{run}-handoff.csv").open() as f:
        rows = [{key: int(v) for key, v in row.items()} for row in csv.DictReader(f)]
    assert [r["frame"] for r in rows] == list(range(60))
    frames.extend(rows[10:])
stages = ("publish", "native", "signal", "dds_return", "ready", "take_enter", "taken", "callback")
for a, b in zip(stages, stages[1:]):
    report(f"{a} to {b}", [r[b+"_ns"] - r[a+"_ns"] for r in frames])
for a, b in [("signal", "signalled"), ("native", "callback")]:
    report(f"{a} to {b} (overlapping)", [r[b+"_ns"] - r[a+"_ns"] for r in frames])
print("blocking wait entered before signal and returned after:",
      sum(r["blocking"] and r["wait_start_ns"] <= r["signal_ns"] <= r["dds_return_ns"]
          for r in frames), "/", len(frames))
