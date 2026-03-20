# multi PUBSUB large-size shutdown race

## Summary
- `MULTI_PUBSUB --recv recv` could fail in runner-driven sequential large-size
  runs even when isolated `131072B` or `262144B` runs passed.
- The observed failure was a server abort with
  `Invalid argument (.../core/src/utils/fast_mutex.hpp:98)`.

## Reproduction
- `env PERF_RECV_MODE=recv PERF_DEBUG=1 PERF_DEBUG_TRANSITIONS=1 ./core/perf/run_benchmarks_multi.sh --pattern PUBSUB --transports tcp --duration 1 --runs 1 --msg-sizes 131072,262144`

## Findings
- The first large size could already emit its RESULT lines on the client side
  before the PUB server aborted during shutdown.
- The runner previously sent `STOP` only after the client process had already
  exited, so the server could keep publishing while many SUB peers were in
  teardown.
- That made the failure look like a size-matrix problem, but the actual hot
  path was shutdown overlap between active PUB send and subscriber disconnect.

## Mitigation
- `comp_src_pubsub_client` now emits `CLIENT_DONE,<size>` immediately after its
  RESULT lines and before socket teardown.
- `run_comparison.py` consumes `CLIENT_DONE` and sends `STOP` to the server
  immediately, so the publisher is quiesced before subscriber shutdown.

## Regression coverage
- `core/tests/integration/monitoring/test_multi_pubsub_benchmark_process.cpp`
  now waits for the PUBSUB server to exit `0` after `STOP` instead of hiding
  shutdown crashes by force-killing the server process.

## Remaining note
- The `fast_mutex` signature still points at a core shutdown-sensitive area in
  PUB pipe lifecycle. The benchmark protocol is stabilized by the mitigation
  above, and this document keeps the symptom recorded if the same signature
  appears in other non-benchmark surfaces.
