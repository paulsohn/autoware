# Inter-host topics and recording with rmw_agnocast

## Stage 1 migration qualification

Run the [single-laptop migration and conformance job](rehearsal/README.md) before advancing the deployment baseline. The checked-in case inventory makes API regressions visible and records outstanding differences. Physical-host qualification and an independent developer following the tutorial remain open.

## Next colcon experiments in order

The configured gateway at `fe8ce6977` is the control. Grouped routes, both startup modes, and laptop loopback delivery already have Cyclone DDS and Zenoh evidence. Recorder outages and isolated network tests have more extensive Cyclone coverage. The following queue takes precedence over historical next steps later in this document.

1. **Establish the two-island configured control.** Provision two isolated native IPC environments on the laptop and give their network workers independent addresses. Verify isolation with the same ROS domain, a live publisher and subscriber, both gateways off, and zero native matches or delivery. Then enable a configured export on A and import on B. Verify ordered payloads and check the network byte counters. Start with Cyclone, then repeat with Zenoh. Reuse the stock peer for stock-RMW controls. If the native isolation check fails, resolve the fixture before interpreting any bridged result.
2. **Discover endpoints with manually started gateways.** Begin with one native publisher on A and one native subscriber on B, same topic and compatible type/QoS, and zero configured data routes. Inspect the [AWF decision function](../../src/core/agnocast/src/ros2agnocast_discovery_agent/ros2agnocast_discovery_agent/bridge_decider.py) and [per-island snapshot](../../src/core/agnocast/src/ros2agnocast_discovery_msgs/msg/AgnocastDaemonState.msg) for reuse. Obtain application endpoints from each native graph and exchange offers and interests on a reserved network topic. Identify the originating island and distinguish successive agent instances. Reconcile snapshots after restart and expire stale peers. Keep the selected network RMW inside the gateway. Start with a permitted test namespace; use explicit remapping for different topic names.
3. **Qualify route lifetime and ownership.** Exercise both startup orders, multiple readers, last-reader exit, peer crash, network partition and reconnect. Verify one export for multiple readers, one import per receiving island, eventual teardown, and zero idle payload takes. Reject incompatible types/QoS and exclude gateway endpoints and discovery traffic from matching. Reconcile with explicit routes first so a discovered route cannot duplicate or tear down a manual route. Then repeat with RMW-triggered gateway startup, including concurrent contexts and recovery. Prove that the existing group ownership rules allow unrelated configured routes to survive dynamic churn.
4. **Qualify stock peers and recording.** Use the ordinary network graph to match a stock publisher or existing subscriber without installing a discovery agent on that peer. Test an explicit-topic recorder and stock `ros2 bag record -a`, starting each before and after the source. Decode bags and check sequences. A recorder that waits for an advertised data publisher can stay invisible to a demand-only exporter. Preserve a configured, advertised export as the control. Decide whether an optional advertisement policy is warranted from the observed bootstrap behavior.
5. **Run the paired performance matrix below, then qualify deployment.** Collect timing markers from step 1 onward. Run the repeated matrix after lifecycle behavior is stable, then replay the three-LiDAR workload with a remote recorder. Repeat the selected delivery, reconnect and performance cases on two physical hosts when available. Keep Bazel deferred.

