"""Match far-branch acquisitions in each control to those produced during its paired stall."""
import math
import re
import sys
from pathlib import Path


def check(ok, path):
    if not ok:
        raise ValueError(f"invalid or incomplete run: {path}")


def percentiles(values):
    ordered = sorted(values)
    return "/".join(f"{ordered[math.ceil(q * len(values)) - 1] / 1e6:.2f}" for q in (.50, .95))


def read_run(path, mode, slow):
    log = path.read_text()
    check("verified 32 synchronized clouds and metadata, 16 complete and 16 timed out" in log
          and log.count("process has finished cleanly") == 7 and "[ERROR]" not in log, path)
    views = "80 copies=0" if mode in ("native", "stock") else "0 copies=80"
    check("sync_input views=" + views in log, path)
    if mode == "native":
        routes = {topic: (int(w), int(r)) for topic, w, r in
                  re.findall(r"native_route (\S+) writers=(\d+) readers=(\d+)", log)}
        expected = {prefix + sensor: (1, 1) for prefix in ("/raw/sync/", "/sync/")
                    for sensor in ("left", "right", "top")}
        expected.update({"/output": (1, 3), "/region/near": (1, 1), "/region/far": (1, 1)})
        check(routes == expected, path)
    check(abs(float(re.search(r"fast_source_hz=([\d.]+)", log)[1]) - 20) < .2, path)
    near, far = {}, {}
    for seq, kind, branch, stamp, age in re.findall(
            r"branch,(\d+),(\w+),([01]),(\d+),(-?\d+)", log):
        channel = (near, far)[int(branch)]
        seq = int(seq)
        check(seq not in channel and kind == ("partial" if seq % 2 else "full"), path)
        channel[seq] = int(stamp), int(age)
    for seq in range(32):
        check(near[seq][0] == far[seq][0] and far[seq][1] >= 0, path)
        check(near[seq][1] >= 0 or (slow and 4 <= seq < 26), path)
    check(len(near) == len(far) == 32, path)
    check(all(0 <= near[seq][1] < 100_000_000 for seq in range(26, 32)), path)
    markers = [int(v) for v in re.findall(r"near_stall_(?:start|end)_ns=(\d+)", log)]
    check(len(markers) == (2 if slow else 0), path)
    if slow:
        check(markers[1] - markers[0] >= 900_000_000, path)
    user, system = re.search(r"graph_user_s=([\d.]+) graph_sys_s=([\d.]+)", log).groups()
    peak = int(re.search(r"scope_peak_bytes=(\d+)", log)[1]) / 2**20
    available = int(re.search(r"Minimum host MemAvailable: (\d+)", log)[1])
    return near, far, markers, float(user) + float(system), peak, available


def summarize(root, mode, rounds):
    control, impaired, cpu, peak, available = [], [], [], [], []
    partial_control, partial_impaired, worst_far = [], [], []
    missing, recovery, oldest = [], [], []
    for trial in range(1, rounds + 1):
        healthy = read_run(root / f"{trial}-{mode}-healthy.log", mode, False)
        slow = read_run(root / f"{trial}-{mode}-slow-near.log", mode, True)
        near, far, (start, end) = slow[:3]
        active = [seq for seq in range(4, 32) if start <= far[seq][0] <= end]
        worst_far.append(max(age for _, age in far.values()) / 1e6)
        ids = [seq for seq in active if seq % 2 == 0]
        partial_control.extend(healthy[1][seq][1] for seq in active if seq % 2)
        partial_impaired.extend(far[seq][1] for seq in active if seq % 2)
        check(ids, mode)
        control.extend(healthy[1][seq][1] for seq in ids)
        impaired.extend(far[seq][1] for seq in ids)
        drops = [seq for seq, (_, age) in sorted(near.items()) if age < 0]
        missing.append(len(drops))
        arrivals = sorted((stamp + age, age) for stamp, age in near.values() if age >= 0)
        last_stale = max((t for t, age in arrivals if age >= 100_000_000), default=start)
        fresh = next(t for t, age in arrivals if t >= end and t > last_stale)
        recovery.append((fresh - end) / 1e6)
        oldest.append(max(age for _, age in arrivals) / 1e6)
        for run in (healthy, slow):
            cpu.append(run[3])
            peak.append(run[4])
            available.append(run[5])
        print(mode, trial, "missing_seq=" + ",".join(map(str, drops)), f"matched_full={len(ids)}",
              f"recovery_ms={recovery[-1]:.2f}",
              "control_ms=" + percentiles([healthy[1][i][1] for i in ids]),
              "stall_ms=" + percentiles([far[i][1] for i in ids]))
    print(mode, f"pairs={rounds} full_n={len(control)} far_healthy_ms={percentiles(control)}",
          f"far_slow_ms={percentiles(impaired)} near_missing={sum(missing)}/{rounds * 32}",
          f"partial_healthy_ms={percentiles(partial_control)} partial_slow_ms={percentiles(partial_impaired)}",
          f"max_far_slow_ms={max(worst_far):.2f}",
          f"oldest_near_ms={min(oldest):.1f}..{max(oldest):.1f}",
          f"recovery_ms={min(recovery):.1f}..{max(recovery):.1f}",
          f"cpu_s={min(cpu):.2f}..{max(cpu):.2f} peak_MiB={max(peak):.1f}",
          f"min_host_MiB={min(available)}")


if __name__ == "__main__":
    for transport, count in (("native", 8), ("stock", 8), ("cyclone", 2), ("zenoh", 2)):
        summarize(Path(sys.argv[1]), transport, count)
