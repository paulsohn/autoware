# Runtime type fallback on the laptop

Times are microseconds. Each row pools the last 40 of 48 verified samples per run. CPU is median whole relay process time in seconds. Cloud payloads alternate between empty, one point and the indicated maximum.

| Direction | Message | Adapter | Runs | End-to-end median / p95 | Callback median / p95 | CPU s |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| a2r | PointCloud2 (16 KiB) | serialized | 2 | 208.5 / 269.0 | 39.6 / 65.6 | 0.088 |
| a2r | PointCloud2 (16 KiB) | typed | 2 | 179.9 / 229.8 | 42.4 / 63.3 | 0.087 |
| a2r | Image (reliable) | serialized | 1 | 186.1 / 266.3 | 35.3 / 79.5 | 0.086 |
| a2r | PointCloud2 (512 KiB) | serialized | 2 | 780.1 / 1437.0 | 516.1 / 795.8 | 0.109 |
| a2r | PointCloud2 (512 KiB) | typed | 2 | 656.5 / 1301.8 | 481.5 / 767.0 | 0.104 |
| a2r | Path | serialized | 2 | 1114.0 / 1445.9 | 95.6 / 144.1 | 0.103 |
| a2r | Path | typed | 2 | 1416.8 / 1779.8 | 351.3 / 485.0 | 0.097 |
| a2r | PoseArray | serialized | 1 | 288.8 / 390.2 | 81.5 / 114.3 | 0.104 |
| a2r | Image (sensor QoS) | serialized | 1 | 200.7 / 229.8 | 33.6 / 42.7 | 0.089 |
| r2a | PointCloud2 (16 KiB) | serialized | 2 | 230.6 / 501.4 | 52.8 / 83.0 | 0.098 |
| r2a | PointCloud2 (16 KiB) | typed | 2 | 184.3 / 416.8 | 11.7 / 33.0 | 0.091 |
| r2a | Image (reliable) | serialized | 1 | 180.2 / 357.5 | 8.8 / 24.0 | 0.092 |
| r2a | PointCloud2 (512 KiB) | serialized | 2 | 948.7 / 1514.1 | 77.3 / 445.0 | 0.108 |
| r2a | PointCloud2 (512 KiB) | typed | 2 | 902.5 / 1414.6 | 31.5 / 375.9 | 0.103 |
| r2a | Path | serialized | 2 | 1406.6 / 1762.3 | 177.4 / 249.7 | 0.103 |
| r2a | Path | typed | 2 | 1415.1 / 2042.2 | 407.5 / 620.4 | 0.093 |
| r2a | PoseArray | serialized | 1 | 406.5 / 601.0 | 56.4 / 79.6 | 0.094 |
| r2a | Image (sensor QoS) | serialized | 1 | 319.6 / 421.9 | 17.9 / 27.3 | 0.086 |

a2r exports native samples to Cyclone DDS. r2a imports Cyclone DDS samples into native storage. The reader checks payload equality, order and count. An opposite-transport subscriber on the output name checks for unintended publication. [Run and negative-control outcomes](runs.txt) · [Per-sample timings](samples.csv)