**Progress.** Steps 1 and 2 passed on two isolated laptop containers for Cyclone DDS and Zenoh. Step 3 passed ordinary last-endpoint teardown and reattachment in either startup order, including middleware-triggered startup. The configured-only control kept an unlisted topic unmatched. The mixed-route lifecycle fixture passed two readers sharing a route, last-reader removal, a peer-interface partition, receiver-container kill and restart, and a compatible context joining the live owner. Configured traffic resumed after each fault. Cold simultaneous launch passed on both network RMWs. Compatible contexts shared one owner; conflicting namespace policies elected one owner, including manual versus RMW-triggered launch. Step 4 passed stock typed readers and writers on both networks. Both stock Lyrical recorder modes needed a configured advertisement in either startup order. Retain configured exports for logging. The matched-QoS two-island startup, delivery and idle comparison and the 600-group Cyclone recorder replay are recorded in the [journal](../buffer/JOURNAL.md#measure-the-discovery-startup-cost-and-replay-a-stock-three-lidar-recorder). The [Universe graph-churn experiment](../buffer/JOURNAL.md#measure-universe-graph-churn-and-interleave-matched-qos-cloud-controls) exercised up to 48 silent offers, eight changing offers, two auxiliary routes, a native-only control, and full Cyclone and Zenoh peer bag readback. With 48 silent offers the native discovery worker approached one CPU core on both networks. The [shared native inventory experiment](../buffer/JOURNAL.md#reuse-the-native-graph-inventory-for-discovery-queries) reduced that worker to 0.74–1.03% of one core with full cloud delivery. Native crash cleanup and domain isolation passed. One loaded Zenoh observer previously missed a retirement. In the [two-observer replay](../buffer/JOURNAL.md#compare-independent-zenoh-graph-observers-during-loaded-route-retirement), both graphs retired all 28 GIDs within 0.56 s, including repeated same-name routes. Keep the seven-second check and capture observer identities on recurrence. Physical-host qualification remains deferred. [Operator configuration](manual-gateway/README.md#discover-a-peer-agnocast-application)

### Dynamic discovery performance test

Pair a configured route with a discovered route using identical workers, startup mode, backend, QoS, payload, rate and reader count. Add a gateway-off native-only run for local overhead. Compare each backend against its own control. Begin with a small sequenced message, then repeat with a representative PointCloud2 stream at the recorded size and rate. Include no match, an active match, a second reader and detach/rejoin. Include a steady native stream while unrelated endpoints churn to expose graph scanning or worker startup costs.

| Measurement | Evidence to retain |
| --- | --- |
| Activation and recovery | Timestamp endpoint creation, first visible compatible match, route readiness and first correct receiver sample. Record last departure to teardown and reconnect to first correct sample. Report pre-ready loss separately. |
| Forwarding under load | After readiness record sent, received, missing and duplicate sequence IDs, throughput and median/p95 latency. Decode recorder samples. Record native subscriber latency alongside remote delivery. |
| Idle and scale costs | Per-process CPU and RSS, worker count, idle native takes, and network control bytes separately from payload bytes. Sweep the number of advertised topics while holding active routes fixed, then increase active routes. Record actual counts and memory headroom. |

Use monotonic timestamps on the laptop after confirming a shared clock across the fixtures. Across physical hosts use round trips or report measured clock uncertainty. Alternate configured/discovered order, keep repeated paired runs and their spread, and publish raw counts with the median/p95. Run builds and trials sequentially under the existing memory guard. Evaluate incremental discovery cost against the configured gateway. The historical 1.2× native latency target describes the local RMW engineering milestone; it is not an acceptance threshold for network discovery.

## Standalone RMW milestone

Selecting rmw_agnocast with the required device and heap hook must carry supported same-host topics in shared memory by default. Ordinary ROS applications need no lists of publisher or subscriber topics. Unsupported operations must report their status explicitly. There is no DDS fallback. Cyclone belongs in the optional, manually launched network gateway. A local-only deployment must build, load and operate without rmw_cyclonedds_cpp installed.

The historical mixed prototype links the Cyclone RMW, inherits unimplemented entry points through that library, creates Cyclone endpoint handles, and uses delegated waits and serialization. Its single-process gateway also relies on that mixed implementation. Prior measurements qualify the selected native data path and gateway use cases; standalone graph, wait and bridge behavior require independent validation.

First demonstrate two ordinary rclcpp processes exchanging a standard message on a previously unspecified topic in a Lyrical userspace lacking the Cyclone RMW. Check dependency resolution, endpoint discovery, blocking wakeup, payload receipt, shutdown, and an actionable error when native transport is unavailable. Then qualify other message layouts, QoS, graph tools, service/client support and a gateway with explicitly separate native and DDS endpoints.

The [standalone implementation and isolated checks](standalone/README.md) cover default rclcpp nodes, generated C and C++ types, concurrent service clients, graph discovery and a stock local recorder. The [manual standalone gateway experiment](manual-gateway/README.md) validates independent native and Cyclone endpoints on a laptop and stock container peer. The [stall and partition replay](manual-gateway/README.md#recorder-stalls-and-isolated-network-loss) covers a slow recorder, blocked DDS writer, and peer link recovery. The export keeps one waiting sample and reports overwrites under backpressure. The [simultaneous export and import](manual-gateway/README.md#simultaneous-export-and-import) qualify independent manual routes on the laptop. The [opt-in RMW startup experiment](manual-gateway/README.md#opt-in-to-startup-from-an-rmw-context) passed a two-context race, starter exit, last-context cleanup and reuse of an existing manual owner. The [live-context crash check](manual-gateway/README.md#recover-an-automatic-owner-after-supervisor-loss) covers replacement election, the retry budget and preservation of manual ownership. The [last-context handoff](manual-gateway/README.md#hand-off-a-route-as-the-last-context-exits) covers overlap, teardown and competing manual launches. The [continuous recorder test](manual-gateway/README.md#measure-what-a-running-recorder-loses-during-handoff) verified uninterrupted overlap and measured the gap during full replacement. Grouped exports and imports support type discovery for named topics. The opt-in discovery pilot now selects same-name routes between two native islands on the laptop. Physical two-host qualification remains open. Use the standalone API coverage table when selecting its endpoints and QoS.

## Prototype deployment contract

Keep the pilot on Lyrical and colcon. On each host that owns native topics, a gateway shares the applications' /dev/agnocast, kernel ABI, heap hook and versioned route layout. Cross the network with ordinary Cyclone DDS samples. A laptop or remote recorder needs DDS connectivity, compatible QoS and the ROS interfaces required by its tools. It should run stock rosbag2 with no Agnocast device, allocator shim or CLI extension. An ordinary DDS publisher can feed an explicitly permitted native input in the reverse direction. Two native islands can use an export on the source host and an import on the destination host. Acknowledge that topology only after verifying delivery across distinct transport domains at the device level on two machines. Services, actions, GPU-native networking and transparent mixed-vendor interoperability remain separate qualifications.

A local recorder has two candidates. Benchmark stock rosbag2 on the DDS export as the common deployment and stock rosbag2 using rmw_agnocast's selected native subscription with the hook as a local-only comparator. In both cases rosbag writes a serialized representation. Retain a volatile KeepLast baseline with documented reliability and depth. Start the delivery contract at observed compatible match and route readiness. Report earlier samples and gaps during restart separately. Use distinct native and DDS names first. A graph-visible native endpoint already has a Cyclone handle, so a discovered name or matched count alone cannot establish payload connectivity or external demand.

## Route ownership and activation

Start with one configured direction, input/output name, expected ROS type or discovery policy, and QoS per route. Use the same routing engine for manual and automatic startup. Require an unambiguous installed message type and compatible type identity when available. Reject conflicting routes, reciprocal cycles and ambiguous types with an actionable status. Pin a single exporter per logical source and DDS destination across the deployment. Reject duplicate ownership, including simultaneous manual/auto startup. Represent two-way flows with separate directed names. Qualify same-name routes separately on a real multi-host graph before enabling them. Republish preserves message content and header stamps. DDS publisher identity changes at the crossing.

1. **Manual**. Start the configured gateway explicitly. Keep a cheap DDS publisher advertised for each export so topic discovery and late recorder attachment can work. Attach the native reader on the first compatible external reader; tear it down after the last one leaves with bounded hysteresis. Reconcile endpoint changes using graph events and periodic reconciliation if needed. Account for discovery races, the gateway's own endpoints, native proxy endpoints and other gateways. If demand cannot be identified safely, retain explicit route activation. Validate an idle period with zero native payload takes. Keep volatile ingress explicitly armed where first-sample delivery matters.
2. **Automatic**. The first participating RMW context arranges a single gateway at startup. This is separate from activating individual routes. Prototype a host/user/ROS-domain scoped rendezvous with compatible device, route policy and network configuration, an ownership lock, readiness handshake, crash recovery and bounded retries. An incompatible existing owner must be reported. The gateway opts out of autostart to prevent recursion. Start two applications simultaneously, exit the starter while its peer runs, crash/restart the gateway, and stop the last application. Measure startup-to-first-usable-route and orphan count. Prefer automatic deployment only once these cases and manual override work. An explicitly launched supervisor that respawns remains manual startup.
3. Keep the Cyclone-integrated opt-in alternative as a later design comparison. Reuse the same traffic and metrics if prototyped. The existing delegated Cyclone implementation already supplies DDS; changing ownership of the gateway is a separate engineering decision.

## Message representations

The pinned upstream bridge loader selects a generated plugin and falls back to a generic bridge. Its legacy object layout differs from this RMW's versioned routes. Reuse the dispatch principle and inspect the upstream conversion routines before adapting any code. The existing pilot relay has compiled Path and PointCloud2 callbacks and skips publishing at zero DDS matches, but still takes every input. Keep it as the control. [Upstream loader](../../src/core/agnocast/src/agnocastlib/src/bridge/agnocast_bridge_loader.cpp) · [Pilot relay](../../src/agnocast_rmw_loan_probe/route_bridge.cpp)

For egress, try a runtime-typed generic subscription and serialized DDS publication. Compare with the compiled paths using the same QoS and input sequences. Retain a specialization when it saves measured CPU, copies or allocations. A scoped PointCloud2 host view is a candidate; measure materialization during serialization and release the native owner promptly. The RMW's serialized take currently materializes host-cloud bytes, copies already encoded CDR, or serializes a native object according to layout. Never send a shared pointer, device address or local Buffer descriptor as the wire representation. Prove readability with a receiver lacking the backend.

For ingress, publishing serialized bytes to a selected native topic is a separate capability. GenericPublisher calls rcl_publish_serialized_message. There is no rmw_publish_serialized_message override in the prototype, so delegation can put the sample on DDS while the native reader receives nothing. First prove receipt at a native-only consumer with a DDS-only negative control. Consider runtime C++ type support plus deserialize into a properly initialized object and typed RMW publish, or a bounded serialized-publish implementation reusing the native writer. Check object destruction on errors. Qualify each direction independently with a type absent from the relay binary, e.g. a nested variable-length ROS message, plus configured Buffer/CDR and specialized PointCloud2 cases. Missing type support, type mismatch and unsupported QoS must fail the route visibly without stopping other routes. Prefer deferring an expensive generic direction over silently losing or misrouting samples. [RMW implementation](../../src/rmw_agnocast/rmw.cpp) · [Generic publisher](../../src/rclcpp/rclcpp/src/rclcpp/generic_publisher.cpp)

## Native CLI inventory and retirement checks

All upstream entry points are accounted for below. Keep the native package available for legacy deployments. The ordinary ROS commands are proposed pilot interfaces; passing command output and payload checks will decide which replacements work. [Registered verbs](../../src/core/agnocast/src/ros2agnocast/setup.py)

| Native verbs | Role and proposed RMW check |
| --- | --- |
| topic list/info, node list/info (all suffixed _agnocast) | Merge/annotate native discovery. Try stock topic list -t, topic info -v, node list and node info locally and from the DDS side. Compare endpoint names, counts, types, QoS and disappearance against controlled endpoints, including a visible native proxy with the route off. Keep native-only diagnostics for native participants unreachable through the RMW graph. |
| topic echo/hz/delay (suffixed _agnocast) | These activate a bridge for observation. Run ordinary verbs on the export while traffic is live; verify payload, rate and stamped-message delay against source data. Check first attachment, two observers, last departure, idle cleanup and the local RMW option separately. |
| bag record_agnocast | Upstream activates matching routes while recording. Test stock bag record for explicit topics, regex and all topics, including a late-added permitted export. Verify recorded sequence, timestamps, type and decoded content. Advertise known exports before a reader exists to avoid a discovery deadlock. |
| agnocast bridge-daemon-status / discovery-daemon-status | Describe the native daemons. For this gateway expose owner, health, selected types/QoS, matched demand, active routes, drops, errors and restart counts. Keep these verbs when operating the upstream daemons. |
| agnocast generate-bridge-plugins / version | Optional tooling for upstream compiled plugins and installation diagnostics. An RMW gateway with its own typed adapters has no runtime dependency on the generator. Retain version/ABI diagnostics for the local userspace and kernel. |

Test stock tools with a fresh CLI daemon per RMW/domain or direct discovery where supported. Validate remote DDS tools with only stock ROS installed. Record whether a discrepancy is in graph visibility, CLI type support, data transport or route activation before adding any shim.

## Colcon experiments in order

1. **Recorder lifecycle and cost on the laptop.** Reuse the rebuilt three-LiDAR graph. Compare gateway off, existing always-subscribed relay, demand-driven idle, one recording client, two recording clients, slow/paused recorder, disconnect/rejoin and direct local recording. Use separate export names and a graph observer that lists without subscribing. Exercise real delivered sequences and ROS bag readback. Confirm one export per source sample regardless of reader count and zero idle native takes after detach. Bound queue depth and loan lifetimes under recorder backpressure; report which samples drop and where. Preserve the healthy 24/24 and fault observations as controls.
2. **Type dispatch and CLI parity.** Run the type/direction tests and verb cases above. Include a route omitted from the allowlist and incompatible reliability. Compare generic and typed cost in identical runs. Keep generic where cost is acceptable for the declared workload; document measured incremental cost and build/deployment burden before choosing. Validate acquisition/release, runtime type support lifetime, route disable and restart.
3. **Network boundary.** On the laptop, when available, use a peer network namespace/container with its own address, no Agnocast device/hook or application overlay, and DDS configured for that interface. Verify the UDP interface and byte counts, remote bag readback, remote ingress, peer loss and recovery. The present loopback-only Cyclone profile must be replaced for this test. Repeat on a real second host when available, including MTU/fragmentation for large clouds and disconnect/rejoin. Test host A native to host B native via two gateways separately from host A native to a stock remote recorder. One-way latency across machines needs measured clock uncertainty; use sequence/readback and round-trip timings otherwise.
4. **Startup choice.** Compare explicit, demand-driven manual startup with true middleware startup using identical routes, then run the ownership/failure cases above. Default to explicit startup for the pilot until automatic startup earns its operational complexity.
5. **Decision evidence.** For each workload keep per-process bridge and logger CPU, idle wakeups, native takes and loan release, serialization/materialization counts, DDS bytes, queue/peak memory, local median/p95, recorder receive latency, sequence gaps/duplicates, startup and recovery times. Include disk and receiver backlog separately. Alternate run order, keep the pressure trace and shared-host caveat. Use matched native Agnocast with upstream bridge, direct Cyclone DDS and Zenoh recorder baselines where available; label differing graphs and unavailable cells. Run sequentially under the laptop memory guard. Advance on verified delivery, isolated failure and measured overhead; the earlier 1.2× latency objective remains an engineering reference. Bazel follows the colcon deployment decision and layout work.

### Recorder lifecycle evidence

The first colcon experiment passed for the explicit typed PointCloud2 export.
A matched DDS publisher event activated and detached one native reader.
Across two paired runs the demand-driven route took 16 clouds and the eager
route took 24, with identical recorded sequences in every bag. The distinct
DDS export was discoverable before recording. Immediate detach was sufficient
for the paced attach/detach test. Next run the ungated producer during
discovery and throttle the recorder to measure loss, backpressure and local
loan lifetime. Direct local recording and the network boundary remain open.
[Lifecycle results](recorder-lifecycle-results/)

### Continuous recorder evidence

Three running controls and three recorder stalls exercised attach, recovery
and restart under uninterrupted 10 Hz source traffic. Each local run delivered
60/60 with the same peak of three observed live synchronizer input owners.
Suspending the recorder for 1.6 s consistently lost 14 forwarded clouds from
the best-effort bag. The restarted recorder always captured a contiguous
suffix. Qualify direct local recording next using this workload; compare
loss, native owner retention and CPU before choosing the same-host logger
path. Preserve the DDS export for network-bound recording. Lossless export
would need its own bounded backlog/replay policy and overload tests.
[Sequence audits and timings](live-recorder-results/)

### Local recorder decision

Prefer the stock rosbag2 recorder with rmw_agnocast and the heap hook for
selected same-host PointCloud2 topics when the recorder can join the local
Agnocast installation. Use a directed DDS export for an ordinary DDS
recorder or a network peer. In the grouped colcon trials, direct recording
at writer and reader depth 32 recovered all 16 clouds produced during a
1.6 s recorder suspension. The best-effort DDS export recovered two at the
same configured depth. Direct recording requires native new-message
notifications for event executors and a valid RMW reception timestamp;
both are now implemented on the default threaded native path.

The next group can qualify generic message fallback in each direction,
missing type support, and stock CLI behavior on an exported topic together.
Independent network delivery and middleware-managed startup remain open.
[Grouped comparison](local-recorder-results/summary.md)

### Runtime type fallback decision

A manually started route can now resolve the input publisher's installed ROS
type at startup. Both directions support serialized rclcpp endpoints on this
laptop, including a native output that an independent typed subscriber reads.
The RMW handles serialized publish according to the selected native layout.
No per-message-type gateway build is required for the tested PoseArray and
Image routes. Keep the CPU/host-cloud PointCloud2 specialization available as
the automatic cloud route. Use serialized forwarding for automatically
discovered Path; the existing explicitly named path adapter remains available
for compatibility. These choices are scoped to the measured representations.

For example, with the gateway's normal rmw_agnocast and heap-hook environment:

```bash
route_bridge a2r auto /local/poses /dds/poses
AGNOCAST_RMW_BUFFER_CDR=1 route_bridge r2a auto /dds/image /local/image sensor
```

The first command exports poses; ordinary Cyclone subscribers can consume
the DDS output. The second imports a sensor-QoS stream into the native CDR
layout; configure its native consumer for the same layout. Fully qualified
type names allow configuring a route before discovery. Auto pins one observed
type name; a later schema change requires restarting the route. Type-hash
validation, automatic QoS selection, physical inter-host delivery and
middleware-owned startup are still open.
[Type fallback comparison](type-fallback-results/summary.md)

### Network-isolated peer result

The explicit gateway exported three-LiDAR clouds to stock Cyclone rosbag2
across a Docker bridge and imported a remote PoseArray into native storage.
A late recorder captured every forwarded cloud in both healthy runs. Both
brief link outages lost the same 16 groups and recovered at the first group
after reattachment while local delivery continued. Use this recipe for a
plain ROS logger with an explicit export. A physical second host, discovery
lease expiry, and middleware-owned startup remain separate qualifications.
[Prepared peer and operator commands](network-peer/README.md) · [Results](network-peer/results/summary.md)

### Recovery after loss of a matched logger

The demand-driven export usually detached its local reader after the last
remote reader expired. One repeat continued taking clouds after forwarding stopped. An independently restarted gateway or recorder can match the
surviving endpoint and resume the stream. Docker network reattachment after
lease expiry exposed a separate recorder recovery case. For now, verify that
bag rows advance after reconnection and restart the recorder into a fresh bag
segment if necessary. Preserve the healthy gateway and local graph.
Before choosing middleware-owned recovery, compare this interface replacement
with a plain Cyclone publisher under the same profile and a link outage that
preserves both interfaces. [Recovery evidence](network-peer/results/recovery.md)

### Demand-driven gateway owns its match status through the matched callback

The Lyrical stock Cyclone endpoint reproduced the missed zero-match notification
when a subscription-count query ran before event dispatch. The demand-driven
gateway now attaches and detaches through the callback and skips the competing
per-cloud count query. Keep the existing count check for always-attached
routes. An interface-preserving outage and a Docker network replacement
have distinct recovery behavior. The latter can still require a new recorder
segment. [Stock control and integrated regression](network-peer/results/match-status.md)
