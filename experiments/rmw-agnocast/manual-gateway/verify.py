import csv
from pathlib import Path
import re
import sqlite3
import sys
from rclpy.serialization import deserialize_message
from sensor_msgs.msg import PointCloud2

case = Path(sys.argv[1])
if (case/"pipeline").is_dir():
    # Walk both bags in order, keeping at most two serialized clouds in memory.
    def messages(directory, topic):
        db = next(directory.glob("*.db3"))
        with sqlite3.connect(f"file:{db}?mode=ro", uri=True) as conn:
            for (raw,) in conn.execute("select data from messages join topics on topic_id=topics.id where topics.name=? order by messages.id",(topic,)):
                msg = deserialize_message(raw,PointCloud2)
                yield (msg.header.stamp.sec,msg.header.stamp.nanosec),msg

    remote = messages(case/"bag","/manual/cloud")
    current = next(remote,None)
    recorded = []
    local = 0
    for index,(stamp,msg) in enumerate(messages(case/"pipeline"/"bag","/perception/cloud")):
        local += 1
        if current and current[0]==stamp:
            assert current[1]==msg
            recorded.append(index)
            current = next(remote,None)
    assert current is None and local==600 and recorded[0]==0 and recorded[-1]==599
    (case/"peer-sequences").write_text(" ".join(map(str,recorded)))
    print(f"local={local} peer={len(recorded)} first={recorded[0]} last={recorded[-1]}")
    sys.exit(0)

stamps = {int(s): i for i, s in enumerate((case/"stamps").read_text().split())}
log = (case/"graph.log").read_text()
assert len(stamps) == 60 and "verified route pilot groups=60" in log
seen = []
peer_age = {}
bags = sorted((case/"bag").rglob("*.db3"))
if (case/"bag").exists():
    assert bags
for db in bags:
    with sqlite3.connect(f"file:{db}?mode=ro", uri=True) as conn:
        for data, recorded in conn.execute("""select data,timestamp from messages join topics on topic_id=topics.id
                                      where topics.name='/manual/cloud' order by messages.id"""):
            msg = deserialize_message(data, PointCloud2)
            seq = stamps[msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec]
            assert msg == deserialize_message((case/f"local-{seq}").read_bytes(), PointCloud2)
            seen.append(seq)
            peer_age[seq] = recorded - (msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec)
assert seen == sorted(set(seen))
rows = re.findall(r"\bsample,(\d+),(full|partial),(\d+)", log)
assert len(rows) == 60
with (case/"delivery.csv").open("w") as stream:
    writer = csv.writer(stream)
    writer.writerow(("sequence", "kind", "local_age_ns", "peer_recorded", "peer_age_ns"))
    writer.writerows((seq, kind, age, int(int(seq) in seen), peer_age.get(int(seq), "")) for seq, kind, age in rows)
print(f"local=60/60 peer={len(seen)} sequences={seen}")
