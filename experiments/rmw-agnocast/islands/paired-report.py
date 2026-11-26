"""Summarize control.py timing files as CSV; supply the paired.sh directory prefix."""
import csv
import json
import sys
from pathlib import Path

prefixes = [Path(arg) for arg in sys.argv[1:]]
if not prefixes:
    sys.exit("supply at least one paired.sh output prefix")
w = csv.writer(sys.stdout, lineterminator="\n")
w.writerow(("series", "network", "payload", "pair", "route", "input_reliability", "sequence_count", "cold_first_ms",
            "median_us", "p95_us", "rejoin_first_ms", "idle_processes", "idle_cpu_percent",
            "idle_summed_rss_mib", "idle_seconds", "idle_eth0_tx_bytes", "idle_eth0_rx_bytes"))
for prefix in prefixes:
    for path in sorted(prefix.parent.glob(prefix.name + "-*-*-*/*-timing.json")):
        cloud, pair, dynamic = path.parent.name.rsplit("-", 3)[1:]
        r = json.loads(path.read_text())
        idle = r["idle"]
        count = len(r["rx_ns"])
        assert len(r["tx_ns"]) == count == 20
        qos = r.get("input_reliability", "reliable" if dynamic == "1" else "best_effort")
        w.writerow((prefix.name, path.name.split("-")[0], "640KiB" if cloud == "1" else "Int32", pair,
                    "discovered" if dynamic == "1" else "configured", qos, count,
                    round(r["start_to_first_ms"], 2), round(r["median_us"], 2), round(r["p95_us"], 2),
                    round(r["rejoin"]["start_to_first_ms"], 2) if "rejoin" in r else "",
                    idle["workers"], round(idle["cpu_percent"], 2),
                    round(idle["summed_rss_bytes"]/1048576, 1), round(idle["seconds"], 3),
                    *idle["eth0_delta"]))
