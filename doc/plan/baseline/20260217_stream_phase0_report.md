# STREAM Phase 0 Baseline Report (2026-02-17)

## Scope

- branch/worktree: `perf/stream-phase0-5-codex-hybrid` (`/home/hep7/project/kairos/zlink-perf-wt`)
- target stack: `zlink`
- baseline workload:
  - size: `64`, `1024`
  - ccu: `1000`
  - duration: `5s`
  - repeats: `3`
  - inflight: `1`
  - client/server io threads: `1/1`

## Reproduction Commands

- `doc/plan/baseline/20260217_phase0_zlink64/command.sh`
- `doc/plan/baseline/20260217_phase0_zlink1024/command.sh`

## Results (Median, PASS rows)

| size | throughput_msg_s | throughput_mib_s | p50_us | p95_us | p99_us | pass/total |
|---:|---:|---:|---:|---:|---:|---:|
| 64 | 234588.40 | 28.64 | 4064.75 | 5730.64 | 6524.23 | 3/3 |
| 1024 | 224915.20 | 439.29 | 4244.05 | 5638.96 | 6379.32 | 3/3 |

Source files:

- `doc/plan/baseline/20260217_phase0_zlink64/summary.json`
- `doc/plan/baseline/20260217_phase0_zlink1024/summary.json`

## Environment Snapshot

- OS/Kernel: `Linux ulalax-home 6.6.87.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 5 18:30:46 UTC 2025 x86_64`
- CPU: `Intel(R) Core(TM) Ultra 7 265K`
- CPU count: `20`
- Hypervisor: `Microsoft` (WSL2)
- `ulimit -n`: `1048576`
- `/proc/sys/net/core/somaxconn`: `65535`
- `/proc/sys/net/core/rmem_max`: `16777216`
- `/proc/sys/net/core/wmem_max`: `16777216`

## Phase 0.5 Profiling Note

- profiling command template: `doc/plan/baseline/20260217_phase05_profile_zlink1024/command.sh`
- observation on 2026-02-17: `perf` tool was not installed (`/bin/bash: perf: command not found`)
- action: profiling wrapper script added at
  - `core/tests/scenario/stream/profile_stream_hotspots.sh`
  - script behavior: skip profiling gracefully when `perf` is unavailable and still run benchmark.
