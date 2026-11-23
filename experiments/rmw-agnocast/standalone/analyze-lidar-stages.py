"""Validate nominal three-LiDAR logs and export per-group end-to-end and stage times."""
import csv
import re
import sys
from pathlib import Path

logs, output = map(Path, sys.argv[1:])
with (output / "lidar-o3-ages.csv").open("x") as ages, (output / "lidar-o3-stages.csv").open("x") as stages:
    age_rows = csv.writer(ages, lineterminator="\n")
    stage_rows = csv.writer(stages, lineterminator="\n")
    age_rows.writerow(("arm", "round", "traced", "seq", "kind", "age_ns", "top_publish_start_age_ns"))
    columns = ("group_stamp_ns", "top_pub_start", "top_pub_return", "crop_in", "crop_out",
               "sync_last", "out_start", "out_return", "sink",
               "left_crop_in", "left_crop_out", "right_crop_in", "right_crop_out",
               "left_sync_in", "right_sync_in", "top_sync_in")
    stage_rows.writerow(("arm", "round", "seq", "last_sensor", *columns))
    for path in sorted(logs.glob("*.log")):
        mode, round_id = path.stem.rsplit("-", 1)
        traced = mode.endswith("-trace")
        arm = mode.removesuffix("-trace")
        log = path.read_text()
        if "verified 24 synchronized clouds and metadata, 12 complete and 12 timed out" not in log:
            raise ValueError(f"incomplete verification in {path}")
        samples = re.findall(r'sample,(\d+),(full|partial),(\d+)', log)
        assert [int(i) for i, _, _ in samples] == list(range(24))
        top_age = {int(i): int(v) for i, v in re.findall(r'top_release_ns,(\d+),(\d+)', log)}
        assert sorted(top_age) == list(range(0, 24, 2))
        for i, kind, age in samples:
            age_rows.writerow((arm, round_id, int(traced), i, kind, age, top_age.get(int(i), '')))
        events = {}
        for name, stamp, when in re.findall(r'lidar_stage,(\w+),(\d+),(\d+)', log):
            by_stamp = events.setdefault(name, {})
            assert int(stamp) not in by_stamp, (path, name, stamp)
            by_stamp[int(stamp)] = int(when)
        if not traced:
            assert not events
            continue
        stamps = sorted(events["sink"])
        assert len(stamps) == 24
        for name in ("crop_in", "crop_out", "sync_in"):
            assert len(events[name]) == 60
        assert len(events["top_return"]) == 12
        assert set(events["out_start"]) == set(events["out_return"]) == set(stamps)
        for seq, group in enumerate(stamps):
            sensors = [group + i * 1000000 for i in range(2 if seq % 2 else 3)]
            for sensor in sensors:
                assert events["crop_in"][sensor] < events["crop_out"][sensor]
            if seq % 2:
                continue
            top = sensors[2]
            last = max(sensors, key=events["sync_in"].__getitem__)
            times = (group, group + top_age[seq], events["top_return"][group],
                     events["crop_in"][top], events["crop_out"][top], events["sync_in"][last],
                     events["out_start"][group], events["out_return"][group], events["sink"][group])
            assert times[0] < times[1] < times[2] and times[1] < times[3] < times[4]
            assert times[5] < times[6] < times[7] and times[6] < times[8]
            assert 0 <= int(samples[seq][2]) - (times[8] - group) < 10000000
            earlier = [events[name][sensor] for sensor in sensors[:2] for name in ("crop_in", "crop_out")]
            callbacks = [events["sync_in"][sensor] for sensor in sensors]
            stage_rows.writerow((arm, round_id, seq, sensors.index(last), *times, *earlier, *callbacks))
