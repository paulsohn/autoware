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

### Nearest-lanelet tests assume unspecified tie ordering

Local source test fix: `ffc328da`, separate from the Bazel definitions.

- [ ] Associate with a matching upstream fix. Reviewed 2026-11-16: repository
  PR searches for `lanelet2_utils nearest distance`, `nn_search Lyrical` and
  `find_nearest tie` found no matching test fix. Actual
  [core#839](https://github.com/autowarefoundation/autoware_core/pull/839)
  introduces the nearest-search API and hardcoded-ID tests; it is related
  feature history, not a fix. Current main still has those assertions, test
  blob `b67ceb31dbccb7334d85bbf93b0b5d4c8c6e57a2`, implementation blob
  `58bed08550cbe9eeed6d9c52a5b0bf5b5f41e508`.
- Scope: autoware_core/common/autoware_lanelet2_utils/test/nn_search.cpp.
  Reproduce with the full utility command in the Phase 5.2.4 JOURNAL entry,
  Ubuntu 24.04, LLVM 21.1.8/libc++, C++23, Lanelet2 1.2.1-8.rcr.2 and the current
  local extension 1.3.0. Four ID assertions fail while all other cases pass.
- Confirmed diagnosis using temporary distance logging (removed from final
  code): fixture 001 P1 reference 2267 returns 2262; P2 reference 2262 returns 2281;
  fixture 002 P0 reference 2270 returns 2305; P2 reference 2268 returns 2282. Every
  returned/reference pair has distance 0: both polygons contain the query point.
  The production nth_element/sort compares only distance and specifies no
  tie-break by lanelet ID. Those results satisfy the current API.
- Fix the nine nearest-search fixture assertions to retain count 1 and verify
  both reported score and actual returned-polygon distance against the known
  reference lanelet. Positive-distance cases also retain exact distance checks.
  Do not introduce a production ID tie-breaker merely to match one STL's order.
  All 173 existing cases across all eleven suites then pass with no skips;
  production code is unchanged. This is portable test behavior, not a new
  Lyrical algorithm or an older-ROS compatibility claim. No upstream submission
  was made by this experiment.


### Ground filtering needs portable float constants and subscription QoS

Local source fix: `fd3c132e`, separate from the Bazel build definitions.

- [ ] Associate with matching upstream fixes. Reviewed 2026-11-16: searches for
  `ground_filter M_PI`, `ground_filter lyrical` and `ground_filter QoS` find no
  matching PR. Current main still has both paths: grid.hpp blob
  `8157550bf4ce838c677cc9fa93fcaefab746ac81` and ground_filter_node.cpp blob
  `ed442301962611e0d02c0423319f1fb8dfd1f243`. The Lyrical trackers are related,
  not specific associations for these two findings.
- Scope: autoware_core/perception/autoware_ground_filter, Ubuntu 24.04 with
  LLVM 21.1.8/C++23, glibc 2.34 ABI toolchain, Lyrical RCR/Cyclone DDS. Run the
  module command in the Phase 5.2.3 JOURNAL entry to reproduce.
- First compile failure: nonportable `M_PIf`, `M_PI_2f` and `M_PI_4f` macros
  are undeclared in grid.hpp. Derive float pi/half-pi/quarter-pi constants from
  existing autoware_utils_math::pi and replace those uses. Preserve the float
  precision and original arithmetic; no global compiler macro or C++20-only
  constant is needed. This keeps the source language requirement compatible
  with the existing C++17 project convention.
- Next compile failure: both indexed input message-filter subscribers pass
  rmw_qos_profile_t, but the current Subscriber API accepts rclcpp::QoS. For
  rclcpp major >=32 pass the SensorDataQoS object with the same queue depth;
  preserve the legacy profile on the older branch. This follows the wrapper's
  existing local compatibility boundary without making this component depend
  on that wrapper. The old branch remains unverified on older distributions.
- Verification: all 24 existing cases pass with no skips, including both exact
  and approximate indexed synchronization, grid/radial algorithms, and sanity
  rejection. No colcon, Humble/Jazzy or Ubuntu 26.04/GCC result is claimed.
  No upstream issue/PR has been submitted by this experiment.


### Test-helper relay test must shut down its ROS context

Local source fix: `5086091b`, separate from the Bazel module definitions.

- [ ] Associate with a matching upstream issue or PR. Reviewed 2026-11-16:
  repository-scoped search for `autoware_test_utils shutdown` finds
  [core#1181](https://github.com/autowarefoundation/autoware_core/pull/1181),
  but its actual fixture patch changes autoware_planning_test_manager, not
  this relay test. Related lifecycle work is not this specific fix.
- Current main's test_autoware_test_manager.cpp blob
  `9e0e00d199f6716d6c01afafc399e2cf4ace9718` still initializes ROS without
  shutdown. Scope: autoware_core/testing/autoware_test_utils, relay test only.
- Reproducer: the complete module command in the Phase 5.2.3 JOURNAL entry
  on Ubuntu 24.04, LLVM 21.1.8/C++23 and Lyrical RCR/Cyclone DDS. All 21
  assertions pass, then the process exits with SIGSEGV. GDB shows
  `DefaultContext` destruction -> `Context::shutdown` ->
  `rcl_logging_external_shutdown` -> spdlog logger registry access during
  static destruction. The relay test is the only suite member that calls init.
- Fix: give that test a GTest fixture with SetUp init and TearDown shutdown.
  This also handles assertion/exception exits instead of leaving the context
  initialized. After the change the complete 21-case suite passes and exits
  normally. No production helper API or logging-library patch is needed.
  The upstream planning fixture uses the same ownership pattern; its graph
  discovery changes are unrelated and are not applied here.
- This demonstrates an exit-order failure in the pinned environment, not proof
  of failure on every ROS distribution/link layout. No colcon or older-ROS
  validation and no upstream submission are claimed.

### Downsampling uses the native expected header

- [x] Associated with merged [core#1539](https://github.com/autowarefoundation/autoware_core/pull/1539).
  Reviewed its actual patch and current main on 2026-11-16. Main's downsampling
  header blob is `2b087d9c20ae47cb25c293eda1314adf26eefa3b` and already
  includes `tl/expected.hpp`; this is an existing upstream fix, not a new
  submission opportunity.
- Local source fix: `92256b96`, kept separate from Bazel build definitions.
- Package: `autoware_core/sensing/autoware_downsample_filters`. The source
  snapshot includes `tl_expected/expected.hpp`, but RCR's native tl_expected
  provider exports `tl/expected.hpp`. On Ubuntu 24.04 with pinned LLVM 21.1.8,
  C++23 and Lyrical RCR, the full module build fails with that legacy header
  not found before executing any tests.
- Change the include to `tl/expected.hpp` and replace the manifest's ROS
  `tl_expected` dependency with `libexpected-dev`, matching that package's
  upstream transition. Bazel supplies the native library through
  `@rosdistro//cc:tl_expected`; the manifest change does not authorize a host
  apt dependency for Bazel.
- Reproducer/verification: run the downsampling module command in the Phase
  5.2.3 JOURNAL entry. After the header change, all twelve existing tests pass
  without skips. The PR also changes three string-literal error returns to
  `tl::make_unexpected`; the pinned C++23 toolchain compiles and tests the
  original returns, so that extra change is not needed by this target. Other
  packages touched by the PR are outside this package's fix. No colcon,
  Humble/Jazzy or Ubuntu 26.04/GCC validation is claimed.


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

Local fix: `9b277d8a`.

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

### CIE configurator adds nodes from different contexts to one executor

Local fix: `8c1c2afc`.

- [ ] Associate this Lyrical compatibility fix with a matching upstream PR.

Verified 2026-11-16: the existing multi-domain CIE launch test fails on pinned
rclcpp 32.0.3 with `add_node() called with a node with a different context from
this executor`. The prerun process exits 1 before collecting domain 0/1
announcements. Single-domain launches pass, so they do not cover this path.
`AnnouncementSources` correctly creates a context for each extra ROS domain;
both configurator entry points incorrectly add all those nodes to the default
context's executor. Pinned rclcpp's Executor::add_node explicitly rejects this.

Use one shared `spin_domain_nodes` helper from the normal configurator,
deprecated prerun CLI path, and standalone prerun entry point. Each extra
context gets its own executor. Drive their spin_some calls serially on one
thread, with a 1 ms idle interval, because callbacks and reapply handlers share
state under a single-executor assumption. Single-domain execution retains the
ordinary blocking spin. Creating parallel executor threads would require a
separate concurrency review; that is unnecessary for this configuration tool.

The complete configurator C++/Rust suite passes (137 C++ cases plus seven Rust
cases), and all seven existing functional launch scenarios pass 38 cases with
no skips. These include receiving announcements in both domains and applying,
reapplying, restarting and resolving wildcard/precedence configurations.
Reproduce with the Phase 4.3–4.4 Cyclone DDS commands in JOURNAL.md.

Upstream main's
[prerun_node_main.cpp](https://github.com/autowarefoundation/agnocast/blob/main/src/agnocast_cie_thread_configurator/src/prerun_node_main.cpp),
blob `37564990adcaeeeeab85fe64455e53736d4b3967`, still uses a single executor
for every domain node. Its newer init/shutdown exception handling is separate
from this defect. Searches for Lyrical and context/configurator PRs found
related scheduling, domain-ID parsing and capability work, but no matching
executor-context repair. Existing AWF Lyrical trackers are related context,
not evidence that this path has been fixed or tested upstream. GCC,
Humble/Jazzy, CMake/colcon and Fast DDS were not checked for this patch.

### Heap-hook version lookup misses ament overlays without preload

Local fix: `54410051`.

- [ ] Associate the overlay-prefix repair with a matching upstream PR.

Verified 2026-11-16: the built CLI reports kernel, library and CLI version
2.4.1, but reports the heap hook unavailable even though the selected ament
prefix contains lib/libagnocast_heaphook.so. With LD_PRELOAD unset, the command
only searches /opt/ros/$ROS_DISTRO/lib. That misses ordinary development
prefixes as well as Bazel's composed runtime.

Reuse the existing `_get_lib_path` helper to look under the selected
agnocastlib package prefix before the existing system-install fallback.
Preserve LD_PRELOAD priority and the warning that an inspected library is not
preloaded. No global allocator injection or Bazel-specific path is needed.
The actual command then reports 2.4.1 for all four components. Two regressions
cover overlay lookup and preload priority with a fake CDLL, while the complete
CLI suite passes all 95 cases. The product CLI behavior test also reaches and
passes these version assertions against the actual shared libraries.

Upstream main's
[agnocast_version.py](https://github.com/autowarefoundation/agnocast/blob/main/src/ros2agnocast/ros2agnocast/verb/agnocast_version.py),
blob `3b7e0bbdd242233a8dfe7d69c98011c93ba8a32c`, retains the /opt/ros-only
fallback. [PR #1224](https://github.com/autowarefoundation/agnocast/pull/1224)
introduced the version command; it is related history, not this prefix repair.
Searches for version/ament and heaphook/LD_PRELOAD did not identify a matching
fix. This is install-layout portability, not a newly introduced Lyrical API
change. Older distributions and CMake/colcon execution were not checked.


### Bridge-manager handoff retains the old abstract socket during DDS teardown

Local correction: `26f1d0ae`.

- [x] Associate the observed failure with [issue #1501](https://github.com/autowarefoundation/agnocast/issues/1501).
- [x] Align the source correction with [draft PR #1519](https://github.com/autowarefoundation/agnocast/pull/1519).

Observed 2026-11-16 in the complete nine-arrangement Fast DDS service suite:
a replacement BridgeManager reports `bridge UDS bind() failed: Address already
in use`; the synchronous Agnocast-client/ROS-server arrangement then times out
with no requests delivered. The kernel clears its manager role before the old
manager releases its abstract socket. DDS teardown makes that window visible.
This is a runtime lifecycle race, not a newly introduced Lyrical API change.

PR #1519 at head `be1c2b1dae9a2d5105026871f874ddb84d2305aa` is a matching
fix, reviewed as an open draft, not merged. Apply its four-file scope: close
the listener after the main event loop stops and before worker/DDS teardown;
retry only EADDRINUSE with a bounded 50 × 100 ms default budget; retain other
error paths and a zero-retry duplicate-bind test. Its new unit case holds and
releases an address to exercise the retry. No scenario is skipped or given
weaker assertions. The full Fast DDS library suite reports 673 cases: 668 pass
and five retain existing unsupported-behavior skips. The new retry test passes;
the previously failing service target passes all nine arrangements and 45
cases with its source unchanged. Product bridge and CLI behavior also pass.
The complete Cyclone 39-target regression and enabled-wrapper suite pass.
All ten Cyclone product behavior targets and both full Fast DDS matrices also
pass against the correction. JOURNAL.md records exact commands and scope.
The upstream PR remains a draft; local test evidence does not imply merger.

Inspected main still has the original single bind and late close:
[UDS helper](https://github.com/autowarefoundation/agnocast/blob/main/src/agnocastlib/include/agnocast/bridge/agnocast_bridge_uds.hpp)
blob `88c136e59eb933dd896f98b33369fe7e3ba30404`, and
[manager](https://github.com/autowarefoundation/agnocast/blob/main/src/agnocastlib/src/bridge/agnocast_bridge_manager.cpp)
blob `899689b7b08b0091aa18c148dea3c96ad2cbb5cb`.
[PR #1507](https://github.com/autowarefoundation/agnocast/pull/1507) is related:
its eventfd wakeup is already present locally, but does not release the listener
before slow teardown. Upstream PR test claims are not local validation.

## EKF launch test waits for discovery and first output

Local source correction: `5515a02e`.

Checked 2026-11-16. This is middleware/test readiness, not a Lyrical API fix.

- [x] Reproduce the original launch test failure with Cyclone DDS plus Agnocast:
  activation succeeds but `test_node_link` sees no odometry after a one-shot
  initial pose and a 200 ms observation window; process shutdown succeeds.
- [x] Preserve both assertions and verify the corrected test with RCLCPP and
  Agnocast. Subscribe to odometry first, wait for both topic endpoints before
  publishing the volatile initial pose, and use monotonic ten-second maximum
  deadlines that finish when ready. Observed tests complete in 1.4 and 3.3
  seconds, not ten-second sleeps. All 49 existing C++ cases also pass.
- [x] Inspect the current upstream
  [launch test](https://github.com/autowarefoundation/autoware_core/blob/main/localization/autoware_ekf_localizer/test/utils/test_ekf_localizer_launch.py),
  blob `1db0863636b041e18c653c3e049eeb4fa042ff89`: it still publishes before
  endpoint matching and waits only 0.2 seconds. The local pre-fix blob matches.
- [ ] Associate a matching upstream fix: none found in searches for the launch
  test, EKF discovery and EKF/Agnocast. Related
  [PR #1190](https://github.com/autowarefoundation/autoware_core/pull/1190)
  applies the Agnocast node wrapper and conditionally omits launch tests when
  enabled; its actual patch does not synchronize this launch scenario.
  [Core Lyrical tracker #1538](https://github.com/autowarefoundation/autoware_core/issues/1538)
  and [Autoware tracker #7404](https://github.com/autowarefoundation/autoware/issues/7404)
  track build/transition work, not a matching readiness correction.
- [ ] Submit/associate an upstream PR after review; none is opened by this task.

Scope: only test synchronization in test_ekf_localizer_launch.py; no production
node, timeout in its launch system, middleware setting or test assertion is
removed. The timeout bounds discovery/output failure rather than asserting
real-time latency. Local evidence uses Ubuntu 24.04/Clang/RCR Lyrical; it does
not establish Humble/Jazzy, GCC/Ubuntu 26.04 or hardware behavior. JOURNAL.md
contains the exact two launch commands and full-module gate.

## Component-interface member callbacks with Clang/libc++

- [ ] Associate with a matching upstream PR. Searched 2026-11-17 for
  component_interface_utils/libc++ PRs; no match found. The related Lyrical
  tracker [core#1538](https://github.com/autowarefoundation/autoware_core/issues/1538)
  and target-export discussion/PR #1540 concern broader/GCC/CMake compatibility,
  rather than this demonstrated member-pointer trait failure.

Local source fix: `5b15d677`, decay member-pointer arguments before binding. Package:
common/autoware_component_interface_utils, rclcpp.hpp. Upstream main checked
2026-11-17, file blob ae900b8fb2895f8da558049c836587a5cf85edbc; all six
member callback adapters still use std::bind(callback, instance, ...).
The tracker describes Ubuntu 26.04/GCC 15, Boost 1.90 and fmt 11. This finding
is demonstrated with Ubuntu 24.04, Clang 21.1.8/libc++, C++23 and Lyrical RCR;
it does not establish a failure in AWF's GCC environment.

Reproduce with the complete test_component_interface_utils suite, specifically
node_adaptor_create_subscription_member_callbacks. Both shared-pointer and
const-reference callbacks fail to compile in RCLCPP function_traits.hpp:
reference to overloaded FunctionT::operator() cannot be resolved. The named
rvalue-reference member pointer becomes an lvalue when passed to std::bind;
libc++ records a member-pointer reference in its binder template arguments,
which does not match RCLCPP's member-pointer binder trait specialization.

Move the member-function pointer into std::bind at all six member overloads,
including legacy init_sub/init_srv and returning subscription/service forms.
Member pointers are values, so this retains invocation/ownership semantics and
uses the already included utility header. Do not change callback signatures or
omit member-callback tests. The full 25-case utility suite passes after this
correction and declaring its proper interface runtime closure; the latter is
Bazel packaging, separate from the compile failure. The grouped command in the
pose-initialization JOURNAL entries reproduces the full suite. Motion's 118 and
pose initialization's 23 original C++ cases also pass with the corrected public
header. Both actual pose-initializer executables and all eleven expanded bridge
regression cases pass too. Humble/Jazzy, GCC/libstdc++ and upstream CMake builds
were not rerun.

## Trajectory headers and floating-point checks with Lyrical/LLVM

Local source correction: `a6a8f7ae`.

Verified 2026-11-17 on Ubuntu 24.04 x86_64, Bazel 9.3.2, LLVM/libc++ 21.1.8
and RCR Lyrical. The complete trajectory gate builds all 19 original examples
and passes all 293 original cases with plotting enabled; route-handler's 158
active cases also pass. Exact commands and setup are in the Phase 5.2.5 journal.

- [x] Match the trajectory subset of merged
  [core#1539](https://github.com/autowarefoundation/autoware_core/pull/1539),
  merged 2026-11-13 as 0b817ca557ea7839d3f352c7b24b32ac0d55d829.
  Compared the actual patch: four installed headers switch from
  tl_expected/expected.hpp to tl/expected.hpp and package.xml switches to
  libexpected-dev. The former include fails with the current Bazel provider;
  use the existing pinned tl-expected 1.3.1 through rosdistro, without installing
  an apt package. Other packages changed by that PR are outside this task.
- [ ] Associate the TF header correction with a matching upstream PR. Searches
  found no matching trajectory correction. Upstream main still has the doubled
  tf2_geometry_msgs include in pose.cpp (blob
  030020859c7ef8e3bd4bfdde50d87fde15e3d994) and removed .h TF includes in
  add_offset.hpp (blob 448d851def01538b07a6edba347d304f621e43a5).
  Use tf2_geometry_msgs/tf2_geometry_msgs.hpp in pose.cpp and the .hpp forms of
  Quaternion, Vector3 and utils in add_offset.hpp and its existing test.
  Each old spelling produces a missing-header error with the selected Lyrical
  providers. This changes include spelling only; PR #1539 does not cover it.
- [ ] Associate the lateral-distance test correction with an upstream PR.
  Upstream main still uses EXPECT_EQ(result, check_distance), blob
  7a11adb1e4cecd065713e1d31b2e667859e90475; a search for the named test found
  no PR. ParabolicTrajectoryNotNearest compares a cross-product distance with
  an independently projected Euclidean distance: 0.15676701343069127 versus
  0.15676701343069119, three ULPs apart. EXPECT_DOUBLE_EQ uses GTest's four-ULP
  comparison rather than requiring identical rounding. No production formula,
  expected geometric result or test group is removed.

The related Lyrical trackers are
[autoware#7404](https://github.com/autowarefoundation/autoware/issues/7404) and
[core#1538](https://github.com/autowarefoundation/autoware_core/issues/1538).
Their Ubuntu 26.04/GCC work is not local test evidence. The implementation also
uses deleted default constructors on aggregates in reference_path.cpp, valid
under its upstream C++17 contract but not under the workspace's C++23 default.
A package-local C++17 compilation setting resolves that issue without a source
patch. Humble/Jazzy, GCC and colcon/ament/CMake were not run locally.


## Planning-factor test must dispatch the selected backend

Local source correction: `2c3383fd`.

- [ ] Associate with a matching upstream PR. Checked 2026-11-17: no matching
  planning-factor executor PR was found. Main test_planning_factor_interface.cpp
  (blob 578b369eefb5ea0f3441289d30c43f1d793c132c) spins a wrapper Node through
  rclcpp::spin_some, regardless of the runtime backend.

With ENABLE_AGNOCAST=1, a built/preloaded heap hook and matching 2.4.1 kernel
module/device, six cases pass but PublishStampsAndForwardsFactors times out
without receiving its message. RCLCPP spin_some does not dispatch Agnocast's
callback queue. Select SingleThreadedAgnocastExecutor when use_agnocast() is true,
otherwise the ordinary RCLCPP executor. Run spin asynchronously, use an atomic
receive flag, cancel/join before assertions and endpoint destruction, and
propagate executor exceptions through the future. Keep the five-second receive
deadline and every payload assertion. Conditional compilation retains builds
without USE_AGNOCAST_ENABLED.

All seven original cases pass in both RCLCPP and enabled Agnocast configurations
after the correction. A heap hook declaration/launch is a separate runtime
prerequisite, not the fix for callback dispatch. No production behavior, timeout,
assertion or test group is removed. See the planning-factor journal for exact
commands; this is an executor-test correction rather than a Lyrical-only fix.

## Path-generator endpoint comparison after quaternion interpolation

Local source correction: `d9ae1378`.

- [ ] Associate with a matching upstream PR. Checked 2026-11-17: a search for
  connectPathToGoal found no PR; main test_goal_connection.cpp, blob
  731452d4d5b65044da6435bc09d536f469911cb8, still compares the whole interpolated
  starting point with the raw fixture using ASSERT_EQ.

Observed: UtilsTest.connectPathToGoal fails exact orientation equality after
repeated trajectory interpolation. Position and other fields match; the largest
quaternion-component difference is about 3.09e-13 (z:
-0.9705764902527797 versus -0.9705764902524712). Compare the four orientation
components with a 1e-12 absolute tolerance, then compare every remaining field
exactly. Keep the original goal-position/orientation assertions and all test
cases. No production interpolation or fixture changes are required.
All 38 original path-generator cases and the executable's generated-parameter/
readiness check pass with this correction on the same LLVM/Lyrical platform.
The package journal records the complete gate; this does not establish other
compiler/platform behavior.

## Mission-planner arrival test uses a removed TF header

Local source correction: `b806740a`.

- [ ] Associate with a matching upstream PR. Checked 2026-11-17: no matching
  arrival-checker/TF PR was found, and upstream main test_arrival_checker.cpp
  (blob 5fbf1d51f3fb914dab73550f598f43a83cb7c0e0) still includes
  tf2/LinearMath/Quaternion.h. Use Quaternion.hpp, as supplied by the selected
  Lyrical TF package. This is the same header-removal category as the trajectory
  finding above, not part of expected-header PR #1539.

Observed: the arrival test cannot compile with the old spelling. The one-line
correction preserves every assertion. All 40 original mission-planner cases
then pass across plugin geometry, arrival checks, reroute safety and node API
transactions; both actual executables pass their root behavior checks. The
module journal contains exact commands. Verified on Ubuntu 24.04, LLVM/libc++
21.1.8 and RCR Lyrical; upstream GCC/CMake and Humble/Jazzy were not run.

## Velocity-smoother tests: Lyrical TF headers and empty-input fixture

Local source correction: `b1c0b446`.

- [ ] Associate the two TF header corrections with a matching upstream PR.
  Checked 2026-11-17: no matching velocity_smoother/Quaternion.hpp PR found.
  Upstream main still uses Quaternion.h in
  [test_resample.cpp](https://github.com/autowarefoundation/autoware_core/blob/main/planning/autoware_velocity_smoother/test/test_resample.cpp)
  (blob 0e066d206aa1f3b30c178fa8158d8736a60eb3d7) and
  [test_pseudo_jerk_smoothers.cpp](https://github.com/autowarefoundation/autoware_core/blob/main/planning/autoware_velocity_smoother/test/test_pseudo_jerk_smoothers.cpp)
  (42b267ea35a01a1b0fd9cbc03bda8e0ee2d61996). The selected Lyrical TF provider
  removes that spelling; use Quaternion.hpp without changing assertions. This
  matches the removed-header category above, rather than an OSQP API migration.
- [ ] Associate the empty-input fixture correction with a matching upstream PR.
  Checked 2026-11-17: no EmptyTrajectoryCase PR found. Main
  [test_stop_velocity_calculation.cpp](https://github.com/autowarefoundation/autoware_core/blob/main/planning/autoware_velocity_smoother/test/test_stop_velocity_calculation.cpp)
  (blob 12d1d65676eb3e48d11dde7710e3be92bf5ff890) still pushes one point into its
  supposedly empty trajectory. The current builder legitimately accepts that
  singleton, so the assertion fails. Remove that push to test empty input as
  named; keep the rejection assertion. This is a fixture bug, not Lyrical-only
  compatibility. The file exists upstream but is omitted from CMake registration;
  exposing it in Bazel revealed the failure.

Evidence: the corrected complete smoother suite passes all 67 cases across six
Bazel targets, including all nine stop-velocity cases. All four actual smoother
algorithms additionally pass in both RCLCPP and enabled Agnocast configurations.
No production code changed. The package journal gives exact commands and
symptom/cause/fix details. Verified with Ubuntu 24.04, LLVM/libc++ 21.1.8 and RCR
Lyrical; upstream GCC/CMake and Humble/Jazzy were not tested.

## Object-recognition transform test uses a removed TF header

Local source correction: `ea4a5a80`.

- [ ] Associate with a matching upstream PR. Checked 2026-11-17: no matching
  object_recognition_utils/Quaternion PR found. Main
  [test_transform.cpp](https://github.com/autowarefoundation/autoware_core/blob/main/common/autoware_object_recognition_utils/test/src/test_transform.cpp)
  (blob c35e2dd42a1e4eec1726f223d2026754fa5353c1) retains Quaternion.h.

The full module fails to compile against the selected Lyrical TF provider.
Use tf2/LinearMath/Quaternion.hpp; retain every assertion and buffer.h, which
is still provided. All 33 original cases across eight sources pass with the
one-line correction. No production source changes. The module journal records
the complete clean-environment command; this is the removed-header category
already identified above, not a new solver/API transition.

## Behavior-common smoothing test needs explicit ROS shutdown

Local source correction: `20950a32`.

- [ ] Associate with a matching upstream PR. Checked 2026-11-17: a
  smoothPath/shutdown PR search found no match. Main
  [test_trajectory_utils.cpp](https://github.com/autowarefoundation/autoware_core/blob/main/planning/behavior_velocity_planner/autoware_behavior_velocity_planner_common/test/src/test_trajectory_utils.cpp)
  (blob c43a390befa3df5ba5acecab482cbadcccf941b6) initializes RCLCPP in
  smoothPath.nominal without shutting it down.

All 43 assertions pass, followed by a SIGSEGV during process exit. Gdb shows
spdlog registry find/drop called from rcl_logging_external_shutdown and the
global RCLCPP context destructor. Add a local shutdown guard immediately after
init, so node/planner locals destruct before ROS shutdown and shutdown precedes
static teardown, even on early test return. All 43 original cases then pass with
successful exit; no assertions, cases or production source are changed.
This is a test-lifecycle correction exposed by this LLVM/libc++/Lyrical build,
not evidence of a production logger bug. See the module journal for the complete
command and stack-capture environment. Both findings were verified on Ubuntu
24.04, LLVM/libc++ 21.1.8 and RCR Lyrical/Cyclone DDS; upstream GCC/CMake and
Humble/Jazzy were not run.

## Motion planner cannot reload an unloaded plugin

Local source correction: `c8a3e8fe`.

- [ ] Associate with a matching upstream PR. Checked 2026-11-17: repository
  searches for motion_velocity_planner/reload and isClassLoaded found no match.
  Upstream main
  [planner_manager.cpp](https://github.com/autowarefoundation/autoware_core/blob/main/planning/motion_velocity_planner/autoware_motion_velocity_planner/src/planner_manager.cpp)
  (blob 21ca3ddca6d157cb1ff833294141e205fdc45727) still uses
  plugin_loader_.isClassLoaded to reject duplicate module loads.

Reproduce with the actual planner and ObstacleStopModule: configure its original
YAML inputs and class name, start the node, call unload_plugin with that name,
then load_plugin with the same name. Unload logs success; reload warns "already
loaded" and creates no new module. The services have empty responses, so a
reply alone does not prove success. pluginlib retains class/library availability
after the active instance is erased. Duplicate detection must inspect the
manager's loaded_plugins_ instance list, not its loader cache. Use std::any_of
with get_module_name(), retaining duplicate rejection and the rest of both
service paths. This is a lifecycle bug rather than a Lyrical-only issue.

The root-owned executable regression proves initial loading/mapped library,
original parameter values, rejection of an active duplicate, unload, successful
reload and clean shutdown. Before the correction it times out on the second
successful-load log; afterward it passes in 1.4 seconds without relaxing its
25-second bound. All 14 original planner and 39 obstacle-stop cases also pass,
with no skips or assertion changes. See the motion-planner journal for exact
commands and declared resources. Verified on Ubuntu 24.04, LLVM/libc++ 21.1.8,
RCR Lyrical and Cyclone DDS; upstream CMake/GCC and other ROS releases were not
run. No upstream submission has been made.

## Core planning launch omits vehicle parameters for three nodes

Local source correction: `acf8878f`.

- [x] Associate related upstream evidence: merged
  [core#1327](https://github.com/autowarefoundation/autoware_core/pull/1327)
  explicitly reports the same wheel_radius startup failure for mission planner,
  path generator and velocity smoother. Its inspected diff changes NodeAdaptor
  endpoints and corresponding remaps; it does not supply vehicle parameters.
- [ ] Associate a matching fix. Checked 2026-11-17: upstream main XML (blob
  381a878cc439d98a5af4aa53bf06ddf18950a875) retains all three omissions; the
  repository-scoped core-planning/vehicle PR search found no matching fix.

Reproduce by launching the original autoware_core_planning XML with a valid
vehicle_param_file override. Behavior/motion planners receive that file, but
mission planner/path generator abort on uninitialized wheel_radius and smoother
fails construction with an unset double. Add the existing vehicle_param_file
as a param-file input to exactly those three missing consumers. Preserve vehicle
values, XML defaults, node logic and topic names; do not hide the omission with
global test parameter injection or fallback values in VehicleInfoUtils.

The original six-process XML launch then passes configuration/graph checks and
clean exit assertions under Bazel/Cyclone DDS. The complete affected suite gate
passes all 167 original C++ cases and existing executable checks; see the core
planning journal for exact commands and separate Bazel resource-name fixes.
This is a launch configuration bug, not a Lyrical-only correction. Ubuntu 24.04,
LLVM/libc++ 21.1.8 and RCR Lyrical were used; upstream CMake/GCC, other ROS
releases and closed-loop driving were not tested. No upstream submission made.

## Optional RViz in the top-level core launch

- [ ] Associate with an upstream issue or PR. Reviewed 2026-11-18; no matching
  RViz/headless or launch_rviz issue/PR found. This finding has not been submitted.

Package: autoware_core, original launch/autoware_core.launch.xml. Local pre-fix
branch revision: 9080cb68582a1816b62d634734a0f134a5903746. Inspected upstream
[launch source at 9799c6d](https://github.com/autowarefoundation/autoware_core/blob/9799c6da7cdf4cb0bd9d9766e62acb406fd2f73c/autoware_core/launch/autoware_core.launch.xml)
still starts RViz unconditionally, unlike its eight switchable functional groups.

Reproducer: assemble the core runtime and model/map resources without a GUI
runtime, then include the original top-level launch with all functional groups
enabled. Supplying launch_rviz=false has no effect because the argument/node
condition is absent. The launch fails trying to resolve rviz2 before its behavior
tests finish. This applies to a headless deployment regardless of build system.

Proposed fix: add a default-true launch_rviz argument and condition only the
existing RViz node on it. Existing GUI callers retain the same default. Related
precedent: [autoware_launch's RViz option](https://github.com/autowarefoundation/autoware_launch/blob/main/autoware_launch/launch/autoware.launch.xml),
which is not a matching core fix. This issue is independent of Lyrical support;
[AWF's Lyrical tracker](https://github.com/autowarefoundation/autoware/issues/7404)
does not supply evidence for this launch behavior.

Local validation: Ubuntu 24.04, LLVM/libc++ 21.1.8, RCR Lyrical and Cyclone DDS.
The original full launch failed before the change and passed afterward with
launch_rviz=false, actual map/model data, all eight functional groups, vehicle
parameters, typed velocity relay, API version request, planning/control graph
and clean shutdown. The added test also asserts the original XML keeps the GUI
option default true. Reproduce with
`bazel test --config=no-agnocast //bazel/tests:core_system_headless_launch_test`.
The final Phase 5.3.4 journal records the complete ten-target/204-case gate.
Upstream CMake/GCC, other ROS releases and default-on GUI rendering were not
run. Local source fix: `01f30be24d7bcea96d4059ea937e67159288ddef`. No corresponding upstream PR is currently
associated.

## TensorRT common standard-library header compatibility

- [ ] Associate with matching upstream fixes. Reviewed 2026-11-18; searches for
  filesystem/profiler PRs in autoware_cuda found none. These findings have not
  been submitted. The Lyrical tracker is related, not a matching fix.

Repository/package: autowarefoundation/autoware_cuda, autoware_tensorrt_common.
Inspected upstream main a5929dde0bacecae60c5f32dc34416f784a60cb4 still contains
both patterns in
[utils.hpp](https://github.com/autowarefoundation/autoware_cuda/blob/a5929dde0bacecae60c5f32dc34416f784a60cb4/autoware_tensorrt_common/include/autoware/tensorrt_common/utils.hpp)
and
[profiler.cpp](https://github.com/autowarefoundation/autoware_cuda/blob/a5929dde0bacecae60c5f32dc34416f784a60cb4/autoware_tensorrt_common/src/profiler.cpp).
Local source correction: `5b78266c1158f7731a77423b701f5eee4ecef675` (pre-fix branch revision 7eac2d98).
It is separate from SDK/Bazel definitions.

Reproducer: build the two-source library and its complete original native test
using LLVM/libc++ 21.1.8, Ubuntu 24.04 and the declared TensorRT 10.16.1.11 /
CUDA 13.2.2 SDK. profiler.cpp instantiates std::ostringstream without including
`<sstream>`; libc++ supplies only a forward declaration through its other headers.
Add the direct include. Compiling the real test consumer then fails because
utils.hpp selects experimental/filesystem. Its compiler check references
nonexistent `__GNUC_MAJOR__`, and Clang's GCC compatibility version would be a
poor standard-library capability check even with a corrected spelling. Use
`__has_include(<filesystem>)` and retain the existing experimental fallback.
Neither fix changes inference or tensor-reconciliation behavior.

Validation: the actual shared-library export and all 13 original TensorRT tests
pass, including ONNX parsing/GPU engine builds; all 17 CUDA-utils cases also
pass, with no skips/failures. Reproduce with
`bazel test --config=no-agnocast --nocache_test_results --test_output=errors @autoware_tensorrt_common//... @autoware_cuda_utils//...`.
The Phase 6.2 journal records complete inputs, SDK/runtime audit and commands.

[AWF's Lyrical tracker](https://github.com/autowarefoundation/autoware/issues/7404)
reported a core build excluding CUDA, so it does not establish these fixes or
our validation. These are compiler/standard-library compatibility findings,
not Lyrical-only behavior changes. Upstream CMake/GCC, Humble/Jazzy, other SDK
versions and ARM were not tested. Suggested upstream action: the two small
source corrections and compilation with a standard library that does not
provide incidental transitive includes. No upstream PR is currently associated.

## CUDA blackboard example drops image headers

- [ ] Associate with a matching upstream PR. Reviewed 2026-11-19; no matching
  correction found in the related PRs inspected below. Not submitted upstream.

Local source correction: `8c5dc049d5f93d5de384c6e811622db13220f6c8` (separate from Bazel definitions).

Repository/package: autowarefoundation/cuda_blackboard. The original example
subscriber manually reconstructs output_image from a CudaImage but omits its
header. The actual GPU pipeline produces correct image bytes with an empty
frame and zero timestamp, losing the publisher's camera_frame and acquisition
stamp. Copy ros_image->header = cuda_msg->header before publishing; no transport
or GPU allocation changes are required. This is a general metadata bug, not a
Lyrical-specific compatibility change.

[Inspected upstream source](https://github.com/autowarefoundation/cuda_blackboard/blob/a4f330e5c228d713774450b0331b83b94c55a466/src/cuda_blackboard_subscriber_node.cpp)
still lacks the assignment. Compared the actual subscriber-file patches in
PRs [14](https://github.com/autowarefoundation/cuda_blackboard/pull/14),
[21](https://github.com/autowarefoundation/cuda_blackboard/pull/21),
[22](https://github.com/autowarefoundation/cuda_blackboard/pull/22),
[27](https://github.com/autowarefoundation/cuda_blackboard/pull/27) and
[29](https://github.com/autowarefoundation/cuda_blackboard/pull/29); none adds it.
The [Lyrical tracker](https://github.com/autowarefoundation/autoware/issues/7404)
is related platform work, not a matching metadata fix.

Reproducer: bazel test --config=no-agnocast --nocache_test_results
//bazel/tests:cuda_blackboard_component_test. The added test runs the original
example.launch.py with a tiny PPM, checks exact output pixels and image metadata,
and asserts clean shutdown. It fails on the unmodified source (empty frame),
then passes both cases after the one-line copy on CUDA 13.2.2 / NVIDIA 595.99.02.
The Phase 6.3.4 and source-fix journal entries record the full commands and
prerequisites. Upstream CMake and other platforms were not tested.

## TensorRT classifier uses experimental filesystem

- [ ] Associate with a matching upstream PR. Reviewed 2026-11-19; none found
  in the filesystem/TensorRT search and related tracker inspected. Not submitted.

Repository/package: autowarefoundation/autoware_universe,
autoware_tensorrt_classifier. fileExists explicitly uses
std::experimental::filesystem even though the package compiles as C++17.
Clang/libc++ 21.1.8 rejects both references: no member experimental in std.
Include <filesystem> directly and use std::filesystem. The full package build
and all five added GPU inference/preprocessing cases pass with this correction;
the journal records the reproducible build and behavior gate.

[Inspected upstream source](https://github.com/autowarefoundation/autoware_universe/blob/3f14a7bbba980fb6c337f4c55608f6b9ea81c88c/perception/autoware_tensorrt_classifier/src/tensorrt_classifier.cpp)
still contains the experimental namespace. The
[Lyrical tracker](https://github.com/autowarefoundation/autoware/issues/7404)
and its related [toolchain tracker](https://github.com/autowarefoundation/autoware_universe/issues/13590)
cover broader platform/compiler work, not a matching classifier correction.
This is standard-library compatibility, not a Lyrical-only behavior change.
Upstream CMake/GCC, older ROS distributions, INT8 calibration and ARM remain
untested. Suggested action: the two-line correction plus a libc++ compilation
check. Local source correction is separate from the Bazel integration commit.

## Image decompressor assumes std::vector message storage

- [ ] Associate with a matching upstream PR. Reviewed 2026-11-19; none found
  by the decompressor/Buffer and decompressor/Lyrical searches. Not submitted.

Repository/package: autowarefoundation/autoware_universe,
autoware_image_transport_decompressor. The expression
cv::Mat(compressed_image.data) requires a std::vector-specific OpenCV overload;
Lyrical message data is rosidl Buffer. The real module compilation fails with
no matching conversion. Use a contiguous one-row CV_8UC1 data()/size() view as
cv::imdecode input, retaining the source message's ownership and lifetime.
The decoder reads the input; no vector copy or encoding behavior change is
needed. The complete original decoder and ROS-node suites pass after the fix.

[Inspected upstream source](https://github.com/autowarefoundation/autoware_universe/blob/3f14a7bbba980fb6c337f4c55608f6b9ea81c88c/sensing/autoware_image_transport_decompressor/src/image_transport_decompressor.cpp)
still contains the vector-specific construction. The
[Lyrical tracker](https://github.com/autowarefoundation/autoware/issues/7404)
and [toolchain tracker](https://github.com/autowarefoundation/autoware_universe/issues/13590)
are related platform work, not matching corrections. Searches also returned
PRs 13167, 11222 and 8165 for other camera consumers; those search matches do
not establish a decompressor fix or local validation. Reproduce with the
Phase 6.4.4 complete module command in the journal. The source fix is committed
separately; older ROS/CMake and ARM have not been tested.

## YOLOX standard dependency and filesystem headers

- [x] Associate the expected header/manifest correction with matching merged
  [AWF PR #13622](https://github.com/autowarefoundation/autoware_universe/pull/13622)
  ([commit c992af10](https://github.com/autowarefoundation/autoware_universe/commit/c992af10f4c463fda78a06348c139357b9f195ad)).
  Reviewed 2026-11-19; merged 2026-11-13. Its actual YOLOX changes use
  tl/expected.hpp and libexpected-dev, replacing the ROS tl_expected wrapper.
- [ ] Associate the filesystem/sstream corrections with a matching PR. No
  matching correction is associated; not submitted upstream.

Local source correction: `1b2f9a6679152fb3dcdf61476064d035f0f5c447`.

Local experiment: Clang/libc++21 cannot find experimental/filesystem; the
single-image node selects it through nonexistent __GNUC_MAJOR__. label.cpp
also lacks the direct sstream include needed to instantiate stringstream.
Use std::filesystem, __has_include for the existing conditional fallback and
the direct standard header. Separately align the expected include/package.xml
with #13622 after the actual build exposes the missing wrapper header. The
Bazel provider is pinned tl-expected 1.3.1, not a host libexpected-dev install.

Checked upstream main 3f14a7bbba980fb6c337f4c55608f6b9ea81c88c:
[label.cpp](https://github.com/autowarefoundation/autoware_universe/blob/3f14a7bbba980fb6c337f4c55608f6b9ea81c88c/perception/autoware_tensorrt_yolox/src/label.cpp),
[tensorrt_yolox.cpp](https://github.com/autowarefoundation/autoware_universe/blob/3f14a7bbba980fb6c337f4c55608f6b9ea81c88c/perception/autoware_tensorrt_yolox/src/tensorrt_yolox.cpp)
and the
[single-image node](https://github.com/autowarefoundation/autoware_universe/blob/3f14a7bbba980fb6c337f4c55608f6b9ea81c88c/perception/autoware_tensorrt_yolox/src/yolox_single_image_inference_node.cpp)
still have the filesystem/direct-header patterns. #13622 fixes the expected
layout; it is not evidence for those other corrections. The
[Lyrical tracker](https://github.com/autowarefoundation/autoware/issues/7404)
and related [toolchain tracker](https://github.com/autowarefoundation/autoware_universe/issues/13590)
are broader platform work. These are dependency/standard-library compatibility
changes, not Lyrical-only algorithm changes.

Validation: the entire package builds and all fourteen original cases pass,
including real GPU detection/segmentation and ROS-node delivery, with zero skips.
The Phase 6.4.4 journal records the complete inputs, runtime fixes and commands.
Upstream CMake/GCC, older ROS, INT8 calibration and ARM are not tested. The local
source correction is committed separately from the build definitions.

## Packaged cumm TensorView conditional instantiation with Clang

- [ ] Associate with a matching upstream PR. Reviewed 2026-11-19; no matching
  fix is associated or submission made. Related reports are
  [cumm #23](https://github.com/FindDefinition/cumm/issues/23) and
  [spconv #658](https://github.com/traveller59/spconv/issues/658), not matching
  fix/test evidence.

Affected input: AWF spconv_cpp's spconv2.3.8/cumm0.5.3 cu130-rev1 amd64 SDK,
used by the complete local TensorRT plugin library. Clang21 instantiates invalid
std::copy conversions while forming Tensor::astype's callback-based conditional,
even for forbidden integer/half and half/bfloat pairs. The build fails before
plugin linking. The
[local dependency patch](../../third_party/spconv/patches/tensorview_clang_lambda.patch)
uses C++17 if constexpr with the same conversion predicate, preserving rejection
and the vendor binary API. The Phase 6.4.1 journal records exact artifacts,
checksums, compiler errors, GNU ABI selection and commands.

[Inspected cumm main](https://github.com/FindDefinition/cumm/blob/d971d02a0b29b7e6da46186564f37bde77b99dcc/include/tensorview/tensor.h)
still uses callback dispatch. This is Clang/header compatibility, not a
Lyrical-only algorithm issue. The SDK probe passes float/half round trips,
rejected int64-to-half conversion and actual allocator/container/GPU sorting;
all eighteen original plugin cases and both opaque TensorRT interop cases pass.
No vendor library binary is modified.

Suggested upstream action: avoid instantiating the discarded conversion branch
under Clang while preserving supported language versions. This local patch
requires C++17 or newer; do not assume it is suitable unchanged for upstream
C++14 consumers. Source-built spconv/cumm, upstream CMake and ARM are untested.

## Learning-based vehicle model finalizes Python before its objects

Local source corrections: `b0be9d2d` and `8e0efa8d`, separate from Bazel definitions.

- [ ] Associate with a matching upstream fix. Reviewed 2026-11-21: no matching
  fix in the standalone simulator's inspected issue/PR list. Not submitted.

Repository/package: autowarefoundation/autoware_simple_planning_simulator,
autoware_learning_based_vehicle_model. Upstream main
087ff2c2867121a4807bdf7360a964aef40004f2 still declares submodels before guard
and has no destructor to release them first in
[interconnected_model.hpp](https://github.com/autowarefoundation/autoware_simple_planning_simulator/blob/087ff2c2867121a4807bdf7360a964aef40004f2/autoware_learning_based_vehicle_model/include/autoware/learning_based_vehicle_model/interconnected_model.hpp).
The inspected [clang-tidy PR #25](https://github.com/autowarefoundation/autoware_simple_planning_simulator/pull/25)
changes no learning-model lifetime code. The
[Lyrical tracker](https://github.com/autowarefoundation/autoware/issues/7404)
is related platform work, not a matching correction.

Reproduce by constructing InterconnectedModel, creating a Python fixture class
inside its interpreter, adding that submodel and leaving the C++ scope normally.
The root regression at bazel/tests/autoware_learning_based_vehicle_model/test_embedding.cpp
also exercises parameter/dt dispatch, updates and state reset before teardown.
With Python 3.14.4, LLVM/libc++ 21.1.8 and Ubuntu 24.04, destruction segfaults:
_Py_Dealloc → pybind11::handle::dec_ref → SimplePyModel::~SimplePyModel →
InterconnectedModel::~InterconnectedModel. Reverse member destruction destroys
guard first; submodels then decref objects after interpreter finalization.

Clear submodels in an explicit destructor body before guard destruction, and
explicitly default move construction to preserve the original implicit move API.
The regression also moves and updates a live model before clean teardown. The
correction preserves member layout, API and calculations and requires no ROS
version guard. The complete four-source library builds and both root mapping/
embedding cases pass afterward, with no failure/skip and a normal process exit.
The Phase 7.3.1 journal records exact Bazel/run-under commands and the native
backtrace. These results establish the tested Python 3.14 configuration; older
Python/ROS versions and upstream CMake are untested. This is interpreter lifetime,
not a Lyrical-only algorithm change. Keep the source fix separate from its
package-local Bazel definitions.

## Universe mission-planner arrival test: tf2 header compatibility

Observed 2026-11-21 with the local autoware_mission_planner_universe 0.53.0
snapshot and Lyrical RCR lyrical.2026-10-05.rcr.3: building its original arrival
suite fails with `tf2/LinearMath/Quaternion.h file not found`. The selected tf2
exports Quaternion.hpp. Change the include spelling and preserve all 21
original arrival assertions; the complete suite passes after this correction.
This is test-source compatibility, without a planner behavior change.
Local source fix: f8acc3b6 (include spelling/grouping only).

- [ ] Associated with a matching AWF PR. Checked 2026-11-21: upstream
  [main test source](https://github.com/autowarefoundation/autoware_universe/blob/main/planning/autoware_mission_planner_universe/test/test_arrival_checker.cpp)
  still includes Quaternion.h. Searches scoped to Universe for Quaternion.hpp
  and arrival_checker/Lyrical find no matching PR. Not submitted.
- [x] Related upstream work identified:
  [Autoware Lyrical tracker](https://github.com/autowarefoundation/autoware/issues/7404)
  and [Universe Lyrical tracker](https://github.com/autowarefoundation/autoware_universe/issues/13620).
  The linked dependency/CMake/Qt work does not correct this test include.
- [x] Local evidence: all 36 original mission-planner cases pass (15 planner/
  utility and 21 arrival), CPU/Cyclone DDS, ENABLE_AGNOCAST=0. See the
  [source-fix journal](JOURNAL.md#2026-11-21-phase-732--universe-arrival-checker-tf2-compatibility)
  for the command. No Humble/Jazzy or colcon validation is claimed.

## Boundary-departure checker: expected header/provider compatibility

- [x] Associated with matching merged
  [AWF PR #13622](https://github.com/autowarefoundation/autoware_universe/pull/13622).
  Checked 2026-11-21: the PR is merged and its actual boundary-checker changes
  match these four tl/expected.hpp includes plus libexpected-dev in package.xml.
- [x] Independently reproduced locally: the initial library compile cannot find
  tl_expected/expected.hpp with the pinned native tl-expected 1.3.1 provider.
  The complete 66-case original suite passes after the package-scoped fix,
  CPU/Cyclone DDS with ENABLE_AGNOCAST=0; no assertions are edited or skipped.

The initial correction applies to autoware_deprecated_boundary_departure_checker.
On 2026-11-22 the selected motion boundary-prevention module independently exposes
the same missing header. Its two includes and manifest correction also exactly
match merged #13622 (all 41 PR files inspected), and the complete motion group
passes 16 targets including individual/joint module reload. See the
[module source-fix evidence](JOURNAL.md#2026-11-22-phase-732--boundary-prevention-expected-compatibility).
The selected trajectory-controller group independently exposes the same issue
on 2026-11-22: two follower-base headers, MPC mpc.hpp and PID's node header,
plus the three owning manifests, exactly match the merged PR. The full
139-active-case/seven-target group passes, preserving one original disabled
pure-pursuit case. See the
[controller source-fix evidence](JOURNAL.md#2026-11-22-phase-732--trajectory-follower-base-expected-compatibility).
The separate unused experimental/optional removal is not part of that association.
Other packages in that PR enter compatibility work only when a current target
exposes the need. No host libexpected-dev installation, older
ROS validation or colcon result is implied by this manifest alignment. See the
[source-fix journal](JOURNAL.md#2026-11-21-phase-732--boundary-checker-expected-compatibility)
for the failure, integration context and exact command.

## Freespace tests: removed rcpputils filesystem helper

- [ ] Associated with a matching upstream PR. Checked 2026-11-21: a scoped
  freespace/filesystem PR search found no match. The upstream main test source
  still uses rcpputils::fs::remove_all at blob
  [49b85f9a9017415a61b8a41d67159f2bbb21724b](https://api.github.com/repos/autowarefoundation/autoware_universe/git/blobs/49b85f9a9017415a61b8a41d67159f2bbb21724b).
  A broader Lyrical change may cover this; no matching association is asserted.
- [x] Reproduced locally: the original test does not compile against Lyrical's
  removed filesystem helper. Replacing its include/call with C++17
  filesystem/std::filesystem::remove_all permits all ten original freespace
  and both original RRT cases to pass, with no assertions changed or skipped.

The fix only affects the optional rosbag-output directory cleanup helper.
CPU/Cyclone DDS with ENABLE_AGNOCAST=0 is validated; optional bag export and
older ROS distributions are not. The
[journal](JOURNAL.md#2026-11-21-phase-732--freespace-test-filesystem-compatibility)
records the diagnosis and the following module entry gives the full command.

### Lane-change fmt::join needs its declaring header

Local source fix: `4120f86e`, separate from Bazel definitions.

- [x] Associated with merged [universe#13594](https://github.com/autowarefoundation/autoware_universe/pull/13594),
  reviewed 2026-11-22 (merge 0aca0ea44d6ebba9600f753bb561f423591459c8).
  The one-line source fix is identical. The broader
  [toolchain tracker #13590](https://github.com/autowarefoundation/autoware_universe/issues/13590)
  is also linked from the Lyrical transition tracker; no unrelated fixes are applied.
- Input: local Universe 0.53.2 snapshot imported by 7fbc81af,
  planning/behavior_path_planner/autoware_behavior_path_lane_change_module/src/interface.cpp.
  LLVM/libc++ 21.1.8, C++23, RCR Lyrical and fmt 12.2.0 on Ubuntu 24.04.
  Compiling the real module reports two missing fmt::join declarations in
  LaneChangeInterface::isExecutionRequested even with the fmt library declared.
- Include fmt/ranges.h next to fmt/format.h. The former declares the range join
  API with modern fmt; changing the Bazel dependency alone cannot provide that
  declaration. Preserve lane-ID formatting and runtime behavior. Upstream's PR
  reports older fmt compatibility; this branch does not claim an older-fmt run.
- Reproducer and local module-gate results are in the lane-change journal entry.
  This is alignment with an existing upstream fix, not a new upstream submission.

### Grid-map edge ordering must accept const operands

Local source fix: `73b37641`, separate from Bazel definitions.

- [ ] Associate with a matching upstream fix. Reviewed 2026-11-22: PR/issue
  searches for grid_map_utils/const and PolygonIterator return no matching
  correction; current
  [upstream header](https://github.com/autowarefoundation/autoware_universe/blob/main/common/autoware_grid_map_utils/include/autoware/grid_map_utils/polygon_iterator.hpp)
  still declares a non-const member. The
  [Lyrical tracker](https://github.com/autowarefoundation/autoware/issues/7404) and
  [toolchain tracker](https://github.com/autowarefoundation/autoware_universe/issues/13590)
  are compatibility context, not a matching fix association.
- Input: Universe 0.53.2 snapshot imported by 7fbc81af,
  common/autoware_grid_map_utils/include/autoware/grid_map_utils/polygon_iterator.hpp.
  LLVM/libc++ 21.1.8, C++23, grid-map 2.5.0, Ubuntu 24.04/RCR Lyrical.
- Reproduce by compiling the real polygon_iterator.cpp through the complete
  local module gate in the corresponding journal entry. libc++'s std::__less
  compares const Edge references; operator< cannot be called on the const left
  operand, causing invalid-operands/no-matching-function errors in std::sort.
- Qualify operator<(const Edge &) as const. Its body only reads endpoint
  coordinates, so ordering and object layout are unchanged. The full original
  seven-case polygon suite passes and its original benchmark builds. This is
  source-level Clang/libc++ portability, not an older-GCC runtime claim.
  No upstream submission has been made; source fix and build definitions are
  committed separately.

### Intersection UUID construction with modern Boost

Local source fix: `67287e6e`, separate from Bazel definitions.

- [x] Associated with open [universe#13605](https://github.com/autowarefoundation/autoware_universe/pull/13605),
  reviewed 2026-11-22 at head c4e36a66e05eef83f000a4d28e59c9cab648c71d.
  The intersection edit matches; the PR also changes the unselected roundabout
  module. Current main still contains the failing initializer. The
  [Lyrical tracker](https://github.com/autowarefoundation/autoware/issues/7404) and
  [toolchain tracker](https://github.com/autowarefoundation/autoware_universe/issues/13590)
  provide context. This is not a new upstream submission.
- Input: local Universe 0.53.2 snapshot, object_manager.hpp in the intersection
  module; Boost.UUID 1.90.0, LLVM/libc++ 21.1.8/C++23 and RCR Lyrical on Ubuntu
  24.04. Sixteen-byte brace initialization fails with no matching constructor.
- Value-initialize boost::uuids::uuid and copy the ROS message's sixteen bytes
  through begin(). Keep boost::hash and key semantics. The journal's complete
  module gate passes its original suite, added UUID lookup regression and both
  standard/experimental lifecycle checks with CPU/Cyclone DDS and Agnocast off.
  Older Boost builds and enabled-Agnocast runtime are not claimed.

### Path optimizer's production header includes unused GTest

Local source fix: `88b93365`.

- [ ] Associate with a matching upstream correction. Reviewed 2026-11-22:
  [current mpt_optimizer.hpp](https://github.com/autowarefoundation/autoware_universe/blob/main/planning/autoware_path_optimizer/include/autoware/path_optimizer/mpt_optimizer.hpp)
  retains gtest/gtest.h. The path_optimizer/gtest PR search returns only the
  original feature PR #11479, which is not a fix association.
- Local Universe 0.53.2, LLVM/libc++ 21.1.8/C++23: compiling production
  debug_marker.cpp fails to find gtest/gtest.h through this public header.
  No production header or implementation uses GTest declarations/macros.
- Remove the unused include; retain the original node test's declared GTest
  dependency. The complete local optimizer suite and indexed startup/script
  checks pass with CPU/Cyclone DDS and Agnocast off. Exact commands are in the
  optimizer journal. This is production/test dependency separation, not a claim
  of a Lyrical-specific API change or a new upstream submission.

### Cruise PID plugin redeclares persistent node parameters on reload

Local source fix: `15352cbe`.

- [ ] Associate with a matching upstream fix. Reviewed 2026-11-22: current
  [PID constructor](https://github.com/autowarefoundation/autoware_universe/blob/main/planning/motion_velocity_planner/autoware_motion_velocity_obstacle_cruise_module/src/pid_based_planner/pid_based_planner.cpp)
  retains 22 node.declare_parameter calls. PR searches for cruise/reload and
  cruise/ParameterAlreadyDeclaredException return no matching correction.
- The real motion planner loads ObstacleCruiseModule using the default pid_base
  config. Unload succeeds; reload aborts with ParameterAlreadyDeclaredException
  at min_accel_during_cruise because parameters outlive their plugin object.
  The complete original planning-factor test alone does not expose this.
- Read-or-declare all PID-constructor parameters through the existing utility,
  preserving types and current node-owned values. The root lifecycle regression
  changes minimum acceleration to -1.25 and verifies its value after reload,
  duplicate-load handling and clean shutdown. The complete three-module group
  passes 11 cases across seven targets with Cyclone DDS and ENABLE_AGNOCAST=0.
  Exact commands/results are in the journal. This is a general plugin lifecycle
  defect; the optional optimization branch and other middleware configurations
  are not newly validated. No upstream submission has been made.

### Universe/core utilities duplicate a global TF conversion

Local source fix: `5eb05315`.

- [ ] Associate with a matching upstream correction. Reviewed 2026-11-22:
  [Universe geometry.cpp](https://github.com/autowarefoundation/autoware_universe/blob/main/common/autoware_universe_utils/src/geometry/geometry.cpp)
  retains a global tf2::fromMsg(PoseStamped, Stamped<Transform>) definition
  identical to local autoware_utils_geometry. Related
  [PR #11754](https://github.com/autowarefoundation/autoware_universe/pull/11754)
  is closed without merge and changes header/Galactic workarounds, not this
  implementation. It is not a matching fix association.
- The complete local Universe utility test executable, LLVM/libc++ 21.1.8/C++23,
  fails to link with duplicate strong symbols from the two geometry objects.
  Shared-library builds can conceal this source ownership conflict; this is not
  a claim that all CMake builds currently fail.
- Remove the duplicate Universe definition and depend explicitly on the identical
  canonical core geometry provider. Preserve the public declaration and all
  namespaced APIs. The existing manifest already depends on the compatibility
  utility package that declares core geometry. The full 118-active-case original
  suite and an added joint-library conversion/RPY regression pass. Four original
  disabled geometry cases remain disabled. See the journal for exact commands.
  CPU/Cyclone DDS and Agnocast off are validated; no new colcon or older-ROS claim
  is made, and no upstream submission has been made.

### Velocity-limiter plugin redeclares node-owned parameters

- [ ] Associate with a matching upstream change. Reviewed 2026-11-22: current
  [parameter source](https://github.com/autowarefoundation/autoware_universe/blob/main/planning/motion_velocity_planner/autoware_motion_velocity_obstacle_velocity_limiter_module/src/parameters.hpp)
  retains unconditional declarations; a scoped limiter/reload PR search finds
  no matching fix. No new submission is claimed.
- Reproducer: load the limiter in the actual motion planner, set
  trajectory_preprocessing.downsample_factor=4, unload and reload through its
  services. First initialization passes; reload aborts on that existing key.
  Fixing scalars alone then aborts on obstacles.static_map_tags (string vector).
- Use read-or-declare initialization for all 22 header calls and the source's
  distance_buffer call. Retain current values and public names/types. All 19
  original cases plus standalone and joint nine-plugin reload/retention pass,
  CPU/Cyclone DDS, ENABLE_AGNOCAST=0. This is a persistent-node lifecycle defect,
  not a Lyrical-specific algorithm issue. See the
  [source-fix journal](JOURNAL.md#2026-11-22-phase-732--velocity-limiter-plugin-reload)
  for exact verification and scope.

### Motion factories publish incompatible internal C++ type identities

- [ ] Associate with a matching upstream change. Reviewed 2026-11-22: current
  [cruise types](https://github.com/autowarefoundation/autoware_universe/blob/main/planning/motion_velocity_planner/autoware_motion_velocity_obstacle_cruise_module/src/types.hpp),
  [slow-down types](https://github.com/autowarefoundation/autoware_universe/blob/main/planning/motion_velocity_planner/autoware_motion_velocity_obstacle_slow_down_module/src/types.hpp)
  and [road-user-stop types](https://github.com/autowarefoundation/autoware_universe/blob/main/planning/motion_velocity_planner/autoware_motion_velocity_road_user_stop_module/src/types.hpp)
  retain common names in autoware::motion_velocity_planner. Scoped
  motion_velocity DebugData/ODR/segmentation PR searches find no matching fix.
- LLVM/libc++ 21.1.8, native ELF shared plugins, CPU/Cyclone DDS: every factory
  passes alone, but jointly unloading cruise segfaults in its destructor;
  gdb records free(0x3fc999999999999a). nm shows exported weak conflicting type
  destructors and shared_ptr control blocks. Different DebugData and filtering
  layouts share one C++ identity, also colliding with core stop types.
- Give Universe's conflicting CommonParam/ObstacleFilteringParam/DebugData and
  road-user StopObstacle distinct module-prefixed identities. Keep registered
  plugin classes/configuration unchanged. All complete affected native suites,
  individual lifecycles and joint nine-factory reload pass (49 active cases,
  16 targets). No source test assertions change. See the
  [diagnosis and command](JOURNAL.md#2026-11-22-phase-732--distinct-motion-plugin-internal-types).
  This is a composition/ODR defect, not a ROS release-specific API fix.

### Costmap generator includes removed message_filters forwarding headers

- [ ] Associate with a matching upstream change. Reviewed 2026-11-22: the
  [current header](https://github.com/autowarefoundation/autoware_universe/blob/main/planning/autoware_costmap_generator/include/autoware/costmap_generator/costmap_generator.hpp)
  still includes subscriber.h/time_synchronizer.h. The Lyrical/toolchain trackers
  autoware#7404 and universe#13590 do not identify a matching fix; the scoped
  costmap/message_filters PR result #5954 concerns another feature.
- Lyrical message_filters exports the .hpp files and removes both .h forwarding
  headers. Building the original costmap node fails at subscriber.h. Change just
  these two includes to .hpp. All ten original costmap cases, its indexed
  executable lifecycle and the full parking/affected adapter gate pass with
  Cyclone DDS and Agnocast off (27 active cases/eight targets).
- No implementation/assertion changes or host installs; no older-ROS/colcon
  validation is claimed. See the
  [source-fix journal](JOURNAL.md#2026-11-22-phase-732--costmap-message-filter-header-compatibility).

### Planning evaluator needs current TF and rclcpp logging APIs

- [ ] Associate with a matching AWF change. Reviewed 2026-11-22: current
  [obstacle metric header](https://github.com/autowarefoundation/autoware_universe/blob/main/evaluator/autoware_planning_evaluator/include/autoware/planning_evaluator/obstacle_metrics_calculator.hpp)
  still includes removed tf2/utils.h; current planning/motion evaluator sources
  still call get_logging_directory(). Scoped evaluator/tf2 and logging-function
  PR searches plus the Lyrical/toolchain trackers identify no matching AWF fix.
- Reproducer: build all evaluator sources against TF Lyrical and rclcpp 32.0.3.
  The include is missing; after fixing it, both output destructors fail to find
  the old function. Use tf2/utils.hpp and version-gate the logging call: primary
  [rclcpp 29.0.0 declaration](https://github.com/ros2/rclcpp/blob/29.0.0/rclcpp/include/rclcpp/logger.hpp)
  offers get_log_directory; 30+ removes the old call. Preserve the older branch
  for Jazzy. No algorithms, output layout or test assertions change.
- All 30 original metric cases and both installed executable lifecycles pass,
  with the complete affected support/costmap gate (51 active cases/12 targets),
  CPU/Cyclone DDS and Agnocast off. Older ROS/colcon is not executed. See the
  [source-fix journal](JOURNAL.md#2026-11-22-phase-732--planning-evaluator-lyrical-apis)
  and following module entry for exact command and limits. No submission is claimed.

### Trajectory-checker diagnostic fixture requires constexpr floating abs

- [ ] Associate with a matching upstream change. Reviewed 2026-11-22: the
  [current diagnostic test](https://github.com/autowarefoundation/autoware_universe/blob/main/planning/planning_validator/autoware_planning_validator_trajectory_checker/test/test_trajectory_checker_diag.cpp)
  retains constexpr std::abs for the forward-trajectory acceleration fixture.
  The Lyrical/toolchain trackers and scoped checker/constexpr PR search identify
  no matching fix. No submission is claimed.
- LLVM/libc++ 21.1.8/C++23 rejects that initializer because its floating abs
  overload is not constexpr, followed by six dependent initializer errors.
  Make those seven locals const; preserve the original std::abs call, formulas,
  inputs and every assertion. This avoids an unnecessary compile-time evaluation
  requirement in a runtime diagnostic test.
- All 25 original cases (seven direct and eighteen diagnostics) pass in 43.2
  seconds, followed by the complete 51-case/12-target consumer group, Cyclone
  DDS/Agnocast off, zero failures/skips/disabled cases. No older-toolchain result
  is implied. See the
  [source-fix journal](JOURNAL.md#2026-11-22-phase-732--trajectory-checker-fixture-math-portability).
