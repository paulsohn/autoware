# Docker network peer on the laptop

| Case | Recorded / 60 | Gateway publications | Bridge TX MiB | Gateway CPU s | Local complete-group p95 ms | Complete-group source-to-bag p95 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| unrouted | 0 | 0 | 1.62 | — | 3.24 | — |
| idle | 0 | 0 | 1.62 | 0.1 | 3.21 | — |
| healthy-1 | 52 | 52 | 35.36 | 0.21 | 3.20 | 4.50 |
| reconnect-1 | 37 | 53 | 25.35 | 0.19 | 2.90 | 5.06 |
| reconnect-2 | 37 | 53 | 25.35 | 0.19 | 2.82 | 4.73 |
| healthy-2 | 53 | 53 | 35.87 | 0.2 | 2.92 | 4.42 |

All cases delivered 60/60 locally and released 150/150 audited synchronizer input loans. TX includes discovery and graph traffic. CPU is user + system through graph exit, at process-tick resolution. P95 is nearest-rank on complete groups from sequence 4 onward. Source and bag timestamps share the laptop clock.

In the two reconnect trials the network operations spanned 1.659 and 1.642 s.
Both bags missed groups 16–31 and resumed at group 32. Its bag timestamp came
51.9 and 58.3 ms after the connect command began. Relative to the command
return these offsets were −3.9 and +10.1 ms; Docker can restore the link
before its CLI returns.

The unbridged recorder subscribed to the graph-visible native topic. The idle gateway advertised an export with no recorder. In the four active runs the peer recorder started after local group 3. Both disconnections removed the actual Docker network attachment and reattached the same IP while the recorder process stayed alive. The gateway continued publishing through the brief outage. A longer outage that expires discovery leases is a separate case.
