"""Join each reader's actual deliveries to its paced tracker replay."""
import re
import runpy
import sys
from pathlib import Path

isolation = runpy.run_path(str(Path(__file__).with_name("summarize-isolation.py")))


def main(root):
    tracks = {}
    for line in (root / "tracker.txt").read_text().splitlines():
        key = re.match(r"trace=(\S+)", line)[1]
        assert key not in tracks
        tracks[key] = line
    print("depth mode trial delivered glimpse same_id confirmed within_100ms oldest_ms peak_latest_age_ms "
          "fresh_gap_ms far_full_p50/p95_ms cpu_s peak_MiB min_host_MiB")
    for depth in (5, 8, 16):
        for mode in ("native", "stock", "cyclone", "zenoh"):
            for trial in (1, 2):
                path = root / f"depth-{depth}" / f"{trial}-{mode}-slow-near.log"
                log = path.read_text()
                assert re.findall(r"near_reader_depth=(\d+) near_writer_depth=(\d+)", log) == [(str(depth), "5")]
                assert len(re.findall(r"near_busy_cpu_ns=\d+", log)) == 6
                near, far, (start, end), cpu, memory, available = isolation["read_run"](path, mode, True)
                ages = [age for _, age in near.values() if age >= 0]
                glimpse = ",".join(str(i) for i in (9, 10, 11) if near[i][1] >= 0) or "-"
                gap, peak, _ = isolation["freshness"](near, start, end, 100_000_000)
                full = [age for seq, (stamp, age) in far.items() if not seq % 2 and start <= stamp <= end]
                track = tracks.pop(f"{path.parent.name}/{path.stem}")
                assert f"frames={len(ages)} " in track and "occluded_ids=1/1" in track
                assert "landmark_ids=1 landmark_holes=[] switch_ids=0 duplicates=[]" in track
                identity, confirmed, timely = [int(re.search(k + r"=(\d+)", track)[1])
                                                for k in ("occluded_common", "confirmed", "within_100ms")]
                print(depth, mode, trial, len(ages), glimpse, identity, confirmed, timely,
                      f"{max(ages)/1e6:.1f}", f"{peak:.1f}", f"{gap:.1f}",
                      isolation["percentiles"](full), f"{cpu:.2f}", f"{memory:.1f}", available)
    assert not tracks


if __name__ == "__main__":
    main(Path(sys.argv[1]))
