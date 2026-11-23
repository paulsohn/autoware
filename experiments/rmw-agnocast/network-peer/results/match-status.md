# A count query consumed the pending zero-match event

The old demand-driven gateway subscribed to native input when the first DDS
reader matched. On every native input it also asked its DDS publisher for
the current reader count. If the reader disappeared between executor wakeups,
the count query could return zero and consume the pending notification.
Publication stopped, but the callback that releases the native reader then
had no pending event to take. This explains how an earlier run could take
60 clouds and forward only 21.

The installed Lyrical rmw_cyclonedds_cpp 4.1.5
`rmw_publisher_count_matched_subscriptions` calls
`dds_get_publication_matched_status`. The installed Cyclone DDS 11.0.1 header
documents that this getter resets the publication-matched status. To inspect
the exact binary and header from the tested root:

```bash
prefix="$(cat build/agnocast-lyrical/universe-root)/opt/ros/lyrical"
objdump -d --disassemble=rmw_publisher_count_matched_subscriptions \
  "$prefix/lib/librmw_cyclonedds_cpp.so" | rg 'dds_.*@plt'
rg -B22 '^dds_get_publication_matched_status' \
  "$prefix/include/CycloneDDS/dds/ddsc/dds_public_status.h"
```

## Controlled ordering

Identical Int32 endpoints ran under stock Cyclone and the DDS-facing path
of rmw_agnocast. Each row below held for both implementations. The subscriber
ran inside the stock container. We paused executor dispatch across the
one-second participant lease and let the DDS receive threads continue. Each
publisher sent a numbered sample before and after the outage.

| Peer network operation | First operation after expiry | Count after dispatch | Match callbacks over the trial | Samples delivered |
|---|---|---:|---|---|
| Same eth0 down/up | Read count | 0 | 1, 1 | 1 and 2 |
| Same eth0 down/up | Dispatch events | 0 | 1, 0, 1 | 1 and 2 |
| Docker disconnect/connect at same IP | Read count | 0 | 1 | 1 |
| Docker disconnect/connect at same IP | Dispatch events | 0 | 1, 0 | 1 |

Only the order changed the zero-match callback. Only the network operation
changed rediscovery and delivery in this matrix. The small probe checked
rematching approximately three seconds after dispatch resumed. The graph
test observed the replacement case for 12 seconds after reattachment.
Neither experiment inspected the socket binding after Docker replacement,
so its lower-level cause remains open. A physical network was not exercised.

## Gateway change

Demand mode already attaches the local subscription on a positive match and
releases it at zero. It now uses that event as its only match-status reader.
The per-sample count check remains on the always-attached path. A cloud
already in flight while an unmatch is pending can still be published to an
empty DDS writer. The zero-match callback bounds the wasted work. No polling
timer, kernel change or DDS dependency patch was needed.

The rebuilt three-LiDAR graph verified 540/540 local output groups and
released all 1,350 synchronizer loans across nine streams. With eth0
preserved, both live recorders resumed automatically, starting at source
groups 33 and 34 (197 and 260 ms after the restoration command began).
Both recorded through group 59 without a further gap. The native take
counter stayed at 22 and 23 respectively between unmatch and rematch.
With Docker interface replacement, all three repeats detached and finished
with the same native take count logged at unmatch. The recorder remained
unmatched for the 12-second observation. A fresh recorder after replacement
resumed at group 39. Clean recorder restart, gateway restart, and process
suspension/resumption also delivered continuously from recovery to the end.

One repeat completed bag verification but its new log checker omitted the
colon following the gateway logger name. Correcting the checker validated
the saved counts of 21 at unmatch and exit. A fresh full lease trial also
passed the corrected check. Counts cover output produced up to DDS lease
expiry, including writes during the outage that never reached the bag.

[Probe evidence](match-order.csv) · [Graph counters and bag sequences](match-graph.csv) · [Reproduction commands](../README.md#diagnose-a-missed-matched-callback)
