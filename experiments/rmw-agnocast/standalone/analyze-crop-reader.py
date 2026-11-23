"""Match two-edge crop publications and takes, and export realtime stage stamps."""
import csv
import math
import statistics
from collections import defaultdict
import re
import sys
from pathlib import Path

output, *batches = map(Path, sys.argv[1:])
output.mkdir()

def read(path):
    with path.open() as f:
        return [tuple(map(int, row)) for row in csv.reader(f)]

with (output / "frames.csv").open("x") as frames, (output / "launches.csv").open("x") as launches:
    fw = csv.writer(frames, lineterminator="\n")
    lw = csv.writer(launches, lineterminator="\n")
    fw.writerow(("batch", "arm", "round", "seq", "latency_ns", "stamp", "core_begin", "core_end",
                 "epoll_return", "native_loan", "queued", "rcl_wait_return", "take_begin", "take_end",
                 "crop_entry", "crop_exit", "worker_block_return", "rmw_wait_return"))
    lw.writerow(("batch", "arm", "round", "verified_frames", "child_user_s", "child_system_s",
                 "largest_child_maxrss_kib", "minimum_available_mib", "role_cpus", "shutdown_callback_errors"))
    for batch in batches:
        traced = not batch.name.endswith("-controls")
        crop_only = "cropdirect" in batch.name
        rounds = 6 if crop_only and not traced else 2
        arms = ("agnocast", "view", "cropdirect" if crop_only else "source")
        if "event-controls" in batch.name:
            rounds, arms = 6, ("agnocast", "view", "event")
        if "hybrid" in batch.name:
            rounds, arms = 4, ("agnocast", "scoped", "cropdirect", "hybrid")
        if "input-loan" in batch.name:
            rounds, arms = 4, ("agnocast", "source", "sourcedirect", "hybrid")
        for trial in range(1, rounds + 1):
            for arm in arms:
                stem = batch / f"{arm}-{trial}"
                key = (batch.name, arm, trial)
                samples = read(stem.with_suffix(".csv"))
                assert [i for i, _ in samples] == list(range(60))
                log = stem.with_suffix(".log").read_text()
                assert 'source_rate_hz=10.00' in log
                if arm != "agnocast":
                    assert "CIE benchmark announcement KeepLast(16)" in log
                if arm in ("source", "sourcedirect"):
                    assert log.count("host-cloud publisher loans=59 private_copies=0") == 1
                if arm == "event":
                    assert "verified crop CIE worker" in log
                if arm in ("scoped", "hybrid"):
                    assert log.count("host-cloud publisher loans=60 private_copies=0") == 2
                pins = dict(re.findall(r"verified (pub|filter|sub) cpu=([\d+]+)", log))
                if "pinned" in batch.name:
                    assert len(pins) == 3 and len(set(pins.values())) == 3
                else:
                    assert not pins
                with stem.with_name(stem.name + "-resources.csv").open() as f:
                    usage, = csv.DictReader(f)
                assert int(usage["verified_frames"]) == 60
                lw.writerow((*key, *[usage[k] for k in ("verified_frames", "child_user_s", "child_system_s",
                            "largest_child_maxrss_kib")], re.search(r"Minimum host MemAvailable: (\d+)", log)[1],
                             ",".join(pins[k] for k in ("pub", "filter", "sub") if k in pins),
                             log.count("failed to remove registered on_shutdown callback")))
                if traced:
                    writes = read(stem.with_suffix(".publish"))
                    loans = read(stem.with_suffix(".loans"))
                    with stem.with_suffix(".profile").open() as f:
                        callbacks = list(csv.DictReader(f))
                    assert len(writes) == len(callbacks) == 60
                    assert [r[0] for r in writes] == list(range(writes[0][0], writes[0][0] + 60))
                    stamps = [int(r["stamp"]) for r in callbacks]
                    assert stamps == sorted(set(stamps))
                    if arm != "agnocast":
                        takes = read(stem.with_suffix(".takes"))
                        if crop_only:
                            waits = read(stem.with_suffix(".waits"))
                            assert len(waits) == 60
                        assert [r[0] for r in takes] == list(range(1, 61))
                for i, latency in samples:
                    stages = ("",) * 13
                    worker = ("", "")
                    if traced:
                        eid, addr, begin, end = writes[i]
                        cb, cb_end = (int(callbacks[i][k]) for k in ("entry", "exit"))
                        match, = [r for r in loans if (r[2], r[3]) == (eid, addr)
                                  and begin <= r[5] <= cb]
                        wid, lid, _, _, wake, loan = match
                        assert stamps[i] <= begin <= loan <= cb <= cb_end
                        if arm == "cropdirect":
                            assert wake == 0
                            wake = ""
                        else:
                            assert wid == lid and begin <= wake <= loan
                        assert begin <= end
                        inner = ("",) * 4
                        if arm != "agnocast":
                            _, pub, queued, wait, take, done, _ = takes[i]
                            assert stamps[i] <= pub <= begin
                            assert loan <= queued <= wait <= take <= done <= cb
                            inner = (queued, wait, take, done)
                            if crop_only:
                                block, rmw_done = waits[i]
                                assert queued <= rmw_done <= wait
                                if block:
                                    assert begin <= block <= rmw_done
                                    if arm == "cropdirect":
                                        assert block <= loan
                                worker = (block or "", rmw_done)
                        stages = (stamps[i], begin, end, wake, loan, *inner, cb, cb_end, *worker)
                    fw.writerow((*key, i, latency, *stages))

# Report the engineering milestone per independent batch, using only its own native launches.
latencies = defaultdict(list)
cpu = defaultdict(list)
with (output / "frames.csv").open() as frames:
    for row in csv.DictReader(frames):
        if int(row["seq"]) >= 10:
            latencies[row["batch"], row["arm"]].append(int(row["latency_ns"]) / 1000)
with (output / "launches.csv").open() as launches:
    for row in csv.DictReader(launches):
        cpu[row["batch"], row["arm"]].append(float(row["child_user_s"]) + float(row["child_system_s"]))

def percentiles(values):
    return statistics.median(values), sorted(values)[math.ceil(0.95 * len(values)) - 1]

with (output / "summary.csv").open("x") as summary:
    writer = csv.writer(summary, lineterminator="\n")
    writer.writerow(("batch", "arm", "timed_frames", "median_us", "p95_us", "native_median_ratio",
                     "native_p95_ratio", "median_child_cpu_s"))
    for (batch, arm), values in sorted(latencies.items()):
        median, p95 = percentiles(values)
        native_med, native_p95 = percentiles(latencies[batch, "agnocast"])
        writer.writerow((batch, arm, len(values), round(median, 3), round(p95, 3),
                         round(median / native_med, 4), round(p95 / native_p95, 4),
                         round(statistics.median(cpu[batch, arm]), 4)))
