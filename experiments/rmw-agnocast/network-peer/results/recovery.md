# Remote recorder and gateway recovery

A stock Lyrical Cyclone recorder received a best-effort PointCloud2 export
from the laptop's three-LiDAR graph. Each stream produced 60 groups at 10 Hz.
Two trials per condition used a one-second DDS lease. Interruption began
after 12 local groups and restoration after 32. A separate repeat exposed
a missing gateway match notification.

| Condition | Groups recorded, per trial | First resumed group (zero-based) | Restoration start to bag receipt |
|---|---|---|---|
| Docker network reattached, same recorder | 12, 12 | None | No DDS rematch within 12 s after reattachment |
| Recorder restarted with intact network | 36, 37 | 37, 35 | 589, 400 ms |
| Explicit gateway restarted, same recorder | 31, 32 | 41, 40 | 1001, 862 ms |
| Recorder suspended past expiry, same interface, then resumed | 39, 38 | 35, 34 | 399, 258 ms |
| Docker network reattached, fresh recorder | 33, 33 | 39, 39 | 794, 791 ms |

Each resumed stream continued through group 59 with every payload equal to
the local cloud, in source order, without duplicates. The timing starts
before the recovery command, including launch and discovery where applicable.
For suspension the first resumed group refers to data produced after the
restore marker, excluding buffered older samples. The graph ran six seconds;
the 12-second expiry observation included idle time with both DDS endpoints
still running.

In the two reattachment-only runs the gateway took and forwarded 21 groups,
of which the recorder captured the first 12. A third attempted repeat failed
its prerequisite: the gateway logged its initial match and no zero-match
callback. It took all 60 local clouds but forwarded only 21. The recorder
captured 12. The runner restored the network during cleanup; this repeat
does not measure rematching. The counts identify a missed detach; the layer
responsible for the missing event remains undetermined. Across these eleven
audited streams the local sink verified 660/660 groups and the synchronizer
released 1650/1650 loans.

For an operator, check that bag rows advance after restoring the connection.
If they stall, closing the old recorder cleanly and starting a fresh bag
segment recovered subsequent samples twice while Autoware and the gateway
kept running. Samples missed during the disruption remain missing. Suspension
with the same interface recovered in both trials, so lease expiry alone is
insufficient to explain the Docker result. This laptop test replaces the
peer's virtual interface on reattachment. An interface-preserving network
outage, a stock Cyclone publisher control, and a physical peer remain open.

[Per-group source, local, forwarding and bag evidence](recovery-samples.csv) ·
[Lifecycle times and match counters](recovery-events.json) ·
[Commands and setup](../README.md#exercise-discovery-expiry-and-process-restarts)
