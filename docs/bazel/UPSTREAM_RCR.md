# Findings for the ROS Central Registry

This document collects demonstrated findings that may help the ROS Central
Registry. Add an entry only when it includes a minimal reproducer or exact target,
the observed result, the confirmed cause, and a proposed upstream action.

For each finding, record:

- RCR module and version;
- Bazel version, platform, and relevant flags;
- minimal command or target that reproduces it;
- symptom, confirmed cause, and local experiment;
- whether current upstream still exhibits it;
- suggested issue, documentation change, test, or patch.

## Findings

### rclpy statically embeds ROS/DDS globals in its Python extension

Local correction: `a2dcb386`.

- [ ] Associate the shared-dependency correction with an upstream RCR change.

Verified 2026-11-16 on Ubuntu 24.04 x86_64, Bazel 9.3.2, LLVM/libc++ 21.1.8,
Python 3.14.4, Lyrical and Fast DDS. The selected module is
`rclpy@10.0.11-1.rcr.0`, with the published rosbag2 Python runtime in the same
process. No Agnocast allocator preload is used for this Python suite.

Reproducer in this experiment:

```bash
env -i HOME="$HOME" PATH="$HOME/.local/bin:/usr/bin:/bin" \
  bazel test --config=fastdds --config=no-agnocast \
  --nocache_test_results --test_output=errors @ros2agnocast//:tests
```

All 95 Python assertions pass, then the interpreter aborts with
`corrupted size vs. prev_size while consolidating` or
`double free or corruption (!prev)`. A GDB shutdown backtrace reaches
`eprosima::fastdds::dds::DomainParticipantQos::~DomainParticipantQos()` inside
`rclpy/_rclpy_pybind11.so`, followed by libc exit handlers. The suite imports
both rclpy and rosbag2_py; test assertions alone miss the process-exit defect.

The overlay's `rclpy_bindings` cc_shared_library omits `dynamic_deps`, despite
its matching compile/shared `DEPENDENCIES` map. ELF inspection confirms the
extension embeds Fast DDS symbols and has no ROS/DDS DT_NEEDED entries.
Other loaded native extensions use shared ROS dependencies, duplicating DDS
global state in one process. Add `dynamic_deps = DEPENDENCIES.keys()` to
`rclpy_bindings`, matching the established RCR shared-library pattern. The
local single_version_override pins that module and applies only this BUILD
change through `bazel/patches/rclpy_shared_dependencies.patch`.

After the correction, the complete 95-case suite exits successfully. The
extension has DT_NEEDED entries for librcl.so and librmw_fastrtps_cpp.so and
no longer defines the Fast DDS DomainParticipantQos destructor. Keep the normal
Python shutdown path; suppressing destructors or filtering tests would hide
the linking defect. Broader Phase 4 validation is recorded in JOURNAL.md.

The current published [module metadata](https://bazel.ros.org/modules/rclpy/metadata.json)
lists 10.0.11-1.rcr.0 as the latest version on the verification date. Its
[published overlay](https://bazel.ros.org/modules/rclpy/10.0.11-1.rcr.0/overlay/BUILD.bazel)
still omits the boundary; the downloaded overlay matches source.json integrity
`sha256-PlBGagSfMwv+J/XOTHk84MgFhtKJuZjk0CONtbdIbAI=`.
This verifies the published release, not unpublished upstream work. Suggested
upstream action: fix the overlay and add a Python native-extension coexistence
check that also requires clean interpreter exit. Other OSes, toolchains and
ROS distributions have not been checked for this finding.
