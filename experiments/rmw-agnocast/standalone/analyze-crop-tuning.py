"""Collect independent crop frames, callback timings and resources from complete batches."""
import csv
import re
import sys
from pathlib import Path

output = Path(sys.argv[1])
output.mkdir()
with (output / "frames.csv").open("x") as frames, (output / "launches.csv").open("x") as launches:
    fw = csv.writer(frames, lineterminator="\n")
    lw = csv.writer(launches, lineterminator="\n")
    fw.writerow(("batch", "arm", "round", "seq", "latency_ns", "to_crop_ns", "in_crop_ns",
                 "after_crop_ns", "crop_cpu_ns", "to_core_ns", "publish_core_ns", "core_return_to_crop_ns"))
    lw.writerow(("batch", "arm", "round", "verified_frames", "child_user_s", "child_system_s",
                 "largest_child_maxrss_kib", "minimum_available_mib"))
    for directory in map(Path, sys.argv[2:]):
        batch = directory.name.removeprefix("crop-")
        trace = batch.endswith("trace")
        route = batch.startswith(("route-", "source-"))
        tuned = "source" if batch.startswith("source-") else "direct"
        arms = ("agnocast", "view", tuned) if route else ("agnocast", "view", "loan", "scoped")
        rounds = 2 if trace or batch == "source-confirm" else 6 if route else 4
        for trial in range(1, rounds + 1):
            for arm in arms:
                stem = directory / f"{arm}-{trial}"
                with stem.with_suffix(".csv").open() as f:
                    samples = list(csv.reader(f))
                assert [int(s) for s, _ in samples] == list(range(60))
                log = stem.with_suffix(".log").read_text()
                assert "source_rate_hz=10.00" in log
                if arm != "agnocast":
                    assert "CIE benchmark announcement KeepLast(16)" in log
                if arm in ("loan", "scoped"):
                    n = 60 if arm == "scoped" else 59
                    assert log.count(f"host-cloud publisher loans={n} private_copies=0") == 2
                if arm == "source":
                    assert log.count("host-cloud publisher loans=59 private_copies=0") == 1
                key = (batch, arm, trial)
                with stem.with_name(stem.name + "-resources.csv").open() as f:
                    usage, = csv.DictReader(f)
                assert int(usage["verified_frames"]) == 60
                memory = re.search(r"Minimum host MemAvailable: (\d+) MiB", log)
                lw.writerow((*key, usage["verified_frames"], usage["child_user_s"],
                             usage["child_system_s"], usage["largest_child_maxrss_kib"], memory[1]))
                if trace:
                    with stem.with_suffix(".profile").open() as f:
                        rows = list(csv.DictReader(f))
                    stamps = [int(r["stamp"]) for r in rows]
                    assert len(rows) == 60 and stamps == sorted(set(stamps))
                writer_trace = batch == "source-trace"
                if writer_trace:
                    with stem.with_suffix(".publish").open() as f:
                        writes = [tuple(map(int, r)) for r in csv.reader(f)]
                    assert len(writes) == 60 and all(a[0] < b[0] for a,b in zip(writes, writes[1:]))
                for i, (_, latency) in enumerate(samples):
                    parts = ("", "", "", "")
                    if trace:
                        r = rows[i]
                        entry, end = int(r["entry"]), int(r["exit"])
                        assert stamps[i] < entry < end
                        # The downstream process can enter its callback before the crop returns.
                        parts = (entry - stamps[i], end - entry, stamps[i] + int(latency) - end,
                                 int(r["cpu_after"]) - int(r["cpu_before"]))
                    writer_parts = ("", "", "")
                    if writer_trace:
                        _, address, start, stop = writes[i]
                        assert address and stamps[i] <= start <= stop and start <= entry
                        writer_parts = (start - stamps[i], stop - start, entry - stop)
                    fw.writerow((*key, i, latency, *parts, *writer_parts))
