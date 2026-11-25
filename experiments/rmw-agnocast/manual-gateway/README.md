# Manual topic gateway on the standalone RMW

Use [migration tutorial step 7](../TUTORIAL.md#7-record-the-native-talker-from-a-cyclone-peer) for the build and operator commands. Each invocation owns one directed route. The supervisor runs two endpoints with a private framed Unix socket between them. Only the native endpoint requires the allocator hook and device. The forwarding path uses the installed ROS message class and serialized samples. This incurs encoding, copying and network cost. No type-specific gateway plugin was needed for either tested type.

## Replay on the provisioned laptop

Use the existing Release three-LiDAR standalone CIE graph, packaged hook, exported Lyrical userspace, Docker Engine and the stock Cyclone peer from [peer preparation](../network-peer/README.md). From the colcon worktree:

```bash
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
experiments/buffer/run-lyrical.sh colcon --log-base build/manual-gateway/log build --paths src/agnocast_cyclone_gateway --build-base build/manual-gateway/obj --install-base build/manual-gateway/install --executor sequential
```

On a fresh peer setup, prepare the image and persistent network once:

```bash
sg docker -c 'bash experiments/rmw-agnocast/network-peer/prepare.sh'
sg docker -c 'python3 experiments/rmw-agnocast/network-peer/batch.py build/lyrical-peer-ready --prepare-only'
```

With the peer running, choose a new output directory:

```bash
sg docker -c 'python3 experiments/rmw-agnocast/manual-gateway/check.py build/manual-gateway/replay conflict ingress absent idle active late restart crash active idle absent'
```

The harness selects domain 173 on both sides, uses the prepared Cyclone unicast configuration, and asserts the peer has no hook or native device. Cases run sequentially with memory-monitored host runtime scopes. A three-LiDAR run yields 60 local output groups, alternating full and partial. Each recorded cloud is decoded and compared to its local reference, including the stamp, layout and data. The ingress witness checks all 48 PoseArray messages with empty, single and 1024-element sequences.

## Observed behavior

| Scenario | Local clouds | Peer clouds |
|---|---:|---:|
| No gateway or advertised export with no reader | 60/60 each | 0 |
| Recorder attached before publication | 60/60 each | 60/60 in three runs |
| Attach and detach recorder during publication | 60/60 | 25 contiguous |
| Orderly gateway restart | 60/60 | 43 |
| Kill supervisor and start replacement | 60/60 each | 42 and 43 |

The idle native worker took zero samples. An active worker forwarded 39,333,420 serialized bytes per complete launch. After SIGKILL both workers terminated and the replacement acquired the route. Duplicate and reciprocal ownership attempts were refused while the original gateway held its locks. These locks coordinate processes sharing the same user, ROS domain and shared-memory filesystem. Distributed ownership and startup races between a future automatic launcher and the manual launcher still need tests.

[Launch results](results/launches.csv) report local full-cloud age from the fixture stamp, excluding the first group, with nearest-rank p95. Graph CPU includes launch and teardown. Endpoint CPU sums reported source and destination CPU from initialization of counters to teardown, excluding interpreter and ROS startup; killed workers cannot report. [Frame results](results/frames.csv) include partial groups and peer bag timestamps on the shared laptop clock. The active full-cloud median was 2.16–2.36 ms versus 2.14–2.34 ms without the gateway. This short series characterizes the functional workload. It cannot resolve a small latency difference or predict production network cost.

The first matrix invoked the installed executable directly to target signals precisely. The CLI repeat used ros2 run for ingress, active export and crash recovery, with a test-only command prefix to identify the supervisor. The current replay uses this CLI throughout. The gateway uses volatile QoS and has no history replay or delivery during a restart. Physical networks, same-name routes between native islands, higher-rate overload, physical link interruptions, and any specialized cloud forwarding path still need independent qualification.

## Recorder stalls and isolated network loss

Run on the same prepared peer with a fresh output directory. The sustained case also requires the previously built standalone cluster and tracker overlays used by run-sustained.sh.

```bash
sg docker -c 'python3 experiments/rmw-agnocast/manual-gateway/check.py build/manual-gateway/pressure-replay sustained-control sustained writer-stall partition stall partition active idle ingress late restart crash conflict'
python3 experiments/rmw-agnocast/manual-gateway/pressure-report.py build/manual-gateway/pressure-replay
```

The sustained runs publish 600 three-LiDAR groups at 10 Hz through the existing cluster/tracker workload. Both record locally throughout. The control leaves the peer running. The fault case suspends the matched stock peer recorder process for eight seconds. Readback compares each decoded remote cloud with the corresponding local bag entry. The short cases suspend the recorder, suspend the gateway DDS worker, or lower and restore only the test container's network interface. The partition cases use a one-second participant lease and a 200 ms discovery interval on both sides so the six-second fixture spans rediscovery. Other cases use the existing peer profile.

Before the fix a partition blocked a socket send for three seconds, causing the native worker and gateway to exit. The export callback now hands the newest waiting sample to a dedicated sender. It counts replacements while the sender is busy and keeps the ROS executor available for demand changes. The receiver sees complete frames after the writer resumes. DDS endpoints keep their configured QoS. A confirmed DDS match or a successful publish alone cannot establish that rosbag wrote the sample. Use bag readback for that claim.

The [pressure results](results/pressure.csv) classify missing clouds by their source stamps relative to the interruption (the corresponding eight-second observation window in the control), and report 50 ms samples of summed RSS for the supervisor and its two endpoint processes. This measure counts shared resident pages in each process and excludes kernel queues, helper processes and the recorder. The per-scope runtime limit is 512 MiB and the container limit is 384 MiB. The experiment measures one minute at the existing payload and rate. Physical hosts, longer outages, permanent stalls and workloads approaching the 64 MiB frame ceiling remain open.

In the uninterrupted one-minute control, both bags contained all 600 clouds. The clean eight-second recorder suspension yielded 530/600 on the peer and 600/600 locally, with all 70 missing source stamps inside the suspension. An earlier run recorded 520 and had ten additional missing stamps before the pause. Both gateway workers forwarded all 600 in each long run. Peak sampled gateway RSS was 174.4 MiB. Pausing the gateway DDS worker itself for four seconds reproduced a different failure mode: both runs took 60, forwarded 22, and counted 38 waiting samples replaced. The final sample reached the peer in every fault run. Use local recording when the archive must span remote outages.

## Simultaneous export and import

On the prepared laptop, start the stock peer if it is stopped. Run from the colcon worktree with a fresh output directory.

```bash
sg docker -c 'docker start awpilot0-stock'
sg docker -c 'python3 experiments/rmw-agnocast/manual-gateway/check.py build/manual-gateway/dual-replay dual-active dual-writer-stall dual-restart dual-restart dual-writer-stall dual-active'
python3 experiments/rmw-agnocast/manual-gateway/dual-report.py build/manual-gateway/dual-replay
```

Two manual gateway invocations share the ROS domain and device. The export carries native /output to Cyclone /manual/cloud. The import carries Cyclone /peer/poses to native /manual/local_poses. The same stock container runs the recorder and the PoseArray publisher. The native witness checks all 240 sequence IDs, array lengths, positions and orientation values under test. Receive timestamps establish whether the witness made progress during each export interruption. Readback decodes each recorded cloud and compares it against the local output for that source stamp.

The writer-stall case stops only the export DDS worker for roughly four seconds. The restart case terminates only the export supervisor, then starts its replacement and waits for the recorder match. Each case runs a fresh graph and import. [Per-run results](results/dual.csv) include summed endpoint worker CPU seconds, and the peak sampled sum of supervisor and endpoint RSS per route (50 ms samples, shared pages counted per process). Restart CPU includes both export generations, and peak RSS uses the larger generation peak. These figures exclude ROS application processes and do not estimate unique host memory.
