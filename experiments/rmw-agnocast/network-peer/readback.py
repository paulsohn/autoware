"""Check the peer's SQLite rows against the local verifier's cloud for each source stamp."""
import csv
import re
import sqlite3
import sys
from pathlib import Path
from rclpy.serialization import deserialize_message
from sensor_msgs.msg import PointCloud2

root = Path(sys.argv[1])
stamps = {int(t): i for i, t in enumerate((root/"stamps").read_text().split())}
graph = (root/"graph.log").read_text()
assert len(stamps) == 60 and "verified route pilot groups=60" in graph
assert "sync_view_audit loans=150 released=150" in graph
local = {int(i): (kind, int(ns)) for i,kind,ns in re.findall(r"sample,(\d+),(full|partial),(\d+)", graph)}
assert len(local) == 60
received = {}
if sys.argv[2:] != ["idle"]:
    db, = (root/"bag").glob("*.db3")
    with sqlite3.connect(f"file:{db}?mode=ro", uri=True) as conn:
        for raw, bag_ns in conn.execute("select data,timestamp from messages order by id"):
            msg = deserialize_message(raw, PointCloud2)
            stamp = msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec
            seq = stamps[stamp]
            assert seq not in received and msg == deserialize_message((root/f"local-{seq}").read_bytes(), PointCloud2)
            received[seq] = bag_ns
assert list(received) == sorted(received)
trace = root/"forward.csv"
forwarded = {stamps[int(row[0])] for row in csv.reader(trace.open())} if trace.exists() else set()
assert received.keys() <= forwarded
with (root/"audit.csv").open("w") as f:
    w = csv.writer(f,lineterminator="\n")
    w.writerow(("sequence","source_ns","kind","local_latency_ns","forwarded","bag_ns"))
    for stamp,i in stamps.items():
        w.writerow((i,stamp,*local[i],int(i in forwarded),received.get(i,"")))
print(f"local=60/60 loans=150/150 forwarded={len(forwarded)} recorded={list(received)}")
