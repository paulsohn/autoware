"""Validate each sequence and summarize per-phase timing and physical publisher pool blocks."""
import csv
from math import ceil
from pathlib import Path
from statistics import median
import sys

if not __debug__:
    raise RuntimeError("Sequence validation requires Python assertions")


def rows(path, tag):
    with path.open() as stream:
        return [row[1:] for row in csv.reader(stream) if row and row[0] == tag]


def timing(values):
    return [round(v / 1000, 1) for v in
            (median(values), sorted(values)[ceil(.95 * len(values)) - 1], max(values))]


def summarize(path, output):
    hz, history = path.name.split("-")
    fast, slow, writer = (path / f"{name}.csv" for name in ("fast", "slow", "writer"))
    arrivals = rows(fast, "take")
    fast_seq = [int(seq) for seq, _ in arrivals]
    slow_seq = [int(seq) for seq, _ in rows(slow, "take")]
    publications = rows(writer, "publish")
    assert [int(row[0]) for row in publications] == list(range(594))
    assert fast_seq == list(range(594)), (path, "healthy reader gaps or duplicates", fast_seq)
    assert rows(fast, "loss") == [["0", "0"], ["1", "0"]]
    assert all(0 <= seq < 594 for seq in slow_seq)
    rejected = 16 if history == "all" else 0
    assert rows(slow, "loss") == [[str(c), str(rejected)] for c in range(2)]
    pools = {(role, int(c), phase): int(n) for role in ("pool", "slow_pool", "fast_pool")
             for c, phase, n in rows(writer, role)}
    polls, remaining = map(int, (path / "cleanup.csv").read_text().strip().split(","))
    assert remaining == 0
    for cycle in range(2):
        start = cycle * 297
        retained = range(24, 280) if history == "all" else range(288, 296)
        expected = [start + i for i in [*range(24), *retained, 296]]
        assert [i for i in slow_seq if start <= i < start+297] == expected, (path, cycle)
        for phase in ("control", "stalled", "recovered"):
            indices = [int(seq) for seq, p, *_ in publications
                       if start <= int(seq) < start+297 and p == phase]
            actual_hz = ""
            if len(indices) > 1:
                span = int(publications[indices[-1]][3]) - int(publications[indices[0]][3])
                actual_hz = round(1e9 * (len(indices)-1) / span, 2)
                assert abs(actual_hz - int(hz)) < int(hz)*.05
            output.writerow([hz, history, cycle, phase, actual_hz, len(indices),
                             sum(i in expected for i in indices), rejected if phase == "stalled" else 0,
                             *timing([int(arrivals[i][1]) for i in indices]),
                             *timing([int(publications[i][2]) for i in indices]),
                             *[pools[role,cycle,key] for role in ("pool","slow_pool","fast_pool")
                               for key in ("before","full","drained")], polls])


output = csv.writer(sys.stdout, lineterminator="\n")
output.writerow(("hz", "history", "cycle", "phase", "actual_hz", "fast_count", "slow_count", "slow_reported_loss",
                 "fast_p50_us", "fast_p95_us", "fast_max_us", "publish_p50_us", "publish_p95_us",
                 "publish_max_us", "pool_before_bytes", "pool_full_bytes", "pool_drained_bytes",
                 "slow_before_bytes", "slow_full_bytes", "slow_drained_bytes",
                 "fast_before_bytes", "fast_full_bytes", "fast_drained_bytes", "cleanup_100ms_polls"))
for hz in (10, 50):
    for history in ("last", "all"):
        summarize(Path(sys.argv[1]) / f"{hz}-{history}", output)
