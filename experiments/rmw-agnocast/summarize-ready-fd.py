"""Report the unprofiled branched reader comparison and same-round differences."""
import csv
from math import ceil
from pathlib import Path
from statistics import median
import sys

root = Path(sys.argv[1])
candidate = "clamp" if "--uclamp" in sys.argv[2:] else "ready"


def read_frames(path):
    with path.open() as f:
        rows = list(csv.reader(f))
    assert [int(r[0]) for r in rows] == list(range(60))
    return [int(r[1]) / 1000 for r in rows[10:]]


def quantiles(values):
    return median(values), sorted(values)[ceil(.95 * len(values))-1]


by_period = {}
for ms in ((100,) if "--10hz-only" in sys.argv[2:] else (100, 20)):
    paired = {}
    for mode in ("agnocast", "epoll", candidate):
        times, tap, cpu, rss = [], [], [], []
        runs = []
        for round_id in range(1, 5):
            stem = root / f"{ms}-{mode}-{round_id}"
            samples = read_frames(stem.with_suffix(".csv"))
            times.extend(samples)
            tap.extend(read_frames(Path(f"{stem}-tap.csv")))
            runs.append(quantiles(samples))
            with Path(f"{stem}-resources.csv").open() as f:
                resource, = csv.DictReader(f)
            assert int(resource["verified_frames"]) == 60
            cpu.append(float(resource["child_user_s"]) + float(resource["child_system_s"]))
            rss.append(int(resource["largest_child_maxrss_kib"]) / 1024)
        m, p = quantiles(times)
        if mode == "agnocast":
            native = (m, p)
        paired[mode] = runs
        print(f"{1000//ms} Hz {mode}: sink median/p95 {m:.1f}/{p:.1f} us, "
              f"native ratios {m/native[0]:.2f}/{p/native[1]:.2f}, tap {median(tap):.1f} us, "
              f"CPU {median(cpu):.3f} s, largest child {median(rss):.1f} MiB")
    by_period[ms] = paired
    for col, name in enumerate(("median", "p95")):
        print(candidate, "minus epoll", name, "us:", *[round(b[col]-a[col], 1)
              for a, b in zip(paired["epoll"], paired[candidate])])

if candidate == "clamp":
    for mode in ("epoll", candidate):
        excess = [(by_period[100][mode][i][0] - by_period[20][mode][i][0])
                  - (by_period[100]["agnocast"][i][0] - by_period[20]["agnocast"][i][0])
                  for i in range(4)]
        print(mode, "per-round excess 10 Hz median penalty over native (us):",
              *(f"{value:+.1f}" for value in excess))
