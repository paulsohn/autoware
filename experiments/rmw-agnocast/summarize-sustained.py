"""Summarize complete 60 s runs; split the nominal 40 ms collector timeout from complete groups."""
import csv
import math
import re
import statistics
import sys
from pathlib import Path

root = Path(sys.argv[1])
for mode in ("native","stock","cyclone","zenoh"):
    groups={"full":[],"partial":[]}
    cpus,peaks,plateaus,clouds,bags=[],[],[],[],[]
    for path in sorted(root.glob("*/"+mode+"/samples.csv")):
        with path.open() as f:
            rows=list(csv.DictReader(f))
        assert [int(r["seq"]) for r in rows]==list(range(600))
        summary=(path.parent/"summary.txt").read_text()
        assert "steady_tracks=596/596" in summary
        bags.append(int(re.search(r"bag=(\d+)/600",summary)[1]))
        graph=(path.parent/"graph.log").read_text()
        assert "verified sustained clouds=600" in graph and graph.count("process has finished cleanly")==5
        if mode=="native":
            assert "native_input owners=1500 released=1500" in graph
        elif mode=="stock":
            assert "sync_view_audit loans=1500 released=1500" in graph
        else:
            assert "sync_input views=0 copies=1500" in graph
        driver,children,peak=re.search(r"cpu_driver_stream_s=([.\d]+) cpu_children_s=([.\d]+) scope_peak_bytes=(\d+)",summary).groups()
        cpus.append(round(float(driver)+float(children),2))
        peaks.append(round(int(peak)/2**20,1))
        late=[round(int(r["anon_bytes"])/2**20,1) for r in rows[100:] if r["anon_bytes"]]
        plateaus.append((min(late),max(late)))
        for i,r in enumerate(rows[20:],20):
            groups["partial" if i%2 else "full"].append(int(r["tracked_ns"])/1e6)
            if i%2==0:
                clouds.append(int(r["cloud_ns"])/1e6)
    assert len(cpus)==2,mode
    def stats(values):
        return f"{statistics.median(values):.3f}/{sorted(values)[math.ceil(.95*len(values))-1]:.3f}"
    print(mode,"full_track_ms="+stats(groups["full"]),"partial_track_ms="+stats(groups["partial"]),
          "full_cloud_ms="+stats(clouds),f"recorded={bags} cpu_s={cpus} peak_MiB={peaks} sampled_anon_MiB={plateaus}")

for trial in ("1","2"):
    timings = []
    for mode in ("native","stock"):
        with (root/trial/mode/"samples.csv").open() as f:
            full = sorted(int(row["tracked_ns"]) for row in csv.DictReader(f)
                          if int(row["seq"])>=20 and int(row["seq"])%2==0)
        timings.append([statistics.median(full),full[math.ceil(.95*len(full))-1]])
    print(f"paired round={trial} rmw/native full-track median/p95="
          f"{timings[1][0]/timings[0][0]:.3f}/{timings[1][1]/timings[0][1]:.3f}")
