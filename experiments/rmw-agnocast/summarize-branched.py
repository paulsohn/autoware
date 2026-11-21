"""Report the four rotated runs for a single offered load."""
import csv
from math import ceil
from pathlib import Path
from statistics import median
import sys

root = Path(sys.argv[1])
def report(prefix, modes, rounds):
    for mode in modes:
        cpu, rss = [], []
        samples = {"tap": [], "sink": []}
        run_medians = []
        for n in range(1, rounds + 1):
            base = root / f"{prefix}{mode}-{n}"
            for label, suffix in (("tap", "-tap.csv"), ("sink", ".csv")):
                with Path(f"{base}{suffix}").open() as f:
                    rows = list(csv.reader(f))
                assert [int(row[0]) for row in rows] == list(range(60))
                values = [int(row[1]) / 1e6 for row in rows[10:]]
                assert min(values) > 0
                samples[label].extend(values)
                if label == "sink":
                    run_medians.append(median(values))
            with Path(f"{base}-resources.csv").open() as f:
                usage, = csv.DictReader(f)
            assert int(usage["verified_frames"]) == 60
            cpu.append(float(usage["child_user_s"]) + float(usage["child_system_s"]))
            rss.append(int(usage["largest_child_maxrss_kib"]) / 1024)
        stats = []
        for values in samples.values():
            stats.extend((median(values), sorted(values)[ceil(.95 * len(values)) - 1]))
        if mode == "agnocast":
            native = stats
            native_runs = run_medians
        print(mode, "tap and sink median/p95 ms", *(f"{v:.3f}" for v in stats),
              "sink/native", *(f"{a/b:.2f}" for a, b in zip(stats[2:], native[2:])),
              "child CPU seconds", f"{median(cpu):.3f}", "largest child MiB", f"{median(rss):.1f}")
        if mode == "fixed":
            print("paired sink median ratios", *(f"{a/b:.2f}" for a, b in zip(run_medians, native_runs)))

if (root / "100-fixed-1.csv").is_file():
    for period in (100, 20):
        print(f"Confirmation at {1000 // period} Hz")
        report(f"{period}-", ("agnocast", "fixed"), 2)
else:
    report("", ("agnocast", "fixed", "cyclone", "zenoh"), 4)
