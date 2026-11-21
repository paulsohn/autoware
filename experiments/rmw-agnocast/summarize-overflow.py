"""Validate independent cloud/metadata histories and report recovery after a finite overload."""
import re
import sys
from pathlib import Path


def check(ok, message):
    if not ok:
        raise ValueError(message)


def read(path, slow):
    log = path.read_text()
    for sensor in ("left", "right", "top"):
        check(f"Loaded node '/crop_{sensor}'" in log, path)
    check(log.count("process has finished cleanly") == 5 and "[ERROR]" not in log, path)
    check("views=260 copies=0" in log if "stock" in path.name else "views=0 copies=260" in log, path)
    counts = re.search(r"extended expected=130 clouds=(\d+) metadata=(\d+) paired=(\d+) resume_ns=(\d+)", log)
    clouds, infos, paired, resume = map(int, counts.groups())
    expected = [(i, k) for i in range(104) for k in
                (("late_pair", "late_top") if i % 4 == 2 else ("partial" if i % 2 else "full",))]
    rows = [(int(i), k, int(c), int(m), int(stamp), int(age))
            for i, k, c, m, stamp, age in re.findall(
                r"delivery,(\d+),(\w+),([01]),([01]),(\d+),(-?\d+)", log)]
    check([(r[0], r[1]) for r in rows] == expected, path)
    check((sum(r[2] for r in rows), sum(r[3] for r in rows), sum(r[2]*r[3] for r in rows))
          == (clouds, infos, paired), path)
    check(all(r[2] and r[3] for r in rows if r[0] >= 92), path)
    releases = [(int(i), int(n)) for i, n in re.findall(r"top_release_ns,(\d+),(\d+)", log)]
    check([i for i, _ in releases] == list(range(0, 104, 2)) and
          all((n >= 40000000) == (i % 4 == 2) for i, n in releases), path)
    check(9.9 < float(re.search(r"fast_source_hz=([\d.]+)", log)[1]) < 10.1, path)
    if not slow:
        check(resume == 0 and paired == 130, path)
        return rows
    check(resume and clouds < 130, path)
    # Recovery is the first post-resume callback after which every cloud is <100 ms old.
    callbacks = sorted((stamp + age, age) for _, _, c, _, stamp, age in rows if c and stamp + age >= resume)
    last_stale = max((n for n, (_, age) in enumerate(callbacks) if age >= 100000000), default=-1)
    recovery = (callbacks[last_stale + 1][0] - resume) / 1e6
    check(callbacks[-1][0] > resume and all(r[5] < 100000000 for r in rows if r[0] >= 92), path)
    print(f"{path.stem} clouds={clouds} metadata={infos} paired={paired} "
          f"cloud_only={clouds-paired} metadata_only={infos-paired} recovery_ms={recovery:.1f} "
          f"oldest_ms={max(r[5] for r in rows)/1e6:.1f}")
    for idx, label in ((2, "missing_cloud"), (3, "missing_metadata")):
        print(label, " ".join(f"{r[0]}/{r[1]}" for r in rows if not r[idx]) or "-")
    return rows


if __name__ == "__main__":
    root = Path(sys.argv[1])
    for mode in ("stock", "cyclone", "zenoh"):
        for trial in (1, 2):
            for condition in ("extended", "overflow"):
                read(root / f"{trial}-{mode}-{condition}.log", condition == "overflow")
    audit = root / "audit-stock.log"
    rows = read(audit, True)
    log = audit.read_text()
    check("sync_view_audit loans=260 released=260" in log, audit)
    drops = re.findall(r"queue_evict id=(\d+) stamp=(\d+) depth=(\d+)", log)
    check(all(int(d) == 32 for _, _, d in drops), audit)
    check(sorted(int(s) for _, s, _ in drops) == sorted(r[4] for r in rows if not r[2]), audit)
    trace = [list(map(int, line.split())) for line in (root / "audit.trace").read_text().splitlines()]
    check(len(trace) == 130 and sorted(r[0] for r in trace if r[9] == 0) ==
          sorted(int(i) for i, _, _ in drops), audit)
    check(all(0 < r[2] <= r[8] <= r[9] for r in trace if r[9]), audit)
    print(f"stock output loans admitted={len(trace)} taken_and_released={sum(bool(r[9]) for r in trace)} "
          f"evicted_at_depth_32={len(drops)}")
