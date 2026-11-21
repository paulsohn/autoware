"""Summarize paired native and RMW three-LiDAR launches on this host."""
import math
import re
import runpy
import sys
from pathlib import Path

helpers = runpy.run_path(str(Path(__file__).with_name("summarize-overflow.py")))
root = Path(sys.argv[1])
check = helpers["check"]
topics = {"/raw/sync/left", "/raw/sync/right", "/raw/sync/top",
          "/sync/left", "/sync/right", "/sync/top", "/output"}


def percentile(values, p):
    return sorted(values)[math.ceil(p * len(values)) - 1]


def verify_native(log, path):
    routes = re.findall(r"native_route (\S+) writers=(\d+) readers=(\d+)", log)
    check(len(routes) == 7 and {t for t, w, r in routes if int(w) and int(r)} == topics, path)
    check(set(re.findall(r"native_crop /crop_(\w+)", log)) == {"left", "right", "top"}, path)


print("Nominal times in ms, pooled nearest-rank median/p95 after four warm-up groups per launch")
for mode in ("native-default", "native", "stock", "cyclone", "zenoh"):
    samples = {"full": [], "partial": []}
    cpu, peak = [], []
    trials = range(1, 7) if mode in ("native", "stock") else (1, 2)
    for trial in trials:
        for condition in ("nominal", "overflow"):
            if condition == "overflow" and (trial > 2 or mode == "native-default"):
                continue
            directory = root if trial <= 2 else root / "confirmation"
            path = directory / f"{trial}-{mode}-{condition}.log"
            log = path.read_text()
            check(log.count("process has finished cleanly") == 5 and "[ERROR]" not in log, path)
            if mode.startswith("native"):
                verify_native(log, path)
            if condition == "overflow":
                helpers["read"](path, True)
                continue
            check("verified 24 synchronized clouds and metadata, 12 complete and 12 timed out" in log, path)
            check(("views=60 copies=0" if mode in ("native-default", "native", "stock") else "views=0 copies=60") in log, path)
            rows = re.findall(r"sample,(\d+),(\w+),(\d+)", log)
            check([(int(seq), k) for seq, k, _ in rows] ==
                  [(seq, "partial" if seq % 2 else "full") for seq in range(24)], path)
            full = [int(age)/1e6 for seq, kind, age in rows if int(seq) >= 4 and kind == "full"]
            print(f"trial={trial} {mode} nominal_full_ms={percentile(full,.5):.3f}/{percentile(full,.95):.3f}")
            for seq, kind, age in rows:
                if int(seq) >= 4:
                    samples[kind].append(int(age)/1e6)
            user, system = re.search(r"graph_user_s=([\d.]+) graph_sys_s=([\d.]+)", log).groups()
            cpu.append(float(user) + float(system))
            peak.append(int(re.search(r"scope_peak_bytes=(\d+)", log)[1])/2**20)
    print(mode, *(f"{k}={percentile(v,.5):.3f}/{percentile(v,.95):.3f}" for k,v in samples.items()),
          f"launch_cpu_s={min(cpu):.2f}..{max(cpu):.2f} peak_MiB={min(peak):.1f}..{max(peak):.1f}")
