# Reader overload and three-LiDAR replay at 3f60d0929

## Reproduction

Use the built Release rehearsal, released 2.4.1 kernel module and hook, and O3 Universe overlays described in the parent workload notes. Run from the colcon experiment checkout on the laptop. The source trees and O3 fixture executables under build/ are prerequisites. Choose fresh output directories for each run.

```bash
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
out=build/rehearsal-confirm
probe=experiments/rmw-agnocast/standalone
experiments/buffer/run-lyrical.sh env -i HOME=/tmp PATH=/usr/bin:/bin bash "$probe/cloud-reader-pressure.sh" "$out" "$out/pressure-repeat"
python3 "$probe/analyze-reader-pressure.py" "$out/pressure-repeat" > "$out/pressure-repeat.csv"
bash "$probe/bench-universe.sh" "$out/lidar-repeat" --cie-rehearsal "$out"
python3 "$probe/analyze-lidar-stages.py" "$out/lidar-repeat" "$out/lidar-repeat"
python3 "$probe/summarize-lidar-rehearsal.py" "$out/lidar-repeat" > "$out/lidar-repeat.csv"
```

## What was measured

The native pressure probe runs one publisher and two subscribers in three processes. All use ordinary rclcpp typed PointCloud2 calls. The reliable publisher and healthy reader use KeepLast(8). Each cloud carries 16,384 four-byte intensity values (64 KiB). The consumer checks the sequence and payload bytes. After 24 control frames the slow application stops taking for 272 frames while its RMW thread continues receiving. It drains before the writer sends a recovery frame. All three processes stay alive through two cycles at each rate. No per-message acknowledgement throttles the publisher. Healthy-reader latency measures from stamping the completed payload through return from take, including its 250 µs polling sleep. Publisher time brackets the publish call. Control and stall quantiles have different sample counts. The one-frame recovery phase verifies delivery.

Pool bytes are allocated tmpfs blocks from stat on each process's Agnocast pool. The measurement excludes sparse address reservations and private process memory. It samples after the control stream, at saturation, and after the slow application drains. The supervisor checks disappearance of all three pool names after process exit. The native unlink daemon polls the kernel once a second.

The Universe fixture consists of three crop components and the synchronizer with 32,768 input points per sensor frame, 20/20/10 Hz, and a 40 ms partial-group timeout. It checks output clouds and metadata. Per-round CSV counts include warmup. Timing excludes the first four groups of each launch and uses nearest-rank p95. The pooled CPU column is the median user-plus-system seconds over the whole launch. Native uses bundled CIE, standalone uses standalone AWF CIE on the synchronizer, and Cyclone and Zenoh use the existing ROS executor controls. The standalone launch checks the actual RMW, rclcpp and Agnocast overlay in a Cyclone-free ROS distribution. Each batch of 20 launches runs sequentially in alternating forward and reverse order. This is one laptop and five launches per arm.

## Universe results

| Middleware and executor | Verified full / partial | Full median / p95 (ms) | Partial median / p95 (ms) | Median launch CPU (s) |
|---|---:|---:|---:|---:|
| Native Agnocast, bundled CIE | 60 / 60 | 2.263 / 3.398 | 41.729 / 42.927 | 1.942 |
| Standalone RMW, standalone CIE | 60 / 60 | 2.242 / 3.163 | 41.534 / 42.150 | 3.018 |
| Cyclone DDS, ROS executor | 60 / 60 | 2.727 / 3.484 | 42.019 / 42.552 | 1.924 |
| Zenoh, ROS executor | 60 / 60 | 2.404 / 3.504 | 41.864 / 42.284 | 2.838 |

The standalone/native pooled ratios were 0.99× for median and 0.93× for p95. Each standalone launch confirmed 60 retained synchronizer inputs and zero copies at that boundary. One paired launch reached 1.23× p95 (3.742 vs 3.039 ms). The other four pairs ranged from 0.56× to 0.88×. Each launch contributes ten post-warmup full groups, so its p95 is its maximum. Standalone consumed 1.55× the native whole-launch CPU. These checks support the latency milestone on this graph. They leave steady-state CPU attribution open. [All group ages](groups.csv) and [launch summaries](lidar.csv).

