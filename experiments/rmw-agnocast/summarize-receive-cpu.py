"""Stratify alternating gaps by CPU at consecutive first-crop wait returns."""
import csv
from collections import defaultdict
from pathlib import Path
from statistics import median
import sys

root = Path(sys.argv[1])
groups = defaultdict(lambda: [[], []])
paths = sorted(root.glob("fixed-*.stamps"))
assert len(paths) == 4
for path in paths:
    stamps = list(map(int, path.read_text().splitlines()))
    assert len(stamps) == 60
    with path.with_suffix(".profile").open() as f:
        entries = list(csv.DictReader(f))
    assert [int(r["stamp"]) for r in entries] == stamps
    admission = {}
    take = {}
    with path.with_suffix(".reader").open() as f:
        for event, stamp, ns in csv.reader(f):
            if event == "admit":
                admission[int(stamp)] = int(ns)
            if event == "take":
                take[int(stamp)] = int(ns)
    with path.with_suffix(".rcl").open() as f:
        waits = [(int(ns), int(cpu)) for event, ns, cpu in csv.reader(f) if event == "rcl_wait_return"]
    last_cpu = None
    for i, stamp in enumerate(stamps):
        cpu, = [cpu for ns, cpu in waits if admission[stamp] <= ns <= take[stamp]]
        assert cpu >= 0
        if i >= 10:
            gap = stamp-stamps[i-1]
            long = gap > 60_000_000
            assert abs(gap-(100_000_000 if long else 20_000_000)) < 10_000_000
            kind = "same CPU" if cpu == last_cpu else "different CPU"
            groups[kind][long].append((int(entries[i]["entry"])-stamp)/1000)
        last_cpu = cpu

for kind in ("same CPU", "different CPU"):
    print(kind)
    for label, samples in zip(("20 ms", "100 ms"), groups[kind]):
        print(label, "frames", len(samples), "stamp to callback median us",
              f"{median(samples):.1f}" if samples else "n/a")
assert all(sum(len(groups[k][i]) for k in groups) == 100 for i in range(2))
