"""Summarize counterbalanced, 60-frame Path handoff runs."""
import csv
import math
from pathlib import Path
from statistics import median
import sys

root = Path(sys.argv[1])
def report(label, values):
    print(f"{label:33s} {median(values)/1000:7.2f} {sorted(values)[math.ceil(.95*len(values))-1]/1000:7.2f}")

print("interval                             median     p95 (us)")
variant = "direct" if (root / "direct-1.csv").exists() else "traced"
rendezvous = (root / "rendezvous-1.csv").exists()
modes = ("native", "direct", "rendezvous") if rendezvous else ("native", "rmw", variant)
native_stages = (root / "native-traced-1.csv").exists()
rounds = range(1, 5) if native_stages else range(1, 7)
if native_stages:
    modes = ("native", "native-traced", "direct", "direct-stages")
dispatch = (root / "single-1.csv").exists()
if dispatch:
    rounds = range(1, 6)
    modes = ("native", "direct", "single", "rendezvous", "single-traced")
for mode in modes:
    values = []
    for run in rounds:
        with (root / f"{mode}-{run}.csv").open() as f:
            rows = [tuple(map(int, row)) for row in csv.reader(f)]
        assert [seq for seq, _ in rows] == list(range(60))
        values.extend(ns for _, ns in rows[10:])
    report(mode, values)

if variant == "direct":
    print("median per run: child CPU seconds, largest child peak MiB")
    for mode in modes:
        cpu, rss = [], []
        for run in rounds:
            with (root / f"{mode}-{run}-resources.csv").open() as f:
                row = next(csv.DictReader(f))
            cpu.append(float(row["child_user_s"]) + float(row["child_system_s"]))
            rss.append(int(row["largest_child_maxrss_kib"])/1024)
        print(f"{mode:8s} {median(cpu):.3f} {median(rss):.1f}")
    if dispatch:
        for mode in ("rendezvous", "single-traced"):
            values = []
            for run in rounds:
                with (root / f"{mode}-{run}-stages.csv").open() as f:
                    rows = list(csv.DictReader(f))
                assert [int(r["frame"]) for r in rows] == list(range(60))
                values.extend(int(r["callback"]) - int(r["ready"]) for r in rows[10:])
            report(f"{mode} ready to callback", values)
        sys.exit(0)
    if native_stages:
        for mode in ("native-traced", "direct-stages"):
            frames = []
            for run in rounds:
                with (root / f"{mode}-{run}-core.csv").open() as f:
                    core = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(f)]
                assert [r["frame"] for r in core] == list(range(60))
                if mode == "direct-stages":
                    with (root / f"{mode}-{run}-stages.csv").open() as f:
                        outer = list(csv.DictReader(f))
                    assert len(core) == len(outer)
                    for r, o in zip(core, outer):
                        assert all(r[k] == int(o[k]) for k in ("frame", "entry_id", "publish", "callback"))
                        r["outer_wake"] = int(o["wake"])
                        for k in ("interrupted", "drained", "synchronized", "ready"):
                            r[k] = int(o[k])
                frames.extend(core[10:])
            intervals = [("publish", "wake"), ("wake", "acquired"), ("acquired", "callback")]
            if mode == "direct-stages":
                intervals.extend([("publish", "outer_wake"), ("outer_wake", "interrupted"),
                                  ("interrupted", "wake"), ("acquired", "drained"),
                                  ("drained", "synchronized"), ("acquired", "ready"),
                                  ("ready", "callback")])
            print(mode)
            for a, b in intervals:
                report(f"{a} to {b}", [r[b] - r[a] for r in frames])
    if rendezvous:
        frames = []
        for run in rounds:
            with (root / f"rendezvous-{run}-stages.csv").open() as f:
                rows = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(f)]
            assert [r["frame"] for r in rows] == list(range(60))
            frames.extend(rows[10:])
        for a, b in (("publish", "wake"), ("wake", "interrupted"), ("interrupted", "acquired"),
                     ("acquired", "drained"), ("drained", "synchronized"),
                     ("synchronized", "ready"), ("ready", "callback"), ("wake", "callback")):
            report(f"{a} to {b}", [r[b] - r[a] for r in frames])
        report("DDS return after drain (clamped)", [
            max(0, r["helper_return"] - r["drained"]) for r in frames])
        print("DDS returned by native drain:", sum(r["helper_return"] <= r["drained"]
                                                    for r in frames), "/", len(frames))
        report("interrupt + sync intervals", [
            r["interrupted"] - r["wake"] + r["synchronized"] - r["drained"] for r in frames])
        report("remaining outside both intervals", [
            (r["callback"] - r["publish"]) - (r["interrupted"] - r["wake"])
            - (r["synchronized"] - r["drained"]) for r in frames])
    sys.exit(0)

frames = []
for run in rounds:
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
