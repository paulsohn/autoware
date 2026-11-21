"""Verify the three-LiDAR fan-out matrix and pool acquisition-to-sink measurements."""
import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path


def require(ok, path):
    if not ok:
        raise ValueError(f"invalid or incomplete run: {path}")


def p50_p95(values):
    ordered = sorted(values)
    return "/".join(f"{ordered[math.ceil(q * len(values)) - 1]:.3f}" for q in (0.5, 0.95))


def samples(text, tag, expected, path):
    rows = re.findall(rf"\b{tag},(\d+),(\w+),(\d+)", text)
    require(Counter((int(seq), kind) for seq, kind, _ in rows) == Counter(expected), path)
    return [(kind, int(ns) / 1e6) for seq, kind, ns in rows if int(seq) >= 4]


def summarize(root, mode, condition):
    branched, jitter = condition != "control", condition == "jitter"
    expected = []
    for i in range(24):
        kinds = ("late_pair", "late_top") if jitter and i % 4 == 2 else ("partial" if i % 2 else "full",)
        expected.extend((i, k) for k in kinds)
    rounds = 6 if condition in ("regions", "fast") and mode in ("native", "stock") else 2
    fused, branches = defaultdict(list), defaultdict(list)
    cpu, peak, available = [], [], []
    for trial in range(1, rounds + 1):
        directory = root / "double-rate" if condition == "fast" else root
        path = directory / f"{trial}-{mode}-{condition}.log"
        text = path.read_text()
        require("[ERROR]" not in text and "process has died" not in text and
                text.count("process has finished cleanly") == (7 if branched else 5), path)
        require(f"verified {len(expected)} synchronized clouds and metadata" in text, path)
        require(("sync_input views=60 copies=0" if mode in ("native", "stock")
                 else "sync_input views=0 copies=60") in text, path)
        if mode == "native":
            routes = re.findall(r"native_route (\S+) writers=(\d+) readers=(\d+)", text)
            needed = {"/raw/sync/" + t for t in ("left", "right", "top")}
            needed |= {"/sync/" + t for t in ("left", "right", "top")} | {"/output"}
            if branched:
                needed |= {"/region/near", "/region/far"}
            require({t for t, _, _ in routes} == needed and all(
                int(w) == 1 and int(r) == (3 if branched and t == "/output" else 1)
                for t, w, r in routes), path)
        rate = 20 if condition == "fast" else 10
        require(abs(float(re.search(r"fast_source_hz=([\d.]+)", text)[1]) - rate) < rate/100, path)
        top = re.findall(r"top_release_ns,(\d+),(\d+)", text)
        require([int(i) for i, _ in top] == list(range(0, 24, 2)) and all(
            (int(delay) >= 40000000) == (jitter and int(i) % 4 == 2) for i, delay in top), path)
        for kind, ms in samples(text, "sample", expected, path):
            fused[kind].append(ms)
        for kind, ms in samples(text, "regions", expected if branched else [], path):
            branches[kind].append(ms)
        user, system = re.search(r"graph_user_s=([\d.]+) graph_sys_s=([\d.]+)", text).groups()
        cpu.append(float(user) + float(system))
        peak.append(int(re.search(r"scope_peak_bytes=(\d+)", text)[1]) / 2**20)
        available.append(int(re.search(r"Minimum host MemAvailable: (\d+)", text)[1]))
    end = branches if branched else fused
    print(mode, condition, f"runs={rounds}", f"full_n={len(end['full'])}",
          f"fusion_full={p50_p95(fused['full'])}",
          *(f"{kind}={p50_p95(values)}" for kind, values in sorted(end.items())),
          f"cpu_s={min(cpu):.2f}..{max(cpu):.2f}", f"peak_MiB={min(peak):.1f}..{max(peak):.1f}",
          f"min_host_MiB={min(available)}")


if __name__ == "__main__":
    for mode in ("native", "stock", "cyclone", "zenoh"):
        for condition in ("control", "regions", "fast", "jitter"):
            summarize(Path(sys.argv[1]), mode, condition)
