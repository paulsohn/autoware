"""Compare near-branch recovery, deliberate discards and replayed track identity."""
import re
import runpy
import sys
from pathlib import Path

root = Path(sys.argv[1])
isolation = runpy.run_path(str(Path(__file__).with_name("summarize-isolation.py")))
tracks = {line.split()[0]: line for line in (root / "tracker.txt").read_text().splitlines()}
print("mode policy round near/32 rejects oldest_ms recovery_ms far_full_p50/p95_ms "
      "busy_cpu_ms same_uuid confirmed within_100ms missed_targets peak_MiB min_host_MiB")
for mode in ("native", "stock", "cyclone", "zenoh"):
    for policy in ("keep", "reject"):
        for trial in (1, 2):
            path = root / policy / f"{trial}-{mode}-slow-near.log"
            near, far, (start, end), _, peak, available = isolation["read_run"](path, mode, True)
            log = path.read_text()
            assert log.count("near_reader_depth=8 near_writer_depth=5") == 1
            costs = list(map(int, re.findall(r"near_busy_cpu_ns=(\d+)", log)))
            assert len(costs) == 6 and min(costs) > 0
            rejects = re.findall(r"near_rejected_stamp=(\d+) entry_age_ns=(\d+)", log)
            missing = {stamp for stamp, age in near.values() if age < 0}
            assert len({s for s, _ in rejects}) == len(rejects)
            assert all(int(s) in missing and int(a) > 100_000_000 for s, a in rejects)
            assert bool(rejects) == (policy == "reject")
            arrivals = [(stamp + age, age) for stamp, age in near.values() if age >= 0]
            recovery = min(t for t, age in arrivals if t >= end and age <= 100_000_000) - end
            full = [age for i, (stamp, age) in far.items() if not i % 2 and start <= stamp <= end]
            track = tracks.pop(f"trace={policy}/{path.stem}")
            assert f"frames={len(arrivals)} " in track
            same, confirmed, timely = [re.search(key + r"=(\d+)", track)[1] for key in
                                       ("occluded_common", "confirmed", "within_100ms")]
            missed = re.search(r"missed=(\[.*?\])", track)[1].replace(" ", "")
            print(mode, policy, trial, len(arrivals), len(rejects),
                  f"{max(age for _, age in arrivals)/1e6:.2f}", f"{recovery/1e6:.2f}",
                  isolation["percentiles"](full), f"{sum(costs)/1e6:.1f}", same,
                  confirmed, timely, missed, f"{peak:.1f}", available)
assert not tracks
