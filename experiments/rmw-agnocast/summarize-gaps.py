"""Compare verified 20 and 100 ms gaps within each process lifetime."""
import csv
from collections import defaultdict
from math import ceil
from pathlib import Path
from statistics import median
import sys


def latencies(path):
    with path.open() as f:
        rows = list(csv.reader(f))
    assert [int(row[0]) for row in rows] == list(range(60)), path
    return [int(row[1]) for row in rows]


def p95(values):
    return sorted(values)[ceil(.95 * len(values)) - 1]


root = Path(sys.argv[1])
for mode in ("agnocast", "fixed"):
    pooled = defaultdict(lambda: ([], []))
    deltas = defaultdict(list)
    steady = (root / f"20-{mode}-1.csv").exists()
    for run in range(1, (2 if steady else 4) + 1):
        grouped = defaultdict(lambda: ([], []))
        for prefix in (("20-", "100-") if steady else ("",)):
            base = root / f"{prefix}{mode}-{run}"
            stamp = list(map(int, base.with_suffix(".stamps").read_text().splitlines()))
            assert len(stamp) == 60
            sink = latencies(base.with_suffix(".csv"))
            tap = latencies(base.with_name(base.name + "-tap.csv"))
            trace = base.with_suffix(".profile")
            records = []
            if trace.exists():
                with trace.open() as f:
                    records = [list(map(int, row)) for row in list(csv.reader(f))[1:]]
                assert [r[0] for r in records] == stamp
            for i in range(10, 60):
                gap = stamp[i] - stamp[i-1]
                long = int(gap > 60_000_000)
                assert abs(gap - (100_000_000 if long else 20_000_000)) < 10_000_000, (base, i, gap)
                expected = prefix == "100-" if steady else (i % 2 == 1) == (run in (2, 3))
                assert long == expected, (base, i)
                data = dict(gap=gap, tap=tap[i], sink=sink[i])
                if records:
                    start, entry, d1, cpu1, cpu2, d2, end = records[i]
                    previous = records[i-1]
                    assert start < entry < end and previous[5] <= d1 <= d2 and cpu2 >= cpu1
                    data.update(to_entry=entry-start, callback_wall=end-entry, callback_cpu=cpu2-cpu1,
                                runnable_between=d1-previous[5], runnable_inside=d2-d1)
                for k, v in data.items():
                    grouped[k][long].append(v / 1000)
                    pooled[k][long].append(v / 1000)
        for k, (short, long) in grouped.items():
            assert len(short) == len(long) == (50 if steady else 25)
            deltas[k].append(median(long) - median(short))
    print(mode, "microseconds: metric, short median/p95, long median/p95, per-round long-minus-short median deltas")
    for k, (short, long) in pooled.items():
        print(k, *(f"{v:.1f}" for v in (median(short), p95(short), median(long), p95(long), *deltas[k])))
    if "runnable_between" in pooled:
        for label, costs in zip(("short", "long"), pooled["runnable_between"]):
            print(label, "between-callback run-queue increments above 0 / 25 us, total:",
                  sum(x > 0 for x in costs), sum(x > 25 for x in costs), len(costs))
