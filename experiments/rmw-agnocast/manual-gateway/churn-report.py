"""Summarize local cloud latency and gateway resources per observed native graph phase."""
import csv
import json
import math
import os
import statistics
import sys
from pathlib import Path


def events(path):
    return [json.loads(line) for line in path.read_text().splitlines() if line.startswith("{")]


def rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def p95(values):
    return sorted(values)[math.ceil(.95 * len(values)) - 1]


out = csv.writer(sys.stdout, lineterminator="\n")
out.writerow(("trial", "phase", "aux_native_publishers", "churn_batches", "local_samples",
              "full_cloud_samples", "full_cloud_p50_ms", "full_cloud_p95_ms", "tracked_full_p95_ms",
              "gateway_cpu_pct", "native_discovery_cpu_pct", "network_discovery_cpu_pct", "gateway_rss_peak_mib", "peer_samples", "aux_first_ms",
              "aux_teardown_ms", "aux_samples_before_detach"))
for trial in map(Path, sys.argv[1:]):
    case = trial / "0-sustained-control"
    samples = rows(case / "pipeline/samples.csv")
    assert [int(r["seq"]) for r in samples] == list(range(600))
    remote = case / "peer-sequences"
    recorded = set(map(int, remote.read_text().split())) if remote.exists() else set()
    assert recorded == (set() if trial.name.endswith("-native") else set(range(600)))
    gateway = rows(case / "gateway-usage.csv") if remote.exists() else []
    native = events(case / "churn-native.log")
    network = events(case / "churn-network.log")
    phases = [e for e in native if "phase" in e]
    assert [e["phase"] for e in phases] == ["stable","offers12","offers48","churn","one","two","detach"]
    first, retired = {}, {}
    for kind, origin, target in (("first", "created", first), ("retired", "removed", retired)):
        for e in network:
            if kind in e:
                name=e[kind]
                start=next(x["ns"] for x in network if x.get(origin)==name)
                target[name]=round((e["ns"]-start)/1e6, 1)
    counts = {e.get("retired", e.get("advertised")): e["count"] for e in network if "count" in e}
    assert counts.keys() == first.keys() and all(v>0 for v in counts.values())
    expected = {"active1","active2"} if trial.name.endswith("-discovered") else set()
    assert retired.keys() == expected
    assert first.keys() == ({"active1","active2"} if trial.name.endswith("-configured-matched") else expected)
    # The auxiliary publishers disappear at t=54. Exclude the remaining cooldown.
    end = int((case/"churn-start").read_text()) + 54_000_000_000
    for phase, stop in zip(phases, [e["ns"] for e in phases[1:]]+[end]):
        start, name = phase["ns"], phase["phase"]
        group = [r for r in samples if start <= int(r["stamp_ns"]) < stop]
        full = [r for r in group if int(r["seq"]) >= 4 and int(r["seq"]) % 2 == 0]
        lat = [int(r["cloud_ns"])/1e6 for r in full]
        track = [int(r["tracked_ns"])/1e6 for r in full]
        usage = [r for r in gateway if start <= int(r["ns"]) < stop]
        cpu = rss = ""
        scanners = ["",""]
        if gateway:
            a, b = usage[0], usage[-1]
            cpu = round(1e5*(int(b["cpu_usec"])-int(a["cpu_usec"]))/(int(b["ns"])-int(a["ns"])),2)
            if "native_scan_ticks" in a:
                scanners = [round(1e11*(int(b[key])-int(a[key])) / os.sysconf("SC_CLK_TCK")
                                  /(int(b["ns"])-int(a["ns"])),2)
                            for key in ("native_scan_ticks","network_scan_ticks")]
            rss = round(max(int(r["rss_kib"]) for r in usage)/1024,1)
        batches = sum("churn_created" in e for e in native) if name=="churn" and any("churn_created" in e for e in native) else ""
        out.writerow((trial.name,name,phase["publishers"],batches,len(group),len(full),
            round(statistics.median(lat),3),round(p95(lat),3),round(p95(track),3),cpu,*scanners,rss,
            sum(int(r["seq"]) in recorded for r in group) if remote.exists() else "",
            first.get({"one":"active1","two":"active2"}.get(name)),
            max(retired.values()) if name=="detach" and retired else "",
            counts.get({"one":"active1","two":"active2"}.get(name))))
