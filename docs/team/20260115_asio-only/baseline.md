# ASIO-Only Migration - Phase 0 Baseline

**Date:** 2026-01-15
**Author:** dev-cxx
**Phase:** 0 (Baseline Measurement)

## Executive Summary

This document records the baseline measurements for the ASIO-Only Migration project. All tests pass (61/61, 4 fuzzer tests skipped), and performance baseline data has been collected for TCP, IPC, and inproc transports.

## System Information

| Item | Value |
|------|-------|
| Platform | Linux (WSL2) |
| OS Version | Linux 6.6.87.2-microsoft-standard-WSL2 |
| CPU | Intel Core (pinned to core 0 for benchmarks) |
| Compiler | GCC 13.3.0 |
| C++ Standard | C++20 |
| Build Type | Release |
| Boost Version | 1.85.0 (bundled) |

## Test Results

### Summary

| Metric | Value |
|--------|-------|
| Total Tests | 61 |
| Passed | 61 |
| Failed | 0 |
| Skipped | 4 (fuzzer tests) |
| Pass Rate | **100%** |
| Test Duration | 50.58 seconds |

### Skipped Tests

The following tests are intentionally skipped (fuzzer-only):
- test_connect_null_fuzzer
- test_bind_null_fuzzer
- test_connect_fuzzer
- test_bind_fuzzer

### Key Test Categories

All socket patterns tested successfully:

| Category | Tests | Status |
|----------|-------|--------|
| Transport Matrix | test_transport_matrix | PASS |
| ASIO TCP | test_asio_tcp | PASS |
| ASIO SSL | test_asio_ssl | PASS |
| ASIO WebSocket | test_asio_ws | PASS |
| ASIO Poller | test_asio_poller | PASS |
| ASIO Connect | test_asio_connect | PASS |
| PUB/SUB Filter | test_pubsub_filter_xpub | PASS |
| Router Multiple Dealers | test_router_multiple_dealers | PASS |

## Performance Baseline

### Configuration

- **Iterations:** 3 runs per test (min/max trimmed)
- **CPU Affinity:** `taskset -c 0`
- **Message Count:** 200,000 (small), 20,000 (large)
- **Transports:** TCP, IPC, inproc

### PAIR Pattern

#### TCP Transport

| Size | Throughput (M/s) | Latency (us) |
|------|------------------|--------------|
| 64B | 4.18 | 5.22 |
| 256B | 2.31 | 5.28 |
| 1024B | 0.87 | 5.36 |
| 64KB | 0.03 | 13.21 |
| 128KB | 0.02 | 18.30 |
| 256KB | 0.01 | 30.44 |

#### IPC Transport

| Size | Throughput (M/s) | Latency (us) |
|------|------------------|--------------|
| 64B | 4.24 | 4.71 |
| 256B | 2.65 | 5.10 |
| 1024B | 1.02 | 4.76 |
| 64KB | 0.03 | 13.28 |
| 128KB | 0.02 | 22.45 |
| 256KB | 0.01 | 36.40 |

#### inproc Transport

| Size | Throughput (M/s) | Latency (us) |
|------|------------------|--------------|
| 64B | 6.02 | 0.12 |
| 256B | 4.97 | 0.12 |
| 1024B | 3.12 | 0.12 |
| 64KB | 0.16 | 1.91 |
| 128KB | 0.11 | 3.49 |
| 256KB | 0.06 | 6.78 |

### PUB/SUB Pattern

#### TCP Transport

| Size | Throughput (M/s) | Latency (us) |
|------|------------------|--------------|
| 64B | 3.82 | 0.26 |
| 256B | 2.29 | 0.44 |
| 1024B | 0.85 | 1.17 |
| 64KB | 0.03 | 34.41 |
| 128KB | 0.02 | 54.67 |
| 256KB | 0.01 | 100.42 |

#### IPC Transport

| Size | Throughput (M/s) | Latency (us) |
|------|------------------|--------------|
| 64B | 3.35 | 0.30 |
| 256B | 1.98 | 0.50 |
| 1024B | 0.79 | 1.27 |
| 64KB | 0.03 | 33.08 |
| 128KB | 0.02 | 58.06 |
| 256KB | 0.01 | 108.32 |

#### inproc Transport

| Size | Throughput (M/s) | Latency (us) |
|------|------------------|--------------|
| 64B | 5.43 | 0.18 |
| 256B | 4.86 | 0.21 |
| 1024B | 2.71 | 0.37 |
| 64KB | 0.15 | 6.81 |
| 128KB | 0.10 | 9.66 |
| 256KB | 0.06 | 15.59 |

### DEALER/ROUTER Pattern

#### TCP Transport

