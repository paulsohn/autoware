"""Summarize complete Universe runs and paired bridge timings."""
import csv
import math
import sys
from pathlib import Path
from statistics import median

root = Path(sys.argv[1])


def percentiles(ns):
    return median(ns) / 1e6, sorted(ns)[math.ceil(len(ns) * .95) - 1] / 1e6


def frames(path):
    rows = list(csv.reader(path.read_text().splitlines()))
    assert [int(row[0]) for row in rows] == list(range(60)), path
    values = [int(row[1]) for row in rows]
    assert min(values) > 0, path
    return values[10:]


def collect(name, batches):
    samples, cpu, rss, paired = [], [], [], []
    for batch in batches:
        for n in range(1, 4):
            prefix = root / batch / f"{name}-{n}"
            local = frames(prefix.with_suffix(".csv"))
            samples.extend(local)
            resource, = csv.DictReader(Path(f"{prefix}-resources.csv").read_text().splitlines())
            assert int(resource["verified_frames"]) == 60
            cpu.append(float(resource["child_user_s"]) + float(resource["child_system_s"]))
            rss.append(int(resource["largest_child_maxrss_kib"]) / 1024)
            if name == "active":
                remote = frames(Path(f"{prefix}-observer.csv"))
                observer.extend(remote)
                paired.extend(r - l for r, l in zip(remote, local))
            if name in ("active", "idle"):
                off = percentiles(frames(root / batch / f"off-{n}.csv"))
                differences[name].append(tuple(x-y for x,y in zip(percentiles(local), off)))
    m, p = percentiles(samples)
    print(f"{name:10s} median {m:.3f} p95 {p:.3f} ms, CPU {median(cpu):.3f} s, peak {median(rss):.1f} MiB")
    if paired:
        print("observer median/p95 ms", percentiles(observer))
        print("paired observer minus local ms", percentiles(paired))


observer = []
differences = {"active": [], "idle": []}
for mode in ("off", "idle", "active"):
    collect(mode, ("forward", "reverse"))
for mode in ("agnocast", "cyclone", "zenoh"):
    collect(mode, ("references",))
for mode, runs in differences.items():
    print(mode, "minus off, across-run median and range (us)")
    for column in zip(*runs):
        print([round(v * 1000, 1) for v in (median(column), min(column), max(column))])

stages = [[], [], []]
for n in range(1, 4):
    timings = [list(map(int, row)) for row in csv.reader(
        (root / "profile" / f"timing-{n}.csv").read_text().splitlines())]
    assert len(timings) == 60 and all(a[0] < b[0] for a,b in zip(timings, timings[1:]))
    remote = frames(root / "profile" / f"active-{n}-observer.csv")
    for (stamp, entry, returned), latency in zip(timings[10:], remote):
        assert stamp <= entry <= returned and stamp + latency >= entry
        for stage, value in zip(stages, (entry-stamp, returned-entry, stamp+latency-entry)):
            stage.append(value)
for label, values in zip(("source to relay callback", "relay callback through publish",
                          "relay callback to observer callback"), stages):
    print(label, "median/p95 ms", percentiles(values))
