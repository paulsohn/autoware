"""Join native and ROS timestamps to verified Path frames; keep warmups in the export."""
import csv
from pathlib import Path
import sys


def read(path):
    with path.open() as f:
        return [list(map(int, row)) for row in csv.reader(f)]


def timestamp_rows(stem):
    clocks = read(stem.with_suffix('.clocks'))
    loans = read(stem.with_suffix('.loans'))
    stage_file = stem.with_suffix('.stages')
    stages = read(stage_file) if stage_file.exists() else []
    wait_file = stem.with_suffix('.waits')
    waits = read(wait_file) if wait_file.exists() else []
    assert len(waits) in (0, 60)
    native = stem.name.startswith('native-')
    assert len(clocks) == 60 and len(stages) == (0 if native else 60)
    seen = set()
    for index, (tx, callback, address) in enumerate(clocks):
        # Storage addresses can be reused. Require the same address AND time window.
        row, = [row for row in loans if row[3] == address and tx <= row[5] <= callback]
        wake_id, loan_id, entry, _, wake, acquired = row
        assert wake_id == loan_id and (loan_id, entry) not in seen
        seen.add((loan_id, entry))
        pub = queued = waited = take = taken = None
        if not native:
            seq, pub, queued, waited, take, done, taken = stages[index]
            assert seq == index + 1
            offset = taken - done  # take-exit monotonic minus paired realtime
            pub, queued, waited, take = [t + offset for t in (pub, queued, waited, take)]
        row = [tx, pub, wake, acquired, queued, waited, take, taken, callback]
        ordered = [t for t in row if t is not None]
        assert ordered == sorted(ordered), (stem, index, ordered)
        wake_return = rmw_return = None
        if waits:
            wake_real, rmw_real = waits[index]
            assert rmw_real > 0
            rmw_return = rmw_real + offset
            assert queued <= rmw_return <= waited
            if wake_real:
                wake_return = wake_real + offset
                if stem.name.startswith('poll-cie'):
                    assert tx <= wake_return <= wake
                else:
                    assert queued <= wake_return <= rmw_return
        yield [*row, wake_return, rmw_return]


root, output = map(Path, sys.argv[1:])
with output.open('x') as f:
    out = csv.writer(f, lineterminator='\n')
    out.writerow(('arm','round','traced','seq','latency_ns','tx','pub_header','native_epoll',
                  'native_loan','queued','rcl_wait_return','take_begin','take_end','callback',
                  'worker_wait_primitive_return','rmw_wait_return'))
    for arm in ('native','standalone-cie','poll-cie'):
        for trial in range(1, 5):
            traced = trial in (2, 3)
            stem = root / f"{arm}{'-instrumented' if traced else ''}-{trial}"
            latencies = read(stem.with_suffix('.csv'))
            assert [seq for seq, _ in latencies] == list(range(60))
            stamps = list(timestamp_rows(stem)) if traced else [[None]*11 for _ in latencies]
            for (seq, latency), timestamps in zip(latencies, stamps, strict=True):
                if traced:
                    assert timestamps[8] - timestamps[0] == latency
                out.writerow((arm, trial, int(traced), seq, latency, *timestamps))
