"""Join first-hop events to validated cloud frames and compare preceding gaps."""
import csv
from collections import defaultdict
from math import ceil
from pathlib import Path
from statistics import median
import sys

root = Path(sys.argv[1])
groups = defaultdict(lambda: [[], []])
steady = (root / "100-fixed-1.stamps").exists()
runs = sorted(root.glob("*fixed-*.stamps"))
assert len(runs) == 4
for path in runs:
    stamps = list(map(int, path.read_text().splitlines()))
    assert len(stamps) == len(set(stamps)) == 60
    events = {t: {} for t in stamps}
    unkeyed = defaultdict(list)
    for suffix in (".writer", ".reader"):
        with path.with_suffix(suffix).open() as f:
            for name, stamp, ts in csv.reader(f):
                stamp, ts = int(stamp), int(ts)
                if name in ("poll", "ready"):
                    unkeyed[name].append(ts)
                else:
                    assert name not in events[stamp]
                    events[stamp][name] = ts
    with path.with_suffix(".profile").open() as f:
        entries = list(csv.DictReader(f))
    assert [int(r["stamp"]) for r in entries] == stamps
    for i in range(10, 60):
        stamp = stamps[i]
        e = events[stamp]
        e["stamp"] = stamp
        e["entry"] = int(entries[i]["entry"])
        wake, = [v for v in unkeyed["poll"] if e["handoff"] <= v <= e["take"]]
        e["poll"] = wake
        long = stamps[i] - stamps[i-1] > 60_000_000
        expected = (path.name.startswith("100-") if steady else
                    (i % 2 == 1) == (path.stem[-1] in "23"))
        assert long == expected
        names = ("stamp", "publish_entry", "handoff", "poll", "take", "restored", "entry")
        segments = list(zip(names, names[1:]))
        if "admit" in e:
            e["ready"], = [v for v in unkeyed["ready"] if e["admit"] <= v <= e["take"]]
            segments += [("poll", "admit"), ("admit", "ready"), ("ready", "take")]
        for start, end in segments + [("handoff", "publish_return"), ("stamp", "entry")]:
            assert e[end] >= e[start]
            groups[f"{start} to {end}"][long].append((e[end] - e[start]) / 1000)
print("stage | 20 ms median/p95 us | 100 ms median/p95 us | median delta us")
for stage, (short, long) in groups.items():
    assert len(short) == len(long) == 100
    def stats(v):
        return f"{median(v):.1f}/{sorted(v)[ceil(.95*len(v))-1]:.1f}"
    print(f"{stage} | {stats(short)} | {stats(long)} | {median(long)-median(short):+.1f}")
