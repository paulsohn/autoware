"""Verify the timed recurrence and compare actual gaps on the branched graph."""
import csv
from collections import defaultdict
from math import ceil
from pathlib import Path
from statistics import median
import sys

root = Path(sys.argv[1])
iterations = int(sys.argv[2])
assert iterations in (0, 4096)
checksum = 1
for _ in range(iterations):
    checksum = ((checksum ^ (checksum >> 17)) * 0x9e3779b97f4a7c15) & ((1 << 64) - 1)


def load_csv(path):
    with path.open() as f:
        return [{key: int(value) for key, value in row.items()} for row in csv.DictReader(f)]


for mode in ("agnocast", "fixed"):
    pooled = defaultdict(lambda: [[], []])
    deltas = []
    arrival_deltas = []
    for n in range(1, 5):
        base = root / f"{mode}-{n}"
        work = load_csv(base.with_suffix(".profile.work"))
        profile = load_csv(base.with_suffix(".profile"))
        stamps = list(map(int, base.with_suffix(".stamps").read_text().splitlines()))
        assert len(stamps) == len(set(stamps)) == 60
        assert [r["stamp"] for r in work] == [r["stamp"] for r in profile] == stamps
        for file in (base.with_suffix(".csv"), root / f"{base.name}-tap.csv"):
            with file.open() as f:
                rows = list(csv.reader(f))
            assert [int(row[0]) for row in rows] == list(range(60))
        local = [[], []]
        arrivals = [[], []]
        for i, r in enumerate(work):
            assert r["first_result"] == r["second_result"] == checksum
            if i < 10:
                continue
            gap = stamps[i] - stamps[i-1]
            long = gap > 60_000_000
            assert long == ((i % 2 == 1) == (n in (2, 3)))
            assert abs(gap - (100_000_000 if long else 20_000_000)) < 10_000_000
            assert min(r["cpu_before"], r["cpu_after"]) >= 0
            same = work[i-1]["cpu_after"] == r["cpu_before"] == r["cpu_after"]
            arrival = (profile[i]["entry"] - stamps[i])/1000
            arrivals[long].append(arrival)
            for group in (("all", "same") if same else ("all",)):
                for field in ("first_wall", "first_cpu", "second_wall", "second_cpu"):
                    pooled[group, field][long].append(r[field] / 1000)
                pooled[group, "arrival"][long].append(arrival)
            local[long].append(r["first_cpu"]/1000)
        assert len(local[0]) == len(local[1]) == 25
        arrival_deltas.append(median(arrivals[1])-median(arrivals[0]))
        deltas.append(median(local[1])-median(local[0]))
    print(mode, "first-block long minus short CPU by round (us)", *(f"{v:+.2f}" for v in deltas))
    print(mode, "arrival long minus short by round (us)", *(f"{v:+.2f}" for v in arrival_deltas))
    for (group, field), (short, long) in pooled.items():
        def q(v):
            return f"{median(v):.2f}/{sorted(v)[ceil(.95*len(v))-1]:.2f}" if v else "n/a"
        print(group, field, "20/100 ms counts", len(short), len(long),
              "median/p95 us", q(short), q(long))
