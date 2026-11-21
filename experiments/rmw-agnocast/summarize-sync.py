"""Summarize independently verified synchronizer samples, separating collector timeouts."""
import math
from pathlib import Path
import re
import statistics
import sys

root = Path(sys.argv[1])
print("Batch | Transport | Complete median/p95 ms | Partial median/p95 ms | Child CPU s")
loan_input = "--loan-input" in sys.argv[2:]
loan_output = "--loan-output" in sys.argv[2:]
for batch in (("large", "repeat", "pair3", "pair4", "ref1", "ref2") if loan_input or loan_output else ("large", "repeat")):
    modes = ["cyclone", "cdr", "host", "fixed", "zenoh"] if batch in ("large", "repeat") else []
    if batch.startswith("ref"):
        modes = ["cyclone", "zenoh"]
    elif loan_output:
        modes = ["byte-view", "output-shared"]
        if batch in ("large", "repeat"):
            modes.append("output-private")
    elif loan_input:
        modes.extend(["byte-copy", "byte-view"])
    for mode in modes:
        log = (root / f"{batch}-{mode}.log").read_text()
        assert "verified 24 synchronized clouds and metadata, 12 complete and 12 timed out, 1966080 input points" in log
        assert "exit_status=0" in log
        if mode in ("fixed", "byte-copy", "byte-view", "output-private", "output-shared"):
            wakes, = re.findall(r"fixed_wait .*?native=(\d+)", log)
            assert int(wakes) > 0
        if mode.startswith(("byte-", "output-")) or batch.startswith("ref"):
            counts = "views=60 copies=0" if mode in ("byte-view", "output-private", "output-shared") else "views=0 copies=60"
            assert f"sync_input {counts}" in log
        if mode.startswith("output-"):
            copies = 24 if mode == "output-private" else 0
            assert re.findall(r"host-cloud publisher loans=(\d+) private_copies=(\d+)", log) == [("24", str(copies))]
        if mode in ("cdr", "host", "fixed", "byte-copy", "byte-view", "output-private", "output-shared"):
            layout = "CDR" if mode == "cdr" else "host-cloud"
            for topic in ("/sync/left", "/sync/right", "/sync/top", "/output"):
                for endpoint in ("reader", "writer"):
                    assert f"rmw_agnocast {layout} {endpoint} {topic}\n" in log
        samples = re.findall(r"^sample,(\d+),(full|partial),(\d+)$", log, re.M)
        assert [int(i) for i, _, _ in samples] == list(range(24))
        stats = []
        # Omit the first four groups, including lazy TF and collector initialization.
        for kind in ("full", "partial"):
            values = sorted(int(ns) / 1e6 for i, group, ns in samples[4:] if group == kind)
            assert len(values) == 10
            stats.append(f"{statistics.median(values):.2f}/{values[math.ceil(.95*len(values))-1]:.2f}")
        user, system = re.search(r"graph_user_s=([.\d]+) graph_sys_s=([.\d]+)", log).groups()
        print(batch, mode, *stats, f"{float(user) + float(system):.3f}", sep=" | ")

if "--profile-output" in sys.argv[2:]:
    print("Pass | Mode | Group | Median reserve/append/publish (us)")
    for run in (1, 2):
        for mode in ("byte-view", "output-private", "output-shared"):
            log = (root / f"profile-{run}-{mode}.log").read_text()
            assert "verified 24 synchronized clouds and metadata" in log
            assert "sync_input views=60 copies=0" in log and "exit_status=0" in log
            if mode.startswith("output-"):
                count = 24 if mode == "output-private" else 0
                assert f"host-cloud publisher loans=24 private_copies={count}\n" in log
            rows = re.findall(r"sync_output bytes=(\d+) reserve_ns=(\d+) append_ns=(\d+) publish_ns=(\d+)", log)
            assert [int(row[0]) for row in rows] == [1572864, 1048576] * 12
            for offset, group in enumerate(("full", "partial")):
                timings = [
                    statistics.median(int(row[i]) / 1000 for row in rows[4 + offset::2])
                    for i in (1, 2, 3)
                ]
                print(run, mode, group, "/".join(f"{t:.1f}" for t in timings), sep=" | ")
    print("Pass | Mode | Full-group median stamp-to-publish/encode/handoff-to-take/restore (us)")
    for run in (1, 2):
        for mode in ("output-private", "output-shared"):
            folder = root / f"trace-{run}-{mode}"
            assert "verified 24 synchronized clouds and metadata" in (folder / "graph.log").read_text()
            by_stamp = {}
            for role in ("writer", "reader"):
                for line in (folder / f"{role}.csv").read_text().splitlines():
                    event, stamp, timestamp = line.split(",")
                    if event in ("publish_entry", "handoff", "take", "restored"):
                        record = by_stamp.setdefault(int(stamp), {"stamp": int(stamp)})
                        assert event not in record
                        record[event] = int(timestamp)
            assert len(by_stamp) == 24
            rows = [by_stamp[key] for key in sorted(by_stamp)]
            stages = ("stamp", "publish_entry", "handoff", "take", "restored")
            assert all(r[a] <= r[b] for r in rows for a, b in zip(stages, stages[1:]))
            medians = [statistics.median((r[b] - r[a]) / 1000 for r in rows[4::2])
                       for a, b in zip(stages, stages[1:])]
            print(run, mode, "/".join(f"{v:.1f}" for v in medians), sep=" | ")