## Overload results

All 2,376 healthy-reader clouds arrived in order with their payload validated. All eight recovery frames reached both readers. Each stalled KeepLast(8) reader took the last eight clouds from its 272-frame burst. Replacing unread KeepLast history emitted zero MESSAGE_LOST events. Each stalled KeepAll reader retained the first 256 in order and reported the other 16 as lost. The earlier four-condition replay independently observed the same sequence counts and loss totals.

| Rate | Slow reader | Healthy median / p95, both draining (µs) | Healthy median / p95, slow stalled (µs) | Publish median / p95, slow stalled (µs) | Longest stalled-phase publish (µs) |
|---:|---|---:|---:|---:|---:|
| 10 Hz | KeepLast(8) | 226.2 / 349.2 | 235.1 / 352.2 | 20.3 / 39.0 | 180.6 |
| 10 Hz | KeepAll | 254.6 / 349.1 | 276.7 / 477.2 | 64.7 / 154.9 | 759.7 |
| 50 Hz | KeepLast(8) | 203.6 / 326.3 | 209.4 / 341.6 | 14.4 / 30.8 | 64.2 |
| 50 Hz | KeepAll | 185.0 / 342.4 | 247.4 / 374.7 | 63.9 / 117.5 | 183.8 |

The publisher's KeepAll-run pool grew from 0.77 MiB to 16.74 MiB on the first fill. Draining the queue allowed reuse. The second fill completed with exactly the same allocated block count at both rates. Those tmpfs blocks remained allocated through the drain. The two reader pools stayed at 16 KiB apiece. KeepLast used at most 0.97 MiB of publisher pool blocks. In every run all three pool names had disappeared after nine 100 ms polling intervals. The test's immediate-after-exit assertion in the initial pilot was corrected to allow asynchronous unlink.

The 10 Hz KeepAll first-fill p95 was 564.5 µs at the healthy reader and 185.5 µs in publish. The second fill produced 368.1 and 67.0 µs respectively. This establishes a first-fill cost under these conditions without isolating allocator work from scheduling. No writer waited for the slow application to drain. The longest publish call was below 0.8 ms. [Per-cycle timing and pool blocks](pressure.csv) and [per-cloud delivery and timing](clouds.csv).

For this laptop pilot, retain the current reader admission policy. Its 256-sample limit can retain roughly 256 times the cloud payload in publisher memory. A live publisher can retain its high-water allocation after the reader drains. These tests cover 64 KiB clouds, one stalled reader, and paced ingress up to 50 Hz. An unpaced microburst, several simultaneous stalled readers, or sustained production-size recording would require its own resource budget and measurements.

## Repeat and pilot decision

The isolated 1.23× paired p95 in the first batch prompted a fresh five-round four-arm repeat after the pressure test. All 480 additional groups and their metadata passed. The table below pools the two batches, excluding warmup from times as above. [Repeat group ages](repeat-groups.csv) and [repeat launch results](repeat-lidar.csv).

| Middleware | Full median / p95 (ms) | Partial median / p95 (ms) | Median whole-launch CPU (s) |
|---|---:|---:|---:|
| Native Agnocast | 2.461 / 4.217 | 41.807 / 42.602 | 2.000 |
| Standalone RMW | 2.243 / 3.211 | 41.530 / 42.150 | 3.040 |
| Cyclone DDS | 2.780 / 3.384 | 42.012 / 42.389 | 1.929 |
| Zenoh | 2.471 / 3.624 | 41.913 / 42.529 | 2.880 |

Across both batches, standalone/native was 0.91× at median, 0.76× at p95 and 1.52× in launch CPU. 1 of ten paired launch p95 ratios exceeded 1.2× (range 0.56–1.23×). The lowest host MemAvailable in the matrix was 10388 MiB. Retain bounded reader history for the pilot. Delivery and reuse held at both rates, and the representative Universe workload stayed within the latency milestone. The first-fill latency and elevated CPU remain measured costs.
