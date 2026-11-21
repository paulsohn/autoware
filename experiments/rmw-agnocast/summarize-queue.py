"""Check the stock sink's diagnostic handoff trace. Input units are steady-clock ns."""

import argparse
import math
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument("--outputs", type=int, choices=(24, 30), default=24)
parser.add_argument("files", nargs="+")
args = parser.parse_args()
# The first four acquisition groups produce five outputs under jitter.
warmup_outputs = 5 if args.outputs == 30 else 4
for filename in args.files:
    rows = [list(map(int, line.split())) for line in Path(filename).read_text().splitlines()]
    if len(rows) != args.outputs or any(not (0 < r[2] <= r[8] <= r[9]) for r in rows):
        raise ValueError(f"incomplete handoff trace: {filename}")
    changes = [(r[2], 1) for r in rows] + [(r[9], -1) for r in rows]
    active = peak = 0
    for _, delta in sorted(changes):
        active += delta
        peak = max(peak, active)
    lifetimes = sorted((r[9] - r[2]) / 1e6 for r in rows[warmup_outputs:])
    median, p95 = (lifetimes[math.ceil(p * len(lifetimes)) - 1] for p in (0.5, 0.95))
    print(f"{Path(filename).name} completed={len(rows)} peak={peak} "
          f"hold_ms_p50={median:.3f} hold_ms_p95={p95:.3f} max={max(lifetimes):.3f}")
