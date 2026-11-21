"""Verify paired three-LiDAR runs and report pooled nearest-rank latency in ms."""

import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def bounds(values):
    return f"{min(values):.2f}..{max(values):.2f}"


def quantiles(values):
    values = sorted(values)
    return "/".join(f"{values[math.ceil(len(values) * p) - 1]:.3f}" for p in (0.5, 0.95))


def summarize(root, mode, condition):
    jitter = condition == "jitter"
    samples = defaultdict(list)
    releases = defaultdict(list)
    cpu, peak, available = [], [], []
    for round_id in (1, 2):
        path = root / f"{round_id}-{mode}-{condition}.log"
        log = path.read_text()
        require(log.count("process has finished cleanly") == 5 and
                "process has died" not in log and "[ERROR]" not in log, path)
        for sensor in ("left", "right", "top"):
            require(f"Loaded node '/crop_{sensor}'" in log, path)
        counts = "30 synchronized clouds and metadata, 6 complete and 24" if jitter else (
            "24 synchronized clouds and metadata, 12 complete and 12")
        require(f"verified {counts} timed out, 1966080 input points" in log, path)
        views = "views=60 copies=0" if mode == "stock" else "views=0 copies=60"
        require(f"sync_input {views}" in log, path)
        rows = re.findall(r"top_release_ns,(\d+),(\d+)", log)
        require(sorted(int(k) for k, _ in rows) == list(range(0, 24, 2)), path)
        for key, ns in rows:
            ms = int(ns) / 1e6
            target = (50 if int(key) % 4 else 15) if jitter else 0
            require((ms >= 40) == (target == 50), path)
            releases[target].append(ms)
        seen = Counter()
        expected = []
        for seq in range(24):
            kinds = ("partial",) if seq % 2 else ("full",)
            if jitter and seq % 4 == 2:
                kinds = ("late_pair", "late_top")
            expected.extend((seq, kind) for kind in kinds)
        for key, kind, ns in re.findall(r"sample,(\d+),(\w+),(\d+)", log):
            seq = int(key)
            seen[seq, kind] += 1
            if seq >= 4:
                samples[kind].append(int(ns) / 1e6)
        require(seen == Counter(expected), path)
        user, system = re.search(r"graph_user_s=([\d.]+) graph_sys_s=([\d.]+)", log).groups()
        cpu.append(float(user) + float(system))
        peak.append(int(re.search(r"scope_peak_bytes=(\d+)", log)[1]) / 2**20)
        available.append(int(re.search(r"Minimum host MemAvailable: (\d+)", log)[1]))
    print(mode, condition, *(quantiles(samples[k]) if k in samples else "-"
          for k in ("full", "partial", "late_pair", "late_top")), bounds(cpu),
          bounds(peak), min(available), " ".join(f"{k}ms={bounds(v)}" for k, v in sorted(releases.items())))


if __name__ == "__main__":
    print("mode condition full_ms partial_ms late_pair_ms late_top_ms cpu_s peak_MiB min_host_MiB release_ms")
    for middleware in ("stock", "cyclone", "zenoh"):
        for name in ("baseline", sys.argv[2] if len(sys.argv) > 2 else "jitter"):
            summarize(Path(sys.argv[1]), middleware, name)
