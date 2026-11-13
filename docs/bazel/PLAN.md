# Autoware + Bazel integration experiment

## Goal

Build Autoware core and CUDA packages on Ubuntu 24.04 x86_64 with Bazel,
then run a headless planning simulator from a reproducible OCI image.
Keep every build input pinned; compare host and container artifacts.
Thor cross-builds and hardware validation follow this scope.

## Decisions

- One Bazel module per ROS package; root local overrides select source or RCR.
- Bazel 9.3.2, full `ros@lyrical.2026-10-05.rcr.0` release module,
  `rosdistro@lyrical.2026-10-05.rcr.3` override.
- LLVM 21.1.8 and libc++; CUDA 13.2.2 with nvcc and hermetic clang as host compiler;
  pinned TensorRT 10.16.1.11.
- Fixed CycloneDDS by default; isolate host ROS environment variables.
- Shared Autoware compiler settings, strict warnings, machine-checked CMake parity.
- OSQP 0.6 remains a distinct shared library alongside RCR OSQP 1.0;
  LLVM OpenMP is a shared runtime.
- Agnocast is compiled in, selected at runtime. Ordinary ROS tests use the ROS
  backend; kernel-module tests use the matching Agnocast hook and module.
- Build install trees and headless images through RCR rules and rules_oci.
  Include Python/ros2cli packaging fixes, deduplication and headless PCL/VTK
  dependency separation when introducing packaging.
- Include hermetic Python and CUDA path/PID independence fixes with their features.
- Legacy repositories, Docker and Ansible tooling remain reference material;
  host NVIDIA and Docker setup playbooks may still be maintained.

## Work sequence and validation gates

1. Scope cleanup and bootstrap: pinned workspace, dev image, talker/listener.
2. Package infrastructure: interfaces, shared settings, package lists, parity check.
3. Interfaces and utilities, then dependency-layer batches of core packages.
4. Agnocast integration and remaining core components.
5. CUDA toolchain, TensorRT and GPU packages.
6. Install tree, headless runtime image and planning simulator demo.
7. Full tests, cold-build comparisons and host/container image digest comparison.

Each task records commands, results, issues and remaining gaps in JOURNAL.md.
Run appropriate build/behavior checks for each change. Separate source fixes from
Bazel changes; journal the validation context for both. Record only validation performed for the current change. Run heavy builds sequentially
with conservative resource limits.

Use Conventional Commits and sign off commits. Publish validated checkpoints
directly from local `bazel` to remote `paulsohn` branch `bazel`.
When replacing remote history, use an explicit expected SHA with force-with-lease.