| Size | Throughput (M/s) | Latency (us) |
|------|------------------|--------------|
| 64B | 2.99 | 5.43 |
| 256B | 2.00 | 5.34 |
| 1024B | 0.77 | 5.45 |
| 64KB | 0.03 | 12.42 |
| 128KB | 0.02 | 18.67 |
| 256KB | 0.01 | 30.34 |

#### IPC Transport

| Size | Throughput (M/s) | Latency (us) |
|------|------------------|--------------|
| 64B | 3.03 | 5.02 |
| 256B | 2.14 | 5.00 |
| 1024B | 0.85 | 5.50 |
| 64KB | 0.03 | 13.35 |
| 128KB | 0.02 | 21.89 |
| 256KB | 0.01 | 38.21 |

#### inproc Transport

| Size | Throughput (M/s) | Latency (us) |
|------|------------------|--------------|
| 64B | 4.97 | 0.16 |
| 256B | 4.38 | 0.17 |
| 1024B | 2.56 | 0.19 |
| 64KB | 0.15 | 1.93 |
| 128KB | 0.10 | 3.63 |
| 256KB | 0.06 | 7.15 |

## Key Performance Metrics for Phase Comparison

### Primary Metrics (64B Messages)

| Pattern | Transport | Throughput | Latency |
|---------|-----------|------------|---------|
| PAIR | TCP | 4.18 M/s | 5.22 us |
| PAIR | IPC | 4.24 M/s | 4.71 us |
| PAIR | inproc | 6.02 M/s | 0.12 us |
| PUBSUB | TCP | 3.82 M/s | 0.26 us |
| PUBSUB | IPC | 3.35 M/s | 0.30 us |
| PUBSUB | inproc | 5.43 M/s | 0.18 us |
| DEALER/ROUTER | TCP | 2.99 M/s | 5.43 us |
| DEALER/ROUTER | IPC | 3.03 M/s | 5.02 us |
| DEALER/ROUTER | inproc | 4.97 M/s | 0.16 us |

### Acceptance Criteria for Phase 1-5

| Phase | Allowed Deviation (vs Baseline) |
|-------|--------------------------------|
| Phase 1 | +/- 5% |
| Phase 2 | +/- 8% (cumulative) |
| Phase 3 | +/- 10% (cumulative) |
| Phase 4 | +/- 10% (cumulative) |
| Phase 5 | +/- 10% (final) |

**Note:** If cumulative regression exceeds 10% at any phase, stop and analyze before proceeding.

## Binary Information

### Library Size

```
-rwxr-xr-x 1 ulalax ulalax 5.9M Jan 15 17:35 libzmq.so.5.2.5
```

| Metric | Value |
|--------|-------|
| Shared Library | 5.9 MB (libzmq.so.5.2.5) |
| Static Library | 14 MB (libzmq.a) |
| Build Type | Release |
| Strip Status | Not stripped |

### Disassembly Baseline

Hot path function disassembly captured for:
- `zmq::asio_engine_t::on_read_complete()` at 0x00000000000e7160
- `zmq::asio_ws_engine_t::on_read_complete()` at 0x0000000000289c50

Files:
- `baseline_disasm.txt` - Disassembly of on_read_complete function
- `baseline_binary_size.txt` - Binary size record

## ASIO/Boost Version

| Component | Version |
|-----------|---------|
| Boost | 1.85.0 |
| Boost.Asio | Bundled with Boost 1.85.0 |
| Minimum Required | 1.66.0 |
| Recommended | 1.70.0+ |

**Status:** PASS - Boost 1.85.0 exceeds minimum requirements.

## Benchmark Tools Verification

| Tool | Status |
|------|--------|
| `benchwithzmq/run_benchmarks.sh` | Verified |
| `benchwithzmq/run_comparison.py` | Verified |
| `benchwithzmq/analyze_results.py` | Available |
| CPU affinity (`taskset`) | Available |

## Phase 0 Checklist

- [x] Baseline performance data recorded
- [x] All tests pass (61/61, 4 skipped)
- [x] Code analysis document created (`code_analysis.md`)
- [x] Benchmark tools verified (1 run successful)
- [x] Boost/ASIO version confirmed (1.85.0)
- [x] Compiler optimization baseline extracted (disassembly, binary size)
- [x] `baseline_disasm.txt` created
- [x] `baseline_binary_size.txt` created

## Conclusion

Phase 0 is complete. All baseline measurements have been recorded and the codebase is ready for Phase 1 (Transport Layer conditional compilation removal).

**Next Steps:**
1. Create feature branch `phase1-transport-cleanup` from `feature/asio-only`
2. Remove standalone `ZMQ_IOTHREAD_POLLER_USE_ASIO` guards in `session_base.cpp`
3. Simplify feature-combination guards (keep `ZMQ_HAVE_*` only)
4. Run tests and measure performance after each change

---

**Last Updated:** 2026-01-15
**Status:** Phase 0 Complete
