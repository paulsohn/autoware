# Local recorder comparison

Each run had 60 local outputs, a 16-group comparison window, two recorder launches, and one restart. CPU includes startup and teardown. The logger path includes both recorder processes and the relay where present.

| Configuration | Runs | Recorded of window 16 (each run) | Median recorder CPU s | Median logger path CPU s |
| --- | ---: | --- | ---: | ---: |
| unrouted | 1 | 0 | 0.661 | 0.661 |
| direct-running | 3 | 16, 16, 16 | 0.648 | 0.648 |
| running | 3 | 16, 16, 16 | 0.638 | 0.797 |
| direct-stalled | 3 | 5, 5, 5 | 0.630 | 0.630 |
| stalled | 3 | 2, 2, 2 | 0.602 | 0.757 |
| direct-stalled-depth32 | 2 | 5, 5 | 0.628 | 0.628 |
| stalled-depth32 | 2 | 2, 2 | 0.626 | 0.783 |
| direct-stalled-both32 | 2 | 16, 16 | 0.758 | 0.758 |
| stalled-both32 | 2 | 2, 2 | 0.679 | 0.845 |

Direct uses rmw_agnocast on /output. Routed recorders use stock Cyclone on /dds/synchronized. Unrouted uses plain Cyclone on /output without a gateway. depth32 changes only recorder history. both32 also sets the synchronizer queue size and the cloud relay QoS to 32. Raw bag receipt times and per-run comparisons are in [samples.csv](samples.csv) and [runs.txt](runs.txt).
