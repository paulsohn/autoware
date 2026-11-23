# Four-stack colcon replay at 524ad8a37

The laptop has an AMD Ryzen 9 8940HX (16 physical cores, SMT enabled) and ran Linux 7.0.0-43-generic. The subject is the standalone same-host rmw_agnocast shared library from the poll-install overlay. Every timed standalone arm uses the Lyrical prefix with Cyclone removed. All middleware timing runs are sequential on one laptop. The runtime guard sets a 512 MiB scope limit without swap, samples host MemAvailable, and stops the scope if host headroom falls below 2 GiB. The build guard runs one job. The source and executable changes in this replay are confined to benchmark fixtures and their runners.

## Results

Values are pooled steady median / nearest-rank p95 in milliseconds. The Path row uses the independent complete replay. The table uses the selected direct wait for Path, the combined loan/direct-wait profile for the two-edge crop and ordinary waits at the three-LiDAR synchronizer. The scoped branched crop uses ordinary waits at both crops.

| Workload | Native + bundled CIE | Standalone + standalone CIE | Cyclone DDS | Zenoh | Standalone/native |
|---|---:|---:|---:|---:|---:|
| Path, 10 Hz | 0.043 / 0.060 | 0.060 / 0.084 | 0.185 / 0.226 | 0.210 / 0.295 | 1.38× / 1.41× |
| Path, 50 Hz | 0.038 / 0.048 | 0.047 / 0.066 | 0.114 / 0.170 | 0.158 / 0.199 | 1.23× / 1.38× |
| Two-edge crop, 10 Hz | 0.329 / 0.496 | 0.419 / 0.691 | 1.241 / 1.512 | 0.997 / 1.317 | 1.27× / 1.39× |
| Two-edge crop, 50 Hz | 0.315 / 0.598 | 0.364 / 0.771 | 1.169 / 3.874 | 0.849 / 1.064 | 1.16× / 1.29× |
| Two crops and tap, 10 Hz | 0.596 / 0.952 | 0.634 / 1.047 | 1.872 / 2.132 | 1.452 / 1.738 | 1.06× / 1.10× |
| Two crops and tap, 50 Hz | 0.552 / 0.989 | 0.644 / 1.100 | 1.723 / 1.906 | 1.343 / 1.520 | 1.17× / 1.11× |
| Three LiDARs, nominal, complete group | 2.586 / 3.671 | 2.540 / 3.336 | 2.813 / 3.489 | 2.991 / 3.452 | 0.98× / 0.91× |
| Three LiDARs, double rate, complete group | 2.236 / 3.348 | 2.395 / 2.854 | 3.116 / 3.398 | 2.731 / 2.980 | 1.07× / 0.85× |
| Late top LiDAR, both region outputs | 90.496 / 90.678 | 89.496 / 89.952 | 90.233 / 90.779 | 89.947 / 95.822 | 0.99× / 0.99× |
| Near crop during competing timers | 2.648 / 4.145 | 2.493 / 3.326 | 4.018 / 5.310 | 2.958 / 3.677 | 0.94× / 0.80× |
| Three LiDARs through tracking and recording | 4.478 / 5.436 | Endpoint rejected | 6.446 / 7.189 | 5.647 / 6.423 | — |

The archive contains 276 timed-graph or attempted graph launches, plus six stock component churn runs and the standalone behavior probes. The lowest sampled host MemAvailable in the launch table was 11,764 MiB. Both sustained standalone attempts rejected the nested cluster endpoint. The six sustained control runs each verified 600 tracks and 600 bag records. Both orders of component churn saw 78/78 raw inputs and all expected loaded-window crops for standalone, Cyclone and Zenoh.

The two 10 Hz direct-wait Path batches exceeded the 1.2× native latency target independently. The regular standalone receiver was slower at the median in both. Under deliberate overflow each run retained 123/130 paired clouds and metadata on standalone, Cyclone and Zenoh, versus 57/130 with native. All recovered for the terminal window. During a same-callback stall, increasing the standalone near queue from 5 to 16 raised delivered outputs from 24/32 to 32/32; check frame ages before adopting a depth. A 100 ms rejection policy intentionally discarded stale inputs. The far branch continued throughout.

An early Path/overflow pilot overlapped another runner and was excluded. One fully collected isolated Path matrix had a launcher EOF error after its 40th verified trial during a harness edit. Its 2,400 verified rows remain labeled path; the independent path-repeat matrix completed with exit zero. The first sustained pilot exposed a cluster component search-path error after the clean Release install; the fresh forward/reverse matrix followed the fix. Each RMW row uses the same middleware binary for this baseline.

[Per-frame latency and warmup flags](frames.csv), [all grouped latency measurements](summary.csv), [per-launch resources](launches.csv), [queue and freshness delivery counts](delivery.csv), [pressure overlap](pressure-summary.csv), [component churn](component-churn.csv), [functional checks](behavior.txt), [endpoint failures](failures.csv).

## Configurations

