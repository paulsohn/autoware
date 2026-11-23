# Targeted tuning after nested Buffer support

The control is the accepted colcon middleware at 773a1ae66. Native uses bundled CIE and standalone uses the standalone AWF CIE with the selected direct wait. The two-edge crop also uses the previously qualified scoped publisher loans. Each comparison ran four alternating-order pairs on the laptop at each cadence. Each launch verified all 60 frames and excluded ten warmups. The same build, runtime memory guard and quiet-between-launch procedure as the four-stack baseline applied throughout.

The storage candidate reuses the polling arrays on each RMW wait set, rebuilding their contents each time, and retains the context lock across readiness and take when a direct reader already has a pending loan. The Lyrical RMW contract disallows concurrent wait on the same wait set. The self candidate adds suppression of the current waiter's own eventfd signal while it is synchronously acquiring native loans. Other waiters and the condition variable still receive notifications. It was tested twice. The repeat reversed workload order. The fixed arm restored the accepted Buffer fix alone, rebuilt it, reran functional checks and repeated the same paired matrix. All tests used blocking waits.

To reproduce from this experiment branch, the patches apply independently on top of 773a1ae66. Keep the paired benchmark runners from this report. Apply just one candidate to a clean copy of the middleware source, build and run one batch at a time. Use an unused worktree-relative directory for every batch.

```bash
runner=experiments/rmw-agnocast/standalone
out=build/standalone/fresh-targeted-batch
mkdir "$out"
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
experiments/buffer/run-lyrical.sh bash "$runner/build-poll.sh"
bash "$runner/bench-path-final.sh" "$out/path" --paired
CROP_PERIOD_MS=100 bash "$runner/bench-crop.sh" "$out/crop10" --final-cie --paired
CROP_PERIOD_MS=20 bash "$runner/bench-crop.sh" "$out/crop50" --final-cie --paired
python3 "$runner/analyze-final.py" "$out" "$out/report"
```

[Storage candidate](patches/wait-storage.patch), [storage plus self-notification candidate](patches/skip-self.patch), [pooled ratios and absolute latencies](pooled.csv), [all four paired rounds](paired.csv). The variant directories contain the frame-level timings and per-launch CPU, peak child RSS and host memory headroom. Candidate builds and the restored build passed the standalone wait, ownership, slow-reader and stock project checks. The nested Buffer fix independently passed the two full tracker/recorder replays. [Nested graph results](../nested-2b40ab7c6/result.txt).

## Review decision

The restored build measured 1.59×/1.05× native median/p95 on 10 Hz Path, 1.19×/1.22× on 50 Hz Path, 1.17×/1.21× on 10 Hz crop and 1.17×/1.07× on 50 Hz crop. At 10 Hz the Path median was 74.7 µs versus native 47.0 µs. In the earlier two unmodified Path batches the corresponding ratios were 1.43× and 1.38×. The storage candidate recorded 1.46× median. The self candidate gave 1.12× in its first batch and 1.58× in its independent repeat. Its 10 Hz crop medians were 1.23× and 1.26×; its p95 ratios were 1.12× and 2.17×. These comparisons supply no reproducible across-workload gain for either candidate. Keep the Buffer compatibility fix. Pause for review before the next full regression.
