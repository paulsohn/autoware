"""Compare two stock observers and the requesting reader on the same laptop clock."""
import csv
import json
import sys
from pathlib import Path


def events(path):
    return [json.loads(line) for line in path.read_text().splitlines() if line.startswith("{")]


def ms(ns):
    return round(ns / 1e6, 2)


def summarize(path):
    case = path / "0-sustained-control"
    assert "local=600 peer=600" in (case/"verify.log").read_text()
    assert "steady_tracks=596/596" in (case/"graph.log").read_text()
    active = events(case/"churn-network.log")
    observers = [events(case/f"observer-{side}.log") for side in ("host","peer")]
    for topic in ("active1", "active2"):
        starts = [e["ns"] for e in active if e.get("created")==topic]
        stops = [e["ns"] for e in active if e.get("removed")==topic]
        assert starts and len(starts)==len(stops)
        seen = set()
        for generation, (start, removed, end) in enumerate(
                zip(starts, stops, starts[1:]+[float("inf")]),1):
            assert any(e.get("first")==topic and start<=e["ns"]<removed for e in active)
            retired = [e["ns"] for e in active if e.get("retired")==topic and removed<=e["ns"]<end]
            host = [e for e in observers[0] if e.get("topic")==topic and "count" in e]
            current = {gid:owner for e in host if start<=e["ns"]<removed
                       for gid,owner in e["endpoints"].items() if gid not in seen}
            if len(current)!=1:
                raise ValueError(f"{path}: expected one new destination for {topic}, saw {current}")
            gid, owner = next(iter(current.items()))
            seen.add(gid)
            exits = [e for e in observers[0] if e.get("exited")==gid]
            assert len(exits)<=1
            row = [path.name, topic, generation, gid, int(owner.rsplit("_",1)[1]),
                   ms(exits[0]["ns"]-removed) if exits else "unobserved",
                   ms(retired[0]-removed) if retired else "miss"]
            for samples in observers:
                samples = [e for e in samples if e.get("topic")==topic and "count" in e
                           and start<=e["ns"]<end]
                before = [e for e in samples if e["ns"]<removed]
                if not any(gid in e["endpoints"] for e in before):
                    raise ValueError(f"{path}: observer never saw {gid} before unsubscribe")
                assert samples[-1]["ns"]>removed+7_000_000_000
                after = [before[-1]] + [e for e in samples if e["ns"]>=removed]
                max_gap=max(b["ns"]-a["ns"] for a,b in zip(after,after[1:]))
                for field in ("endpoints","count"):
                    present = [i for i,e in enumerate(after)
                               if (gid in e[field] if field=="endpoints" else e[field]>0)]
                    if present:
                        last = present[-1]
                        row.append(ms(after[last+1]["ns"]-removed) if last+1<len(after) else "still visible")
                    else:
                        row.append("gone before unsubscribe")
                gate = next(e for e in after if e["ns"]>=removed+7_000_000_000)
                row.extend([ms(max_gap), gate["count"], "|".join(gate["endpoints"])])
            yield row


out=csv.writer(sys.stdout,lineterminator="\n")
out.writerow(("run","topic","generation","gid","destination_pid","exit_ms","requester_ms",
              "host_gid_gone_ms","host_count_zero_ms","host_max_poll_gap_ms","host_count_at_7s","host_gids_at_7s",
              "peer_gid_gone_ms","peer_count_zero_ms","peer_max_poll_gap_ms","peer_count_at_7s","peer_gids_at_7s"))
for path in map(Path,sys.argv[1:]):
    out.writerows(summarize(path))
