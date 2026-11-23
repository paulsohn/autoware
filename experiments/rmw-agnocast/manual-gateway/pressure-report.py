"""Summarize completed pressure cases from a check.py output directory."""
import csv
import re
import sys
from pathlib import Path

out = csv.writer(sys.stdout, lineterminator="\n")
out.writerow(("batch", "case", "local", "recorded", "source_forwarded", "overwritten", "fault_s",
              "missing_before", "missing_during", "missing_after", "peak_rss_mib", "fault_peak_rss_mib", "last_rss_mib", "min_host_available_mib"))
cases = (case for root in sys.argv[1:] for case in sorted(
    Path(root).iterdir(), key=lambda p: int(p.name.split("-")[0])))
for case in cases:
    if not (case/"rss.csv").exists():
        continue
    result = re.search(r"local=(\d+)(?:/\d+)? peer=(\d+)", (case/"verify.log").read_text())
    gateway = (case/"gateway.log").read_text()
    forwarded = re.search(r"stats role=source samples=(\d+)", gateway)[1]
    drops = re.search(r"export overwritten=(\d+)", gateway)[1]
    with (case/"rss.csv").open() as stream:
        rss = [(int(r["time_ns"]), int(r["combined_rss_kib"])/1024) for r in csv.DictReader(stream)]
    start, end = (int((case/f"fault-{side}").read_text()) for side in ("start","end"))
    memory = [int(n) for log in case.glob("*.log") for n in
              re.findall(r"Minimum host MemAvailable: (\d+)", log.read_text())]
    if (case/"pipeline").is_dir():
        with (case/"pipeline"/"samples.csv").open() as stream:
            stamps = [int(r["stamp_ns"]) for r in csv.DictReader(stream)]
        seen = set(map(int,(case/"peer-sequences").read_text().split()))
    else:
        stamps = list(map(int,(case/"stamps").read_text().split()))
        with (case/"delivery.csv").open() as stream:
            seen = {int(r["sequence"]) for r in csv.DictReader(stream) if r["peer_recorded"]=="1"}
    missing = [stamp for seq,stamp in enumerate(stamps) if seq not in seen]
    assert len(stamps) == int(result[1]) and len(seen) == int(result[2])
    out.writerow((case.parent.name, case.name, *result.groups(), forwarded, drops, round((end-start)/1e9,2),
                  sum(t < start for t in missing),sum(start <= t <= end for t in missing),sum(end < t for t in missing),
                  round(max(m for _,m in rss),1), round(max(m for t,m in rss if start<=t<=end),1),
                  round(rss[-1][1],1), min(memory)))
