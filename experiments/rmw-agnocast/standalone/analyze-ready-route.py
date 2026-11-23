"""Validate a six-order Path A/B and a reversed pair of ready-route stage traces."""
import csv
from pathlib import Path
import sys

first, repeat, traces, output = map(Path, sys.argv[1:])


def read(path):
    with path.open() as f:
        return [list(map(int, row)) for row in csv.reader(f)]


with (output / 'ready-route-frames.csv').open('x') as f, (output / 'ready-route-resources.csv').open('x') as g:
    frames = csv.writer(f, lineterminator='\n')
    resources = csv.writer(g, lineterminator='\n')
    frames.writerow(('batch','round','arm','frame','latency_ns'))
    resources.writerow(('batch','round','arm','verified_frames','child_user_s','child_system_s','largest_child_maxrss_kib'))
    for batch, folder in enumerate((first, repeat), 1):
        for trial in range(1, 7):
            for arm in ('native','poll-cie','route-cie'):
                stem = folder / f'{arm}-{trial}'
                times = read(stem.with_suffix('.csv'))
                assert [seq for seq, _ in times] == list(range(60))
                frames.writerows((batch, trial, arm, *row) for row in times)
                with (folder / f'{arm}-{trial}-resources.csv').open() as source:
                    row, = csv.DictReader(source)
                assert row['verified_frames'] == '60'
                assert stem.with_suffix('.log').read_text().count('CIE benchmark announcement KeepLast(16)') == int(arm != 'native')
                resources.writerow((batch, trial, arm, row['verified_frames'], row['child_user_s'],
                                    row['child_system_s'], row['largest_child_maxrss_kib']))

with (output / 'ready-route-stages.csv').open('x') as f:
    writer = csv.writer(f, lineterminator='\n')
    writer.writerow(('round','arm','frame','tx','ppoll_return','inner_epoll','loan','queued',
                     'rmw_return','rcl_return','take_begin','take_end','callback'))
    for trial in (1, 2):
        for arm in ('poll-cie','route-cie'):
            stem = traces / f'{arm}-instrumented-{trial}'
            clocks, waits, stages, loans, times = [read(stem.with_suffix(ext)) for ext in
                                                   ('.clocks','.waits','.stages','.loans','.csv')]
            assert len(clocks) == len(waits) == len(stages) == len(times) == 60
            for index, ((tx, rx, addr), (woke, rmw), row, (seq, age)) in enumerate(
                zip(clocks, waits, stages, times, strict=True)
            ):
                entry, _, queue, rcl, begin, done, mono = row
                assert entry == index + 1 and seq == index and age == rx - tx
                match, = [l for l in loans if l[3] == addr and tx <= l[5] <= rx]
                wake_id, loan_id, _, _, epoll, loan = match
                if arm == 'poll-cie':
                    assert wake_id == loan_id and tx <= epoll <= loan
                else:
                    # No inner epoll wait should emit a wake hook for a ready route.
                    assert epoll == 0
                offset = mono - done
                stamps = [tx, woke + offset, epoll or None, loan, queue + offset,
                          rmw + offset, rcl + offset, begin + offset, mono, rx]
                ordered = [t for t in stamps if t is not None]
                assert ordered == sorted(ordered), (stem, index, ordered)
                writer.writerow((trial, arm, seq, *stamps))