| Workload | Native Agnocast | Standalone rmw_agnocast | Cyclone DDS and Zenoh |
|---|---|---|---|
| Nested Path at 10 and 50 Hz | Native writer and bundled AgnocastOnlyCallbackIsolatedExecutor reader | Standalone AWF CIE reader. Report the default receiver and the opt-in direct blocking wait with ready-route acquisition and drained descriptor reuse separately | Ordinary serialized Path, stock executor, local router for Zenoh |
| 32,768-point two-edge crop at 10 and 50 Hz | Native wrapper, bundled CIE crop | Standalone CIE crop with scoped loans on both publishers and a direct wait at the crop, the previously selected combined profile | Identical crop workload with ordinary ROS endpoints |
| Two crops with a first-stage tap, 10 and 50 Hz | Native wrapper and bundled CIE crops | Both crops use standalone CIE and scoped publisher loans. This branched fixture uses ordinary waits | Identical tap and sink checks |
| Three-LiDAR synchronizer | Native crop container, bundled mixed CIE at the synchronizer with a 1 ms wait | ROS component container, host views, standalone AWF CIE at the synchronizer. Nominal and double-rate also compare the optional drained direct wait on the top input | Ordinary transport, stock synchronizer executor |
| Near/far branch pressure | Bundled CIE at the synchronizer and near crop | Standalone CIE at the synchronizer and near crop | Standalone CIE at the near crop; stock synchronizer |
| Live crop load/unload | Native static crop container has no equivalent ROS component services | Stock ROS component manager | Same service driver |
| Live cluster, tracker and local SQLite recorder | Native cloud pipeline, fixture republishes /perception/cloud to ordinary Cyclone consumers | Standalone RMW for the graph, Python adapter and recorder; standalone CIE at the synchronizer | Each stack carries the whole graph |

These labels identify the executors at the component under test. Sources, sinks and upstream crop containers keep their normal fixture executors. On the Path workload each reader validates sequence, all nested strings and poses, and the retained previous message. Native and standalone must also match the writer addresses for the message, its vector and its nested string. The two serialized controls use the rclcpp allocator fallback and verify the same logical payload. Bare transport latency and executor overhead cannot be separated by this design.

## Reproduce

Start in the provisioned colcon worktree with a fresh output directory. Use the exported Lyrical root, installed kernel device and release heap hook from the standalone setup. Run each command separately. The two build commands use the build memory guard; the benchmark runners select the runtime guard.

```bash
out=build/standalone/benchmark-replay
mkdir "$out"
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
runner=experiments/rmw-agnocast/standalone
bash "$runner/bench-pressure.sh" "$out/pressure"
bash "$runner/bench-churn.sh" "$out/churn"
bash "$runner/bench-universe.sh" "$out/nominal" --cie-nominal
bash "$runner/bench-universe.sh" "$out/double" --cie-regression
CROP_PERIOD_MS=100 bash "$runner/bench-crop.sh" "$out/crop10" --final-cie
CROP_PERIOD_MS=20 bash "$runner/bench-crop.sh" "$out/crop50" --final-cie
experiments/buffer/run-lyrical.sh bash "$runner/build-final-probes.sh" path
bash "$runner/bench-path-final.sh" "$out/path"
bash "$runner/bench-path-final.sh" "$out/path-repeat"
CROP_PERIOD_MS=100 bash "$runner/bench-crop.sh" "$out/branched10" --branched-cie
CROP_PERIOD_MS=20 bash "$runner/bench-crop.sh" "$out/branched50" --branched-cie
experiments/buffer/run-lyrical.sh bash "$runner/build-final-probes.sh" perception
bash "$runner/bench-sustained-final.sh" "$out/sustained"
for condition in extended overflow jitter regions regions-fast slow busy depth-5 depth-8 depth-16 fresh-keep fresh-reject; do
  bash "$runner/bench-pressure.sh" "$out/$condition" "$condition"
done
python3 "$runner/analyze-final.py" "$out" "$out/results"
python3 "$runner/analyze-pressure.py" "$out/pressure" "$out/churn" "$out/results"
```

For functional replay, use the same stripped bwrap invocation as bench-churn.sh and run standalone/run-universe.sh --ownership, standalone/run-universe.sh --check and standalone/bench-functional-final.sh in separate guarded scopes. Select the current poll-install overlay inside that prefix.

For Path and crop, discard ten warmups from each 60-frame launch. For synchronizer and branch results, discard the first four acquisitions and keep complete and intentional partial groups separate. For the sustained tracker, discard twenty warmups from each 600-acquisition run. The pressure overlap window selects groups whose source stamps fall inside both measured callback stalls. Report pooled median and nearest-rank p95 within a workload and stage. CPU and RSS retain their measurement boundary per fixture. Record counts and failed launches separately, including recorder readback.


## Scope of the sustained failure

The clustering observer requests tier4_perception_msgs/msg/DetectedObjectsWithFeature. Its feature_objects sequence contains DetectedObjectWithFeature, whose Feature contains a PointCloud2 cluster. The current RMW detects the nested Buffer but selects an enclosing serialized representation only for ConcatenatedPointCloudInfo. It rejects the cluster endpoint before discovery or payload delivery. Native uses a Cyclone fan-out after the native pipeline, including for this nested topic; a complete native row measures that deployed arrangement.

Before gateway qualification, test a general serialized representation for enclosing Buffer layouts in the local RMW. Preserve the top-level PointCloud2 host-view and loan paths. Verify C++ and Python typed publication/subscription, retained data and serialized take/readback with populated, empty and multiple nested clouds, then repeat this exact tracker/recorder graph. Confirm that the pre-existing fast paths keep their previous ownership and timing. Dynamic backend negotiation and zero-copy nested objects require separate evidence. The baseline carries the original RMW binary throughout. [Binary fingerprints](binaries.sha256).
