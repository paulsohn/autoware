"""Collect the four-stack replay logs. Preserve failure evidence alongside successful samples."""
import csv
import math
import re
import sys
from collections import defaultdict
from pathlib import Path
from statistics import median

root, dest = map(Path, sys.argv[1:])
dest.mkdir(exist_ok=True, parents=True)
frames, runs, delivery, failures = [], [], [], []
values = defaultdict(list)


def rows(path):
    with path.open() as stream:
        return list(csv.reader(stream))


def sample(case, arm, run, seq, kind, ns, warmup):
    frames.append((case, arm, run, seq, kind, ns, int(seq >= warmup)))
    if seq >= warmup:
        values[case, arm, kind].append(ns / 1e6)


def write(name, header, data):
    with (dest / name).open("w") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(header.split())
        writer.writerows(data)


for case in ("path", "path-repeat", "crop10", "crop50", "branched10", "branched50"):
    for path in sorted((root / case).glob("*.csv")):
        if path.stem.endswith(("-resources", "-tap")):
            continue
        arm, run = path.stem.rsplit("-", 1)
        result = case
        if case.startswith("path"):
            arm, rate = arm.rsplit("-", 1)
            result = case + rate
        metrics = rows(path.with_name(path.stem + "-resources.csv"))[1]
        data = rows(path)
        assert len(data) == int(metrics[0]) == 60
        for seq, (index, ns) in enumerate(data):
            assert seq == int(index) and int(ns) > 0
            sample(result, arm, run, seq, "payload", int(ns), 10)
        if case.startswith("branched"):
            tap = rows(path.with_name(path.stem + "-tap.csv"))
            assert [int(row[0]) for row in tap] == list(range(60))
            for seq, ns in tap:
                sample(result, arm, run, int(seq), "tap", int(ns), 10)
        log = path.with_suffix(".log").read_text()
        hz = (float(path.with_suffix(".rate").read_text()) if case.startswith("path")
              else float(re.search(r"source_rate_hz=([\d.]+)", log)[1]))
        expected = 50 if "50" in result else 10
        assert abs(hz / expected - 1) < .05
        runs.append((result, arm, run, 60, 60, "", "", sum(map(float, metrics[1:3])),
                     metrics[3], re.search(r"MemAvailable: (\d+)", log)[1], round(hz, 3)))
for case in ("nominal", "double", "pressure", "regions", "regions-fast", "jitter", "extended", "overflow",
             "slow", "busy", "depth-5", "depth-8", "depth-16", "fresh-keep", "fresh-reject"):
    for path in sorted((root / case).glob("*.log")):
        arm, run = path.stem.rsplit("-", 1)
        log = path.read_text()
        match = re.search(r"verified (\d+) synchronized clouds and metadata", log)
        extended = re.search(r"extended expected=(\d+) clouds=(\d+) metadata=(\d+) paired=(\d+)", log)
        if not (match or extended):
            diagnostic = [line for line in log.splitlines()
                          if "what():" in line or "runtime_error" in line or "process has died" in line]
            failures.append((case, arm, run, diagnostic[0] if diagnostic else "missing completion marker"))
            runs.append((case, arm, run, "failed", "", "", "", "", "", "", ""))
            continue
        expected, clouds = (int(match[1]), int(match[1])) if match else map(int, extended.groups()[:2])
        cpu = re.search(r"graph_cpu ([\d.]+) ([\d.]+)", log)
        branches = [0, 0]
        for seq, kind, age in re.findall(r"\bsample,(\d+),(\w+),(\d+)", log):
            sample(case, arm, run, int(seq), kind, int(age), 4)
        all_regions = re.findall(r"\bregions,(\d+),(\w+),(\d+)", log)
        for seq, kind, age in all_regions:
            sample(case, arm, run, int(seq), "both-" + kind, int(age), 4)
        for seq, kind, b, stamp, age in re.findall(r"\bbranch,(\d+),(\w+),([01]),(\d+),(-?\d+)", log):
            if int(age) >= 0:
                branches[int(b)] += 1
                sample(case, arm, run, int(seq), ("near-" if b == "0" else "far-") + kind, int(age), 4)
        if all_regions and not sum(branches):
            assert len(all_regions) == expected
            branches = [expected, expected]
        if extended:
            delivery.append((case, arm, run, *extended.groups(), "", "", ""))
        elif sum(branches):
            rejects = len(re.findall(r"near_rejected_stamp=", log))
            delivery.append((case, arm, run, expected, clouds, clouds, clouds, *branches, rejects))
        runs.append((case, arm, run, expected, clouds, *branches, sum(map(float, cpu.groups())) if cpu else "",
                     "", re.search(r"MemAvailable: (\d+)", log)[1], re.search(r"fast_source_hz=([\d.]+)", log)[1]))
for path in sorted((root / "sustained").glob("*.log")):
    arm, run = path.stem.rsplit("-", 1)
    log = path.read_text()
    bag = re.search(r"bag=(\d+)/600", log)
    samples = path.with_suffix("") / "samples.csv"
    cpu = re.search(r"cpu_driver_stream_s=([\d.]+) cpu_children_s=([\d.]+)", log)
    data = []
    if samples.exists():
        with samples.open() as stream:
            data = list(csv.DictReader(stream))
    if len(data) == 600:
        for seq, item in enumerate(data):
            assert int(item["seq"]) == seq
            sample("sustained", arm, run, seq, "full" if seq % 2 == 0 else "partial", int(item["tracked_ns"]), 20)
    if len(data) != 600 or not bag or bag[1] != "600":
        diagnostic = [line for line in log.splitlines() if "RCLError:" in line or "RuntimeError:" in line]
        failures.append(("sustained", arm, run, diagnostic[-1] if diagnostic else "incomplete sustained run"))
    memory = re.search(r"MemAvailable: (\d+)", log)
    runs.append(("sustained", arm, run, 600, len(data), "", bag[1] if bag else "",
                 sum(map(float, cpu.groups())) if cpu else "", "", memory[1] if memory else "", ""))
write("delivery.csv", "case arm run expected clouds metadata paired near far rejected_at_entry", delivery)
write("failures.csv", "case arm run reason", failures)
write("frames.csv", "case arm run seq stage age_ns timed", frames)
write("launches.csv", "case arm run expected received near far_or_bag cpu_s largest_child_rss_kib host_available_mib source_hz", runs)
write("summary.csv", "case arm stage timed_count median_ms p95_ms",
      [(case, arm, stage, len(v), round(median(v), 6), round(sorted(v)[math.ceil(len(v)*.95)-1], 6))
       for (case, arm, stage), v in sorted(values.items())])
