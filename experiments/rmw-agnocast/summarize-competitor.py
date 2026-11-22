"""Join the independent timer window, near callback entries and paced tracker replay."""
from pathlib import Path
import re
import runpy
import sys

root = Path(sys.argv[1])
metrics = runpy.run_path(str(Path(__file__).with_name("summarize-isolation.py")))
tracks = dict(line.split(" ", 1) for line in (root / "tracker.txt").read_text().splitlines())
saturated = (root / "three").is_dir()
affinity = None
print("transport executor round busy_cpu_ms overlap near/32 near_p50/p95_ms far_p50/p95_ms "
      "fresh_gap_ms confirmed within_100ms same_uuid graph_cpu_s peak_MiB min_host_MiB" +
      (" second_cpu_ms common_window_ms timer_cpus" if saturated else ""))
workers = (root / "two").is_dir() and not saturated
for mode in (("stock",) if workers else ("native", "stock", "cyclone", "zenoh")):
    executors = ("single", "two") if workers else ("single", "cie")
    if saturated:
        executors = ("two", "three", "cie")
    for executor in (("cie",) if mode == "native" else executors):
        for trial in (1, 2):
            path = root / executor / f"{trial}-{mode}-slow-near.log"
            near, far, (start, end), cpu, peak, available = metrics["read_run"](path, mode, True)
            log = path.read_text()
            costs = re.findall(r"competitor_cpu_ns=(\d+)", log)
            assert len(costs) == 1 and log.count("near_reader_depth=8 near_writer_depth=5") == 1
            entries = list(map(int, re.findall(r"near_entry_ns=(\d+)", log)))
            ages = [age for _, age in near.values() if age >= 0]
            assert len(entries) == len(ages) and len([t for t in entries if t <= start]) == 5
            extra = []
            if saturated:
                second_start, second_end = map(int, re.findall(r"second_stall_(?:start|end)_ns=(\d+)", log))
                second_cpu, = re.findall(r"second_cpu_ns=(\d+)", log)
                masks = [tuple(sorted(map(int, re.findall(fr"timer_{i}_allowed_cpu=(\d+)", log))))
                         for i in (0, 1)]
                if affinity is None:
                    affinity = masks[0]
                assert len(set(affinity)) == 3 and all(mask == affinity for mask in masks)
                start, end = max(start, second_start), min(end, second_end)
                assert end - start > 800_000_000, path
                extra = [f"{int(second_cpu)/1e6:.1f}", f"{(end-start)/1e6:.1f}",
                         ",".join(map(str, affinity))]
            overlap = sum(start < t < end for t in entries)
            track = tracks.pop(f"trace={executor}/{path.stem}")
            assert f"frames={len(ages)} " in track
            result = [re.search(key + r"=(\d+)", track)[1] for key in
                      ("confirmed", "within_100ms", "occluded_common")]
            gap = metrics["freshness"](near, start, end, 100_000_000)[0]
            print(mode, executor, trial, f"{int(costs[0])/1e6:.1f}", overlap, len(ages),
                  metrics["percentiles"](ages), metrics["percentiles"]([a for _, a in far.values()]),
                  f"{gap:.1f}", *result, f"{cpu:.2f}", f"{peak:.1f}", available, *extra)
assert not tracks
