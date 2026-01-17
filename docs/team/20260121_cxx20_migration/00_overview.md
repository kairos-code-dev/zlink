# C++20 Migration Overview

## Goal

- Switch the default build standard to C++20 across the project.
- Keep opt-out capability via `ZMQ_CXX_STANDARD`.

## Changes

- Default `ZMQ_CXX_STANDARD` set to `20` in `CMakeLists.txt`.
- Build scripts already pass `-DZMQ_CXX_STANDARD=20`.

## Impact

- Compiler requirement: C++20-capable toolchain.
- No API behavior changes; build standard only.

## Benchmark Results (Runs=3)

Command:

```bash
./benchwithzmq/run_benchmarks.sh --runs 3
```

Notes:

- Clean build performed by script.
- CPU pinning disabled (default).

Summary vs libzmq:

- TCP: 64/256/1024B mostly parity or small gains; 64KB+ mixed (improve/regress by pattern).
- IPC: small messages near parity; 64KB+ shows regressions in some patterns.
- INPROC: generally positive across sizes.

Representative diffs:

- PUBSUB/TCP: 64KB +18.7%, 131KB -3.9%, 256KB -2.5%.
- DEALER_ROUTER/TCP: 64KB -8.6%, 131KB -17.5%, 256KB -17.0%.
- ROUTER_ROUTER_POLL/TCP: 64KB +21.6%, 131KB +12.1%, 256KB -9.3%.
- PUBSUB/IPC: 64KB -10.4%, 131KB -19.6%, 256KB +0.3%.
- DEALER_DEALER/INPROC: 64B +16.6%, 256B +11.4%, 1KB +7.8%.
