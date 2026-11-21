"""Check the assembled graph and report post-warmup full/partial latency separately."""
import math
import re
import statistics
import sys
from pathlib import Path

root = Path(sys.argv[1])
print("Mode | complete median/p95 ms | partial median/p95 ms | graph CPU s (runs 1/2) | minimum available host MiB")
for mode in ("plain", "stock", "runner", "cyclone", "zenoh"):
    groups = {"full": [], "partial": []}
    memory = []
    cpu = []
    for run in (1, 2):
        log = (root / f"{run}-{mode}.log").read_text()
        assert "verified 24 synchronized clouds and metadata, 12 complete and 12 timed out, 1966080 input points" in log
        assert "process has died" not in log and "[ERROR]" not in log
        assert log.count("process has finished cleanly") == 5
        for sensor in ("left", "right", "top"):
            assert f"Loaded node '/crop_{sensor}'" in log
        if mode in ("plain", "stock", "runner"):
            topics = [f"{prefix}/sync/{s}" for s in ("left", "right", "top") for prefix in ("", "/raw")]
            for topic in topics + ["/output"]:
                for endpoint in ("reader", "writer"):
                    assert f"rmw_agnocast host-cloud {endpoint} {topic}\n" in log
        if mode in ("stock", "runner"):
            assert "sync_input views=60 copies=0" in log
        if mode == "runner":
            assert re.search(r"fixed_wait .*?native=[1-9]\d*", log)
        else:
            assert "fixed_wait " not in log
        rows = re.findall(r"sample,(\d+),(full|partial),(\d+)", log)
        assert [(int(i), k) for i, k, _ in rows] == [(i, ("full", "partial")[i % 2]) for i in range(24)]
        for _, kind, ns in rows[4:]:
            groups[kind].append(int(ns) / 1e6)
        user, system = re.search(r"graph_user_s=([.\d]+) graph_sys_s=([.\d]+)", log).groups()
        cpu.append(float(user) + float(system))
        memory.append(int(re.search(r"Minimum host MemAvailable: (\d+) MiB", log)[1]))
    stats = []
    for values in groups.values():
        stats.append(f"{statistics.median(values):.3f}/{sorted(values)[math.ceil(.95 * len(values))-1]:.3f}")
    print(mode, *stats, "/".join(f"{v:.3f}" for v in cpu), min(memory), sep=" | ")
