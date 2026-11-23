"""Compare complete-group latency and four-second bursts to the original laptop runs."""
import csv
import math
import statistics
import sys
from pathlib import Path

here = Path(__file__).resolve().parent


def measure(path):
    with path.open() as samples:
        rows = list(csv.DictReader(samples))
    assert [int(row["seq"]) for row in rows] == list(range(600)), path
    rows = rows[20::2]
    track = [int(row["tracked_ns"]) / 1e6 for row in rows]
    cloud = [int(row["cloud_ns"]) / 1e6 for row in rows]
    post = [t - c for t, c in zip(track, cloud)]
    slow = [t > 10 for t in track]
    windows = [sum(slow[i:i + 20]) for i in range(len(rows) - 19)]
    start = windows.index(max(windows))

    def p95(values):
        return sorted(values)[math.ceil(.95 * len(values)) - 1]

    return (statistics.median(track), p95(track), sum(slow), max(windows),
            int(rows[start]["seq"]), p95(cloud), p95(post))


def report(root, label):
    print(label)
    print("pair mode median_ms p95_ms over10 peak_over10_of20 window_first_seq cloud_p95_ms post_cloud_p95_ms")
    for pair in sorted(root.iterdir()):
        if not pair.is_dir():
            continue
        tail = {}
        for mode in ("native", "stock"):
            values = measure(pair / mode / "samples.csv")
            median, tail[mode], total, peak, first, cloud, post = values
            window = first if peak else "-"
            print(f"{pair.name} {mode} {median:.3f} {tail[mode]:.3f} {total} {peak} {window} {cloud:.3f} {post:.3f}")
        print(f"pair={pair.name} p95_ratio={tail['stock'] / tail['native']:.3f}")


report(here / "sustained-results", "Original")
report(Path(sys.argv[1]), "Fresh")
