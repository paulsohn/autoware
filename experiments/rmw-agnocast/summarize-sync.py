"""Summarize independently verified synchronizer samples, separating collector timeouts."""
import math
from pathlib import Path
import re
import statistics
import sys

root = Path(sys.argv[1])
print("Batch | Transport | Complete median/p95 ms | Partial median/p95 ms | Child CPU s")
loan_input = "--loan-input" in sys.argv[2:]
for batch in (("large", "repeat", "pair3", "pair4", "ref1", "ref2") if loan_input else ("large", "repeat")):
    modes = ["cyclone", "cdr", "host", "fixed", "zenoh"] if batch in ("large", "repeat") else []
    if batch.startswith("ref"):
        modes = ["cyclone", "zenoh"]
    elif loan_input:
        modes.extend(["byte-copy", "byte-view"])
    for mode in modes:
        log = (root / f"{batch}-{mode}.log").read_text()
        assert "verified 24 synchronized clouds and metadata, 12 complete and 12 timed out, 1966080 input points" in log
        assert "exit_status=0" in log
        if mode in ("fixed", "byte-copy", "byte-view"):
            wakes, = re.findall(r"fixed_wait .*?native=(\d+)", log)
            assert int(wakes) > 0
        if mode.startswith("byte-") or batch.startswith("ref"):
            counts = "views=60 copies=0" if mode == "byte-view" else "views=0 copies=60"
            assert f"sync_input {counts}" in log
        if mode in ("cdr", "host", "fixed", "byte-copy", "byte-view"):
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
