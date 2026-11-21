"""Join userspace receive markers to one verified frame of the first crop."""
import csv
from collections import defaultdict
from math import ceil
from pathlib import Path
from statistics import median
import sys

root = Path(sys.argv[1])
steady = (root / "100-fixed-1.stamps").exists()
pooled = defaultdict(lambda: [[], []])
runs = sorted(root.glob("*fixed-*.stamps"))
assert len(runs) == 4
for path in runs:
    stamps = list(map(int, path.read_text().splitlines()))
    assert len(set(stamps)) == len(stamps) == 60
    with path.with_suffix(".profile").open() as f:
        profiles = list(csv.DictReader(f))
    assert [int(r["stamp"]) for r in profiles] == stamps
    keyed = defaultdict(dict)
    unkeyed = defaultdict(list)
    with path.with_suffix(".reader").open() as f:
        for name, stamp, ns in csv.reader(f):
            if int(stamp):
                assert name not in keyed[int(stamp)]
                keyed[int(stamp)][name] = int(ns)
            else:
                unkeyed[name].append(int(ns))
    with path.with_suffix(".rcl").open() as f:
        for name, ns, *_ in csv.reader(f):
            unkeyed[name].append(int(ns))
    with path.with_suffix(".receive").open() as f:
        events = [(n, int(cb), int(ns)) for n, cb, ns in csv.reader(f)]
    for i in range(10, 60):
        stamp = stamps[i]
        long = stamp - stamps[i-1] > 60_000_000
        assert long == (path.name.startswith("100-") if steady else
                        (i % 2 == 1) == (path.stem[-1] in "23"))
        t = keyed[stamp].copy()
        t["stamp"] = stamp
        t["entry"] = int(profiles[i]["entry"])
        t["poll"], = [v for v in unkeyed["poll"] if stamp <= v <= t["admit"]]
        ids = set()
        for name, cb, ns in events:
            if t["poll"] <= ns <= t["admit"]:
                assert name not in t
                ids.add(cb)
                t[name] = ns
        assert len(ids) == 1
        for name in ("ready", "rcl_wait_return", "loan_check_in", "loan_check_out", "rcl_take_entry", "rmw_entry"):
            t[name], = [v for v in unkeyed[name] if t["admit"] <= v <= t["take"]]
        stages = ("stamp", "poll", "handler", "read_in", "read_out", "receive", "ioctl_in",
                  "ioctl_out", "create_loan", "invoke", "admit", "ready", "rcl_wait_return",
                  "loan_check_in", "loan_check_out", "rcl_take_entry", "rmw_entry", "take", "restored", "entry")
        segments = list(zip(stages, stages[1:]))
        if "selected_sub" in unkeyed:
            extra = ("rcl_wait_return", "select_in", "selected_sub", "execute_sub", "loan_check_in")
            for name in extra[1:-1]:
                t[name], = [v for v in unkeyed[name] if t[extra[0]] <= v <= t[extra[-1]]]
            segments.extend(zip(extra, extra[1:]))
        for a, b in segments:
            assert t[b] >= t[a], (path, i, a, b)
            pooled[f"{a} to {b}"][long].append((t[b]-t[a])/1000)
print("Stage | 20 ms median/p95 us | 100 ms median/p95 us | median difference us")
for name, (short, long) in pooled.items():
    assert len(short) == len(long) == 100
    def stats(v):
        return f"{median(v):.1f}/{sorted(v)[ceil(.95*len(v))-1]:.1f}"
    print(f"{name} | {stats(short)} | {stats(long)} | {median(long)-median(short):+.1f}")
