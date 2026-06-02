# Middleware evolution roadmap

## Current stable baseline

- Fast DDS `2.14.6`
- Fast CDR `2.2.7`
- carried Fast CDR string-serialization patch for embedded-`\0` protobuf string compatibility
- durable example and RTPS regression coverage in Bazel/CI

## Recommended stages

1. **Stabilize the 2.14.x line**
   - keep patch surface minimal
   - preserve the current example/RTPS regression suite as a release gate
   - continue validating record, FlatBuffers, and pycyber surfaces together

2. **Strengthen zero-copy on the same host**
   - improve intra-process and shared-memory/data-sharing paths first
   - add coverage for larger payloads, fanout, fanin, and service bursts before changing APIs
   - prefer bounded, discovery-aware regression checks over brittle latency assertions

3. **Introduce heterogeneous memory abstractions**
   - add explicit buffer-handle negotiation only after CPU shared-memory semantics are stable
   - model the design on ROS 2 type adaptation / negotiation and NITROS-style accelerator handles
   - require cross-device fallback behavior and observability before rollout

4. **Re-evaluate Fast DDS 3.x later**
   - treat 3.x as a migration project, not a routine upgrade
   - only revisit after the current `fastrtps`-style APIs are isolated behind thinner compatibility seams

## Release gates

- `bash scripts/release/ubuntu2204_baseline.sh`
- `bash scripts/release/build_release_artifacts.sh`
- retained example regression coverage for binary payload integrity, fanout, fanin, payload-size stress, and service burst matrices
