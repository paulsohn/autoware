# Findings for the Autoware Foundation

This document collects source and package findings that are useful to Autoware
independently of this Bazel experiment. Add an entry only after isolating the
behavior from the Bazel rule that exposed it.

For each finding, record:

- repository, package, and source revision;
- compiler, ROS distribution, and platform;
- minimal build or test reproducer;
- symptom, confirmed cause, and compatibility of the proposed fix;
- validation against the current Autoware build where practical;
- suggested issue, documentation change, test, or patch.

Each finding has an upstream-association task. Check it only when a linked
upstream issue or PR tracks that specific finding; related feature or transition
links alone leave it unchecked. A checked item means associated, not submitted
by us, merged, or locally adopted. State whether an unchecked item has been
searched or still needs review.

## Lyrical alignment

Reviewed on 2026-11-15. AWF is independently developing Lyrical support in
[autoware#7404](https://github.com/autowarefoundation/autoware/issues/7404),
with core work tracked in
[autoware_core#1538](https://github.com/autowarefoundation/autoware_core/issues/1538).
The core tracker requires preserving Humble and Jazzy builds. Its Ubuntu
26.04/GCC work complements this experiment's Ubuntu 24.04/Clang/RCR validation;
neither environment's results substitute for the other's.

Continue compatibility work when a local target exposes a failure. Inspect
the tracker, linked issues, and relevant PRs before settling the source fix;
align semantics and compatibility guards where supported by local evidence.
Record the local commit, upstream link, reviewed date, exact overlap, and
remaining checks. Recheck open PRs before updating or submitting a patch.
An association below is not a claim that the upstream patch was applied or
that the local change has been submitted upstream.

| Local work | Upstream association, reviewed 2026-11-15 | Alignment action |
| --- | --- | --- |
| Wrapper generic pub/sub test setup, `1d0cd537` | [core#1442](https://github.com/autowarefoundation/autoware_core/pull/1442) is the merged feature introducing generic pub/sub; [core#1415](https://github.com/autowarefoundation/autoware_core/pull/1415) is the merged context-selection API. Neither is the test fix. | Offer a focused test patch against current upstream sources; search found no matching executor-test fix. See the finding below. |
| Explicit Agnocast context initialization in that test patch | Open [agnocast#1517](https://github.com/autowarefoundation/agnocast/pull/1517) introduces lazy context initialization for nodes/executors in ROS processes. | Related lifecycle work. Revisit the test's explicit initialization if that PR is adopted: its contract rejects explicit init after lazy init. Do not combine the two approaches without lifecycle tests. |
| Agnocast rclcpp override compatibility, `c9051afc` | [callback_isolated_executor#64](https://github.com/autowarefoundation/callback_isolated_executor/issues/64) discusses the same shared-pointer override API transition; it is an issue, not a merged PR. | The issue proposes a different version threshold from our rclcpp-major-32 guard. Compare actual supported headers and validate older distributions before upstreaming; no equivalent Agnocast Lyrical PR was found in the reviewed search. |
| Wrapper message-filter header/template compatibility, `ac766b72` | Draft [core#1257](https://github.com/autowarefoundation/autoware_core/pull/1257) changes the same header's fallback API surface. | Related work, not the Lyrical include/template fix. Preserve its intended API parity if reconciling the patches; no matching Lyrical subscriber fix was found. |
| Wrapper message-filter subscription QoS conversion, `b5d19a6c` | Upstream `main` still forwards the raw profile as of 2026-11-15; draft [core#1257](https://github.com/autowarefoundation/autoware_core/pull/1257) does not fix this path. | Offer a focused Lyrical compatibility patch with the existing include/template adaptation and a subscription-path regression. Reuse upstream's profile-preserving `detail::to_qos` helper. See the finding below. |
| Direct Bazel compile/shared dependency declarations | Draft [core#1540](https://github.com/autowarefoundation/autoware_core/pull/1540) replaces the removed CMake helper with explicit linking, including wrapper CMake helpers. | Same dependency-boundary concern, different build implementation. Compare exported/public dependencies when a target needs them; no CMake patch is required merely to build the Bazel target. |

The tracker also links the toolchain issue
[universe#13590](https://github.com/autowarefoundation/autoware_universe/issues/13590).
Core [PR #1539](https://github.com/autowarefoundation/autoware_core/pull/1539)
has merged the expected-header transition. These are references for targets
that later expose those dependencies, not instructions to patch unused areas
or satisfy Bazel dependencies with distribution packages.

Search scope: the tracker and its comments, the core transition issue,
repository-scoped PR searches for Lyrical, wrapper/message-filter work, and
generic executor/test fixes, plus the PRs and issue linked above. “Not found”
describes that review, not proof that no other upstream work exists.

## Findings

### Lyrical message-filter subscriptions need explicit QoS conversion

- [ ] Associate this finding with a matching upstream issue or PR. Reviewed
  2026-11-15: no matching fix found in repository-scoped PR searches for
  `Lyrical message_filters`, `Lyrical subscriber`, `message_filters QoS`, and
  `message_filters subscribe`. The Lyrical trackers are related, not a specific
  association for this finding. No issue or PR was submitted by this experiment.
- Repository/package: `autoware_core`, `autoware_agnocast_wrapper`,
  `include/autoware/agnocast_wrapper/message_filters.hpp`.
- Local source fix and regression: `b5d19a6c`; the prerequisite include/template
  adaptation is `ac766b72`. The source fix is kept separate from Bazel rules.
- Current upstream status: inspected
  [main's subscription forwarding](https://github.com/autowarefoundation/autoware_core/blob/main/common/autoware_agnocast_wrapper/include/autoware/agnocast_wrapper/message_filters.hpp#L77)
  on 2026-11-15. File blob SHA:
  `a1dcb4bcd039c4a4094e8c80149cdbe861300cb3`. It still passes `qos` directly
  and uses the pre-Lyrical two-parameter Subscriber template. This finding is
  not already fixed on the inspected default branch.
- Related PR: inspected draft [core#1257](https://github.com/autowarefoundation/autoware_core/pull/1257),
  head `7e008dfec3a290bbe251fd04566a2de5f00e1bc9`. Its actual patch curates the
  non-Agnocast-build API; it does not change the enabled-build subscription path
  or add Lyrical QoS conversion. Preserve its API intent without treating it
  as this fix. [core#1538](https://github.com/autowarefoundation/autoware_core/issues/1538)
  and [autoware#7404](https://github.com/autowarefoundation/autoware/issues/7404)
  remain the overall Lyrical transition trackers.
- Reproducer: after the include/template compatibility fix `ac766b72`, instantiate
  the public method rather than only default-constructing its class:

  ```cpp
  using Subscriber = autoware::agnocast_wrapper::message_filters::Subscriber<
    geometry_msgs::msg::PoseStamped>;
  void reproduce(autoware::agnocast_wrapper::Node * node) {
    Subscriber subscriber;
    subscriber.subscribe(node, "/example", rmw_qos_profile_sensor_data);
  }
  ```

- Confirmed symptom on Ubuntu 24.04/LLVM 21.1.8/Lyrical rclcpp 32: compile error
  at the forwarding call, with no conversion from `rmw_qos_profile_t` to
  `const rclcpp::QoS &`. The existing test only default-constructed the class,
  leaving the template method body uninstantiated. A default-construction test
  passing does not establish that the public subscription path compiles.
- Fix: on rclcpp 32+, forward `detail::to_qos(qos)` to the ROS subscriber.
  The helper already exists in upstream
  [node.hpp](https://github.com/autowarefoundation/autoware_core/blob/main/common/autoware_agnocast_wrapper/include/autoware/agnocast_wrapper/node.hpp#L55)
  (inspected blob `42183b25f3644034637b11f94b61334f923dc7d3`). It preserves
  the profile's history/depth instead of normalizing them through
  QoSInitialization::from_rmw. Retain the original pre-32 call and leave
  Agnocast's raw-profile call unchanged. This is wrapper API compatibility,
  not an Agnocast transport modification.
- Regression: `SubscriberForwardsRmwQosOnConstructionAndResubscription` creates
  real subscriptions through the constructor and subscribe method, checks
  topic/history/depth/reliability/durability, changes depth and topic, and
  verifies unsubscribe releases the endpoint. The enabled run uses the real
  heap hook and kernel module. Both backend selections pass the complete
  37-case wrapper suite without skips on Cyclone DDS.
- Validation commands:

  ```bash
  env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
    bazel test --config=no-agnocast --nocache_test_results \
    @autoware_agnocast_wrapper//...
  env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
    bazel test --strategy=TestRunner=local \
    --run_under=//bazel/agnocast:with_heaphook --nocache_test_results \
    @autoware_agnocast_wrapper//...
  ```

- Before submission: validate the ordinary CMake/colcon build, older supported
  distributions, and AWF's Ubuntu 26.04/GCC toolchain. The pre-32 call is
  preserved but not tested here. Our runtime fallback is not a separate build
  with USE_AGNOCAST_ENABLED absent. Combine with the include/template fixes
  when proposing a complete upstream Lyrical wrapper compatibility change.

### Wrapper generic pub/sub tests need backend-aware executor setup

- [ ] Associate this finding with a matching upstream issue or PR. Reviewed
  2026-11-15: no matching repair found; related PRs are listed below.

- Repository/package/source: `autoware_core`, `autoware_agnocast_wrapper`,
  imported snapshot `384bcd47e6d2fd21e5954a915153aa5303b34a53` with the local
  Lyrical compatibility changes. Source-only patch: `1d0cd537` (changes
  `test/main.cpp` and `test/cases/generic_pubsub.cpp`).
- Environment: Ubuntu 24.04 x86-64, Clang 21.1.8, Lyrical RCR
  `lyrical.2026-10-05.rcr.3`, Agnocast 2.4.1 heap hook, and the execution host's
  existing Agnocast kernel device. The setup defect follows executor/context
  APIs; it is not specific to Bazel or established as a Lyrical-only defect.
- Reproducer: compile the existing wrapper C++ suite with
  `USE_AGNOCAST_ENABLED`, run with `ENABLE_AGNOCAST=1`, preload the real
  `libagnocast_heaphook.so`, and provide a compatible kernel module. At parent
  commit `a2714788`, the complete suite has four generic pub/sub failures.
  Omitting preload instead skips 35 cases and masks the defect.
- Symptoms/causes: Method 1's two message round trips time out because the
  tests spin RCLCPP rather than Agnocast callbacks. Method 2's two round trips
  throw by requesting `get_rclcpp_node()` from an Agnocast-owned node. An
  Agnocast-only executor also needs its context/signal handler initialized.
- Proposed patch: select `SingleThreadedAgnocastExecutor` for Method 1 and
  `AgnocastOnlySingleThreadedExecutor` for Method 2 when enabled; use RCLCPP in
  fallback. Pass the common node-base interface, run the selected `spin()` on
  a worker, then cancel and join before teardown. Initialize and shut down
  both contexts for this mixed test binary. Keep all payload assertions and
  test cases unchanged. No production behavior changes are needed.
- Upstream association: merged [core#1442](https://github.com/autowarefoundation/autoware_core/pull/1442)
  provides feature context and merged [core#1415](https://github.com/autowarefoundation/autoware_core/pull/1415)
  explains context selection. Open [agnocast#1517](https://github.com/autowarefoundation/agnocast/pull/1517)
  changes future context ownership; reassess explicit initialization against
  its final implementation. No matching executor-test repair was found in the
  searches above. No upstream submission has been made by this experiment.
- Reproduction commands, from an unsourced caller environment:

  ```bash
  env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
    bazel test --strategy=TestRunner=local \
    --run_under=//bazel/agnocast:with_heaphook \
    --test_env=ENABLE_AGNOCAST=1 --nocache_test_results \
    @autoware_agnocast_wrapper//...
  env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
    bazel test --config=no-agnocast --nocache_test_results \
    @autoware_agnocast_wrapper//...
  ```

  Repeat both commands with `--config=fastdds`. All four configurations passed
  the complete 36-case C++ suite with zero skips after the fix. Kernel tests
  use host PIDs because sandbox PID reuse collides with Agnocast's PID-named
  shared-memory objects; that runner setting is local validation plumbing,
  separate from the source-only patch.
- Before upstream submission: rerun the ordinary CMake/colcon wrapper suite
  against current AWF sources and supported Agnocast versions, including
  enabled/disabled builds on Humble and Jazzy as well as Lyrical. Those builds
  have not been validated here. Reconcile any merged context-lifecycle changes
  first. The Python launch suite was outside this C++ runtime gate.

### `autoware_node` omits its public RCUtils dependency

- [ ] Associate this finding with a matching upstream issue or PR. Targeted
  upstream review is pending.

- Repository/package/revision: `autoware_core`, `autoware_node`, source at
  repository revision `a1dd4c5d`.
- Environment: Lyrical RCR, hermetic Clang 21.1.8, x86-64 Linux.
- Reproducer: compare `package.xml` with
  `include/autoware/node/visibility_control.hpp`, then compile the library with
  direct dependencies as exercised by `bazel test @autoware_node//...`.
- Symptom: the public visibility header directly includes
  `rcutils/visibility_control_macros.h`, but the manifest declares only
  `rclcpp`. The Bazel library needs the RCUtils compile provider directly;
  RCLCPP's transitive dependency can otherwise hide the omission.
- Suggested fix: add `rcutils` as a `<depend>` entry in `package.xml`. This
  corrects metadata for a type already present in the public header. Validate
  with the normal colcon package build and dependency lints before proposing
  the change upstream.

### `autoware_utils_geometry` omits public message dependencies

- [ ] Associate this finding with a matching upstream issue or PR. Targeted
  upstream review is pending.

- Repository/package/revision: `autoware_utils`,
  `autoware_utils_geometry`, source at repository revision `7922787a`.
- Environment: Lyrical RCR, hermetic Clang 21.1.8, x86-64 Linux.
- Reproducer: compare `package.xml` with the output of `rg '^#include'` over
  `src/core/autoware_utils/autoware_utils_geometry/include`. Then compile the
  package with direct-dependency checking, as exercised by
  `bazel test @autoware_utils_geometry//...`.
- Symptom: public headers directly include `geometry_msgs`,
  `autoware_perception_msgs`, and `autoware_planning_msgs`, but the manifest
  declares none of those packages. The Bazel library needs all three as direct
  dependencies; an underlay can otherwise hide the omission from CMake.
- Suggested fix: add the three packages as `<depend>` entries in `package.xml`.
  The message types are already part of the public API, so this changes package
  metadata rather than source compatibility. Validate with the normal colcon
  package build and dependency lints before proposing the patch upstream.

### GCC-only warning pragma is active under Clang

- [ ] Associate this finding with a matching upstream issue or PR. Targeted
  upstream review is pending.

- Repository/package/revision: `autoware_utils`,
  `autoware_utils_geometry`, source at repository revision `7922787a`.
- Environment: Lyrical RCR, hermetic Clang 21.1.8, x86-64 Linux.
- Reproducer: run `bazel test @autoware_utils_geometry//...` and inspect the
  compilation of any source including `boost_geometry.hpp`.
- Symptom: Clang reports `unknown warning group '-Wmaybe-uninitialized'` at the
  header's diagnostic pragma.
- Cause: Clang defines `__GNUC__` for compatibility, so the existing
  `#ifdef __GNUC__` guard also enables a GCC-only warning name under Clang.
- Suggested fix: guard both diagnostic push/pop blocks with
  `#if defined(__GNUC__) && !defined(__clang__)`. This preserves the intended
  suppression for GCC while avoiding an unknown pragma under Clang. Validate
  with both compilers and the normal package tests before proposing the patch.

### Agnocast discovery warning paths and ioctl header self-sufficiency

- [ ] Associate the logger API correction with a matching upstream PR.
- [ ] Associate the missing `<cerrno>` includes with a matching upstream PR.

Verified 2026-11-15 with Lyrical RCR rclpy 10.0.11, Clang 21.1.8/libc++,
Ubuntu 24.04 x86_64. These are source compatibility findings exposed during
Phase 4.2; they do not change bridge selection or delivery semantics.

The complete discovery-agent suite initially collected 147 cases and failed
`test_dispatch_service_reports_a_failure_through_a_real_logger` with
`AttributeError: 'RcutilsLogger' object has no attribute 'warn'`. The real
rclpy logger exposes `warning()`. Use that method in agent.py,
bridge_decider.py and type_registry.py, and update the existing mock
expectations. All 147 cases then pass. The same method also works with the
registry's standard Python logger; no compatibility shim is necessary.

Upstream main's
[bridge_decider.py](https://github.com/autowarefoundation/agnocast/blob/main/src/ros2agnocast_discovery_agent/ros2agnocast_discovery_agent/bridge_decider.py),
blob `8f6d4d81c833a42d84c98724158b4f933e993bf1`, still uses `warn()` in its
escalation and both dispatch-failure paths. Merged
[PR #1574](https://github.com/autowarefoundation/agnocast/pull/1574) fixes
rclpy-incompatible positional formatting arguments, but retains `warn()`:
related work, not a matching fix. Searches for Lyrical, warning/logger and
warn/discovery PRs did not find a matching API correction.

The ioctl wrapper separately fails compilation on undeclared `errno` and
`ENOENT`. Its node_info.cpp, topic_info.cpp and topic_list.cpp use them without
including `<cerrno>`; add the direct include in those translation units.
Other wrapper sources already include it. Upstream main's
[node_info.cpp](https://github.com/autowarefoundation/agnocast/blob/main/src/agnocast_ioctl_wrapper/src/node_info.cpp)
blob `bd80a485cf745c8db8926f8f09e334710a68af8b` and
[topic_info.cpp](https://github.com/autowarefoundation/agnocast/blob/main/src/agnocast_ioctl_wrapper/src/topic_info.cpp)
blob `556f9188931f0363dbd0d14d0e56f45a4f4defce` retain the omission. Searching
cerrno PRs found unrelated configurator/graph changes, not this include repair.

Reproduce with the Phase 4.2 module definitions and clean environment commands
in JOURNAL.md: `bazel test @ros2agnocast_discovery_agent//:tests
@agnocast_ioctl_wrapper//... --test_output=errors`. All ioctl targets build
and all agent cases pass after the fixes. The runtime gate also passes 11
cases, including actual automatic agent startup and a typed gossip endpoint.
No Humble/Jazzy, GCC, CMake/colcon, or Fast DDS validation was performed for
these corrections. AWF's broader Lyrical trackers remain
[autoware#7404](https://github.com/autowarefoundation/autoware/issues/7404) and
[autoware_core#1538](https://github.com/autowarefoundation/autoware_core/issues/1538);
neither tracker is test evidence for these packages.
