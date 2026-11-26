"""Require the pinned upstream test inventory and preserve every recorded pass."""
import argparse
import csv
from collections import Counter
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("reference", type=Path)
parser.add_argument("candidate", type=Path)
parser.add_argument("--strict", action="store_true", help="also reject every nonpassing enabled case")
args = parser.parse_args()


def inventory(path):
    with path.open() as stream:
        reader = csv.reader(stream)
        if next(reader) != ["suite", "program", "case", "status"]:
            raise ValueError(f"{path}: unexpected columns")
        cases = {}
        for suite, program, case, status in reader:
            key = (suite, program, case)
            if key in cases:
                raise ValueError(f"{path}: duplicate {key}")
            cases[key] = status
        if not cases:
            raise ValueError(f"{path}: empty inventory")
        return cases


reference = inventory(args.reference)
candidate = inventory(args.candidate)
for suite in sorted({key[0] for key in candidate}):
    statuses = Counter(value for key, value in candidate.items() if key[0] == suite)
    print(suite, ", ".join(f"{count} {status}" for status, count in sorted(statuses.items())))
errors = 0
for key in sorted(reference.keys() | candidate.keys()):
    old, new = reference.get(key), candidate.get(key)
    if old is None or new is None or (new != old and new != "passed") or (
        args.strict and new not in ("passed", "disabled")
    ):
        print(*key, old, "->", new)
        errors += 1
print(f"{len(candidate)} cases, {errors} differences requiring review")
raise SystemExit(bool(errors))
