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

The first matrix invoked the installed executable directly to target signals precisely. The CLI repeat used ros2 run for ingress, active export and crash recovery, with a test-only command prefix to identify the supervisor. The current replay uses this CLI throughout. The gateway uses volatile QoS and has no history replay or delivery during a restart. Physical networks, same-name routes between native islands, sustained overload, network partitions, and any specialized cloud forwarding path still need independent qualification.
