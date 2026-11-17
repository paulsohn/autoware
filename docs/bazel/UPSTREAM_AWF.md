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
