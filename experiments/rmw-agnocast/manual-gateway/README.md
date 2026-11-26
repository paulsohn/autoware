# Manual topic gateway on the standalone RMW

Use [migration tutorial step 7](../TUTORIAL.md#7-record-the-native-talker-from-a-cyclone-peer) for the build and operator commands. A configuration can group several directed routes. For each route the supervisor runs two endpoints with a private framed Unix socket between them. Only the native endpoint requires the allocator hook and device. The forwarding path uses the installed ROS message class and serialized samples. This incurs encoding, copying and network cost. No type-specific gateway plugin was needed for either tested type.

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

## Opt in to startup from an RMW context

One file describes the topics a participating context should bridge. Set `RMW_AGNOCAST_GATEWAY_CONFIG` to its path. Put native-to-network routes in `exports` and network-to-native routes in `imports`. Either list can be omitted. Every entry names the local Agnocast topic in `topic`. For an export, `remap_to` names the network destination. For an import, `remap_from` names the network source. Omit the directional key to use the same name on both sides.

```json
{
  "dynamic_discovery": false,
  "network_rmw": "rmw_zenoh_cpp",
  "heaphook": "/opt/ros/jazzy/lib/libagnocast_heaphook.so",
  "exports": [
    {"topic": "/lidar/points"},
    {"topic": "/status", "remap_to": "/robot/status", "type": "std_msgs/msg/String"}
  ],
  "imports": [{"topic": "/commands", "remap_from": "/to_robot/commands"}]
}
```

Provide `type` when a destination must be advertised before a source publisher starts. Otherwise the source worker checks publisher endpoint types and activates once it sees exactly one distinct type. A missing or ambiguous type leaves that route pending; other routes can run and application initialization can finish. Discovery binds the type for the current owner lifetime. Install the interfaces on both workers. Volatile traffic sent before activation can be lost.

Set `network_rmw` and `heaphook` once for the group. The default network is Cyclone. An optional top-level `qos` object supplies defaults such as `{"depth":20,"input_reliability":"best_effort","output_reliability":"reliable"}`. A route's `qos` object overrides individual fields. An export can set `always_on` to keep its subscription armed without network readers. The earlier single-route object and legacy argument-array files remain readable.

For an operator-managed group use the same file:

```bash
ros2 run agnocast_cyclone_gateway topic_gateway --config "$RMW_AGNOCAST_GATEWAY_CONFIG"
```

One supervisor owns the complete group and starts two worker processes per route. Matching groups share ownership irrespective of declaration order. An overlapping subset cannot silently attach to part of an existing group. Route locks reject duplicate destinations and local reciprocal forwarding, including across gateway processes in the same ownership scope. An endpoint process failure can restart the whole automatically owned group. Use separate owners for routes requiring independent failure recovery. Loop prevention across unrelated hosts is unqualified.

### Discover a peer Agnocast application

Set `dynamic_discovery` explicitly in each grouped JSON file. Use the JSON boolean `false` to run only the listed routes. Use `true` on both native islands to add same-name routes when a native publisher and a compatible native subscriber appear. The option takes effect when starting the gateway. Existing grouped files need the new field. Legacy single-route files and positional commands remain static.

For example, use the following discovery-only file on both sides after setting the absolute hook path for each host. Select the same network RMW and domain and a reachable network interface or Zenoh router.

```json
{
  "dynamic_discovery": true,
  "discovery_namespace": "/sensing",
  "network_rmw": "rmw_cyclonedds_cpp",
  "heaphook": "/opt/ros/jazzy/lib/libagnocast_heaphook.so"
}
```

The optional namespace bounds matching to topics below that namespace. It defaults to the root namespace. Explicitly configured route names take precedence in the same file. Use explicit routes for remapping, stock peer recorders and QoS outside the tested volatile profiles. The pilot sends native endpoint snapshots over the selected network every half second, filters gateway nodes, and expires silent peers after three seconds. Both workers need installed message types. Route startup and graph convergence can lose early volatile samples. Two isolated containers on one laptop have exercised this path.

The discovery topic is `/_rmw_agnocast/discovery_v1` with ROS type `std_msgs/msg/String`. Its `data` field carries JSON of the form `[island_id, generation, rows]`. The island ID combines the boot ID and IPC namespace inode. The generation is the worker's monotonic start time in nanoseconds. Each inventory row is `[absolute_topic, ros_type_name, type_hash_string, role, reliability]`, where role is `pub` or `sub` and reliability is `reliable` or `best_effort`. The local observer sends the rows to its network worker over a Unix datagram socket. The application data route publishes the discovered ROS message type on its own topic.

### Select the gateway network

The installed package provides [cyclonedds.json](../../../src/agnocast_cyclone_gateway/config/cyclonedds.json) and [zenoh.json](../../../src/agnocast_cyclone_gateway/config/zenoh.json). Each exports native /topic as /from_robot/topic and imports network /to_robot/commands as local /commands. Cyclone is the default when network_rmw is omitted. The package retains its existing name and Cyclone dependency. Install rmw_zenoh_cpp on the gateway and peer to select Zenoh, and start a reachable Zenoh router using your usual session configuration. On the peer select the same RMW and ROS domain.

In the native application terminal, after sourcing both overlays and setting the heaphook, select either installed file. Copy the file and edit the heaphook field if the host uses another absolute hook path.

```bash
export RMW_IMPLEMENTATION=rmw_agnocast
export AGNOCAST_GATEWAY_EXECUTABLE="$(ros2 pkg prefix agnocast_cyclone_gateway)/lib/agnocast_cyclone_gateway/topic_gateway"
export RMW_AGNOCAST_GATEWAY_CONFIG="$(ros2 pkg prefix --share agnocast_cyclone_gateway)/config/cyclonedds.json"
```

For Zenoh, change the final assignment before launching the native application:

```bash
export RMW_AGNOCAST_GATEWAY_CONFIG="$(ros2 pkg prefix --share agnocast_cyclone_gateway)/config/zenoh.json"
```

Migrate launch environments using the old AGNOCAST_GATEWAY_CONFIG name to RMW_AGNOCAST_GATEWAY_CONFIG and restart contexts and gateways together when updating versions. For manual startup, pass --network-rmw rmw_zenoh_cpp or --network-rmw rmw_cyclonedds_cpp directly to topic_gateway. The gateway distinguishes route owners by network RMW and the selected network's profile settings.

Each opted-in rmw_init waits for the group workers to initialize. Inferred routes may still be waiting for a source type at that point. A short-lived helper joins the matching owner or starts an independent supervisor. Each context holds a socket lease until finalization or process exit. The automatic owner shuts down after its last lease closes. An explicitly launched matching manual owner stays up until its operator stops it. Route names, direction, installed type, QoS, domain, effective hook path and the selected network RMW and profile settings must agree. A matching manual command must be running before context initialization to reuse it. A new manual launch while the route is already owned can report a conflict. Unset RMW_AGNOCAST_GATEWAY_CONFIG for the existing local-only behavior. The gateway's own worker clears that variable before initializing ROS.

Replay from the provisioned worktree with both rebuilt overlays. The stock Cyclone witness runs on loopback. Two ordinary rclpy applications synchronize just before rclpy.init. The elected starter exits first. The witness checks the publisher count, delivery order and content through both exits. The automatic case checks the actual supervisor PID exits. The manual case checks it stays running after both applications exit.

```bash
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
export BUFFER_RUNTIME_ONLY=1
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/manual-gateway/check-ownership.sh build/manual-gateway/ownership-replay
```

Startup ownership is local to the user and route configuration. Route discovery and live policy changes remain open. Keep the route file and executable available at their original paths throughout the context lifetime. The auto supervisor currently inherits the first application's stderr for diagnostics. Deployments that capture application output through pipes must account for the supervisor retaining that descriptor.

## Recover an automatic owner after supervisor loss

Updated native contexts request a recoverable lease during the existing startup handshake. When the matching owner is automatic, each participating context parks one thread on the lease and a finalization eventfd. Supervisor death closes the lease. The surviving contexts enter the same serialized election used at startup. A context can make three recovery attempts over its lifetime, with delays of 100, 200 and 400 ms before those attempts. A successful election counts toward the budget. Once exhausted, the context logs the failure and local ROS operation continues. Restart participating applications to renew their budgets. An in-progress bootstrap can delay finalization until that join finishes. Contexts attached to a manual owner or an older gateway keep the original operator-managed lifetime.

The test runs two native String publishers at 20 Hz each, offset by 25 ms. A loopback Cyclone subscriber witnesses the kill after receiving at least ten messages from each. It checks ordering, uniqueness, the end of both streams, the replacement supervisor identity, and final owner exit. The recovery time is the interval between sending SIGKILL and receiving a sample from a publication slot scheduled after the kill. This includes owner election, endpoint creation, ROS discovery and the publication schedule. The experiment does not replay missed volatile samples.

With the overlays rebuilt, run inside the same memory-guarded setup as the ownership replay above. Use a fresh output path each time.

```bash
export TEST_OWNER_CRASH=crash
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/manual-gateway/check-ownership.sh build/manual-gateway/live-crash
export TEST_OWNER_CRASH=exhaust
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/manual-gateway/check-ownership.sh build/manual-gateway/retry-limit
export TEST_OWNER_CRASH=manual-stop
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/manual-gateway/check-ownership.sh build/manual-gateway/manual-stop
unset TEST_OWNER_CRASH
```

## Hand off a route as the last context exits

For a matching configuration, a new context can join the current automatic supervisor while it still accepts leases. Once retirement starts, a successor waits for endpoint teardown and route-lock release. A manual and an automatic launcher can both contend for the released route. Whichever claims it first owns it. A matching incoming context joins either winner. A manual command that loses to an active owner reports a conflict. A manual owner continues running after the application exits and needs an operator stop.

The laptop replay uses two native publishers and a stock loopback Cyclone subscriber. The overlap case joins the new context before the original one exits. The race case releases both applications on the same filesystem marker. The drain and competing-launcher cases briefly suspend one old worker to hold the supervisor in its real teardown path. Linux pidfds verify worker and supervisor exit. The witness checks one matched publisher during successor traffic and checks the exact 40-message sequence. The two publishers send on either side of the handoff. This test does not measure continuous delivery through the transition.

```bash
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
export BUFFER_RUNTIME_ONLY=1
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/manual-gateway/check-ownership.sh build/manual-gateway/handoff-replay --handoff overlap overlap-manual race drain manual-first auto-first successor-first
```

## Measure what a running recorder loses during handoff

Keep the source, its local rosbag2 recorder, and the Cyclone rosbag2 recorder alive throughout each trial. The native source and local recorder run without RMW_AGNOCAST_GATEWAY_CONFIG. Separate native contexts control the gateway lifetime. Each application message contains a sequence number and the laptop wall-clock timestamp taken just before publication. Each bag uses SQLite with the recorder cache disabled. The witness decodes both bags, checks the exact source timestamp for every forwarded sequence, and checks ordering, duplicates and the final sequence.

On the provisioned laptop with the current overlays, run the balanced matrix from the colcon worktree. Use a fresh output directory. This multi-process fixture uses the 1,200 MiB aggregate memory guard. Trials run sequentially.

```bash
export LYRICAL_ROOT="$(cat build/agnocast-lyrical/universe-root)"
unset BUFFER_RUNTIME_ONLY
experiments/buffer/run-lyrical.sh bash experiments/rmw-agnocast/manual-gateway/check-ownership.sh build/manual-gateway/continuity-replay --continuous stable overlap replace manual-lead auto-lead auto-lead manual-lead replace overlap stable
```

The source publishes 100 samples at 20 Hz, starting only after the native recorder and gateway have subscribed. After sequence 29, the driver records an event timestamp. In the stable control this is only a timing marker. In overlap it admits the incoming context before letting the old context exit. In replacement it waits for the outgoing supervisor to exit before releasing the incoming context. The two competing cases also launch a manual gateway, either just before releasing the incoming context or 120 ms afterward. The result reports the actual owner. The overlap check verifies that the original supervisor stayed alive.

[Trial results](results/continuous-handoff.csv) report missing sequence ranges (inclusive, zero-based), the longest interval between consecutive Cyclone bag receipt timestamps, and the elapsed time from the event to receipt of the first sample published after the event. These metrics include the publication cadence and discovery. A 20 Hz stream has a nominal 50 ms interval even with uninterrupted forwarding. Both recorders use the same machine clock; this fixture does not infer one-way delay between physical hosts.
