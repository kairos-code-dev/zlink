# Performance Optimization Implementation Plan

## Scope
Target TCP/IPC regressions after ASIO-only transition by removing avoidable
copies and reducing buffer churn in the hot I/O paths.

## Guiding Principles
- Make the smallest changes that remove the highest-cost copies first.
- Keep correctness invariant: do not break handshake state or decoder/encoder
  buffer ownership.
- Measure after each step; do not stack multiple optimizations without
  verifying benefit.
- Each phase must have a rollback path (feature flag or compile-time toggle).
- **Test-first approach**: Write tests before implementation, verify all tests pass before proceeding.

## Progress Update (2026-01-13)
- Phase 0 complete: TCP socket buffers set to 512KB (baseline captured).
- Phase 1 complete: zero-copy active for large writes (>= 8KB); Phase 1 tests
  present (`test_asio_zerocopy`, `test_asio_partial_write`,
  `test_asio_error_recovery`).
- Phase 2 complete: lazy compaction implemented; compaction counters added;
  Phase 2 tests present (`test_asio_partial_read`, `test_asio_compaction`).
- Phase 3 complete: write batching with debug counters and full test coverage
  (timeout/size/count, large-message priority, timer cancel, strand safety,
  feature-flag off).
- Phase 4 complete: handshake input zero-copy optimization and boundary tests
  (TCP + IPC).
- Feature flags audited: runtime env toggles for batching/zerocopy/compaction/
  handshake enabled in ASIO engine.
- Tests: normal build passes 10 consecutive runs (68 tests, 4 fuzzers skipped).
  ASAN passes with per-test ASAN_OPTIONS. UBSAN passes with ASIO SSL/WS tests
  skipped under UBSAN due to a Boost.Asio misalignment crash.
- Benchmarks (`benchwithzmq/run_benchmarks.sh --with-libzmq --runs 3`) show
  mixed results: small-message TCP/IPC throughput is still down (often 20–40%)
  with much worse latency; inproc is near parity; large-payload throughput
  sometimes improves but latency remains worse.

## Remaining Work (2026-01-13)
- [ ] Investigate UBSAN crash in ASIO SSL/WS (Boost.Asio misalignment) and
      remove UBSAN skip guards once resolved.
- [ ] Optional: rerun benchmarks with CPU pinning and TCP_NODELAY matrix for
      cleaner comparisons.

## Existing Test Baseline

### Current Test Suite
| Category | Count | Files |
|----------|-------|-------|
| Test files | 66 | `tests/*.cpp` |
| Registered tests | ~61 | Actual ctest count |
| ASIO tests | 5 | `test_asio_connect.cpp`, `test_asio_poller.cpp`, `test_asio_ssl.cpp`, `test_asio_tcp.cpp`, `test_asio_ws.cpp` |
| Dealer/Router | 3 | `test_router_*.cpp` |
| IPC | 2 | `test_abstract_ipc.cpp`, `test_ipc_*.cpp` |

### Test Pass Criteria (All Phases)
```bash
# Build and run tests
./build-scripts/linux/build.sh x64 ON

# Or manually:
cd build/linux-x64 && ctest --output-on-failure
# Or (./build.sh users):
cd build && ctest --output-on-failure

# Expected result: ~61 tests passed, ~4 skipped (fuzzer tests)
# NOTE: Exact counts may vary - verify before each phase
```

### Pre-Phase Validation
Before starting ANY phase implementation:
1. Run full test suite - record exact pass/skip counts
2. Run benchmarks - document baseline
3. Commit baseline results

---

## Phase 0: Transport Tuning & Baseline (Pre-requisite)

### Step 0.1: Test Preparation
No new tests required. Verify existing tests pass.

```bash
# Verify all existing tests pass
./build-scripts/linux/build.sh x64 ON
# Or: cd build/linux-x64 && ctest --output-on-failure
# Or (./build.sh users): cd build && ctest --output-on-failure
```

### Step 0.2: Implementation
1) Increase `SO_SNDBUF`/`SO_RCVBUF` to 512KB for TCP sockets.
2) Read back effective buffer sizes via `getsockopt` and log them.

### Step 0.3: Validation
1) All existing tests must pass.
2) Run benchmarks to establish baseline metrics.
3) Measure with both `TCP_NODELAY` on and off.

### Problem
Default socket buffer sizes may be too small for high-throughput scenarios.
No baseline measurements exist for comparison.

### Implementation Details
- Add socket buffer size configuration in TCP transport.
- Run `taskset -c 1 benchwithzmq/run_benchmarks.sh --with-libzmq --runs 3` with CPU pinned.
- Document baseline throughput and CPU usage.
- **TCP_NODELAY Matrix**: Measure all benchmarks with Nagle on/off to inform
  transport tuning decisions in Phase 0/1.

### Latency Measurement Note
**Current limitation**: The benchmark suite only outputs average throughput.
To measure p50/p99 latency:
- Option A: Add histogram collection to benchmark code
- Option B: Use external tools (`perf`, `bpftrace`)
- Option C: Defer latency percentiles to later phases

For Phase 0, focus on throughput baseline. Latency percentiles are nice-to-have.

### Benchmark Hygiene
- Fix CPU governor to `performance` mode.
- Pin both sender and receiver to specific cores.
- Isolate cores (disable SMT sibling if possible).
- Note NIC offloads (TSO/GRO) or loopback path.
- For IPC, document platform-specific socket buffer semantics.

### Files
- `src/asio/tcp_transport.cpp` - TCP socket buffer configuration
- `src/ip.cpp` - IP-level socket options

### IPC Note
IPC transport uses Unix domain sockets via:
- `src/asio/asio_ipc_connecter.cpp`
- `src/asio/asio_ipc_listener.cpp`
- `src/ipc_address.cpp`

Socket buffer tuning for IPC may have different semantics per platform.
Phase 0 focuses on TCP; IPC tuning can be evaluated separately.

### Acceptance Criteria
- [ ] All existing tests pass (record exact count)
- [ ] Baseline throughput documented
- [ ] TCP_NODELAY on/off comparison documented
- [ ] Socket buffer tuning shows measurable improvement (or no regression)

---

## Phase 1: Write Path Zero-Copy (Highest Priority)

### Step 1.1: Test Preparation (BEFORE Implementation)

#### New Test Files to Create
```
tests/test_asio_zerocopy.cpp       - Zero-copy write path verification
tests/test_asio_partial_write.cpp  - Partial write scenarios
tests/test_asio_error_recovery.cpp - Error path testing (shared with Phase 2)
```

#### test_asio_zerocopy.cpp
```cpp
// Test cases to implement:
// 1. test_zerocopy_large_message - Verify large messages use zero-copy path
// 2. test_zerocopy_encoder_buffer_lifetime - Sentinel buffer overwrite detection
// 3. test_zerocopy_fallback_handshake - Verify fallback during handshake
// 4. test_zerocopy_scatter_gather - Verify header+body sent without merge
// 5. test_zerocopy_feature_flag_off - Verify fallback when flag disabled
```

#### test_asio_partial_write.cpp
```cpp
// Test cases to implement:
// 1. test_partial_write_small_buffer - Force partial writes with small SO_SNDBUF
// 2. test_partial_write_offset_advance - Verify offset correctly advances
// 3. test_partial_write_completion - Verify completion only after all bytes sent
// 4. test_partial_write_multiple_chunks - Large message split into multiple writes
```

#### test_asio_error_recovery.cpp (partial)
```cpp
// Test cases to implement:
// 1. test_error_econnreset - Verify cleanup on connection reset
// 2. test_error_epipe - Verify cleanup on broken pipe
// 3. test_error_cancellation - Verify state restoration on cancel
// 4. test_error_no_leak - ASAN verification of no memory leaks
// 5. test_error_no_double_free - Verify no double release of buffers
```

#### Debug Counter Verification in Tests
To verify "no memcpy" without runtime instrumentation:
```cpp
// Option 1: Expose counters via test API
extern "C" {
    size_t zmq_debug_get_zerocopy_count();
    size_t zmq_debug_get_fallback_count();
    size_t zmq_debug_get_bytes_copied();
}

void test_zerocopy_large_message() {
    size_t initial_copies = zmq_debug_get_bytes_copied();
    // ... send large message ...
    size_t final_copies = zmq_debug_get_bytes_copied();
    TEST_ASSERT_EQUAL(initial_copies, final_copies);  // No new copies
}

// Option 2: Use sentinel pattern
// Write sentinel value after encoder buffer, verify not overwritten
// (indicates buffer was used directly, not copied)
```
**Note:** Debug counters must be compiled only in test builds
(`ZMQ_BUILD_TESTS`/`ZMQ_TEST_HOOKS`) to avoid ABI exposure in release builds.

### Step 1.2: Implementation

**IMPORTANT: Transport Interface Change Required**

Current `i_asio_transport::async_write_some` signature:
```cpp
virtual void async_write_some(const unsigned char *buffer,
                              std::size_t buffer_size,
                              completion_handler_t handler) = 0;
```

For scatter-gather support, add overload:
```cpp
// New: scatter-gather write
virtual void async_write_some(
    const std::vector<boost::asio::const_buffer>& buffers,
    completion_handler_t handler) = 0;

// Or use ConstBufferSequence template (more flexible but complex)
template<typename ConstBufferSequence>
void async_write(const ConstBufferSequence& buffers,
                 completion_handler_t handler);
```

**Files requiring interface change:**
- `src/asio/i_asio_transport.hpp` - Add scatter-gather method
- `src/asio/tcp_transport.cpp` - Implement scatter-gather
- `src/asio/ssl_transport.cpp` - Implement scatter-gather
- `src/asio/ws_transport.cpp` - Keep single-buffer writes (frame-based)
- `src/asio/wss_transport.cpp` - Keep single-buffer writes (frame-based)

**Implementation order:**
1) Add scatter-gather to `i_asio_transport` interface
2) Implement in `tcp_transport` first (simplest case)
3) Introduce Buffer Pinning API for encoder
4) Update `asio_engine` to use new interface
5) Add debug counters
6) Extend to SSL/WS transports

### Buffer Pinning API Design
```cpp
// Encoder interface additions
struct encoder_cookie { /* opaque handle */ };

bool acquire_out_buffer(const uint8_t*& ptr, size_t& len, encoder_cookie& h);
void release_out_buffer(encoder_cookie h, size_t bytes_consumed);

// Optional: scatter-gather variant
bool acquire_out_buffers(std::vector<boost::asio::const_buffer>& seq, encoder_cookie& h);
```

### Step 1.3: Validation
```bash
# 1. All existing tests must pass
cd build/linux-x64 && ctest --output-on-failure
# Or (./build.sh users):
cd build && ctest --output-on-failure

# 2. All new Phase 1 tests must pass
ctest -R "zerocopy|partial_write|error_recovery" --output-on-failure

# 3. ASAN/UBSAN check
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON ..
make && ctest --output-on-failure

# 4. Verify debug counters
# Run test that checks zmq_debug_get_bytes_copied() == 0 for large messages

# 5. Benchmark comparison
benchwithzmq/run_benchmarks.sh --with-libzmq --runs 3
```

### Problem
`src/asio/asio_engine.cpp::process_output()` always copies encoder output into
`_write_buffer` via `memcpy`. This adds one full copy for every write batch,
especially expensive for large messages.

### Implementation Steps
- **Step 1.2.1**: Extend `i_asio_transport` interface for scatter-gather
- **Step 1.2.2**: Implement scatter-gather in `tcp_transport`
- **Step 1.2.3**: Define buffer pinning contract in encoder interface
- **Step 1.2.4**: Add debug counters (compile-time gated)
- **Step 1.2.5**: Update `asio_engine` to use zero-copy path
- **Step 1.2.6**: Add feature flag `ZMQ_ASIO_ZEROCOPY_WRITE`
- **Step 1.2.7**: Extend to SSL/WS transports

### Error Recovery Policy
| Error Type | Action |
|------------|--------|
| `EAGAIN` / `EWOULDBLOCK` | Retry within strand, preserve offsets |
| `EINTR` | Retry immediately |
| `ECONNRESET` / `EPIPE` | Abort pipe, release buffer safely, propagate to session |
| `ETIMEDOUT` | Abort pipe, release buffer, propagate to session |
| Cancellation | Release buffer, restore state to "no write pending", let upper layer decide |

### Files
- `src/asio/i_asio_transport.hpp` - **Interface change for scatter-gather**
- `src/asio/tcp_transport.cpp` - Scatter-gather implementation
- `src/asio/tcp_transport.hpp` - Scatter-gather declaration
- `src/asio/ssl_transport.cpp` - Scatter-gather implementation
- `src/asio/asio_engine.cpp` - Zero-copy write path
- `src/asio/asio_engine.hpp` - State flags, debug counters
- `src/encoder.hpp` - Buffer pinning interface

### Risks
- **Encoder buffer lifetime**: Mitigated by buffer pinning API.
- **Handshake logic**: Must preserve existing behavior.
- **Partial write handling**: Must be correct to avoid data corruption.
- **Pipeline stalling**: Monitor in benchmarks.
- **Interface change**: Scatter-gather requires updating all transport implementations.

### Threading/Strand Guarantee
- Document that all I/O operations run on the same strand.
- Add assertions to verify strand context in debug builds.

### Debug Counters
```cpp
// In asio_engine.hpp (gated by ZMQ_DEBUG_COUNTERS)
#ifdef ZMQ_DEBUG_COUNTERS
struct asio_debug_counters_t {
    std::atomic<size_t> zerocopy_write_count{0};
    std::atomic<size_t> fallback_write_count{0};
    std::atomic<size_t> scatter_gather_write_count{0};
    std::atomic<size_t> partial_write_count{0};
    std::atomic<size_t> bytes_copied{0};
};

// Expose for testing
extern "C" size_t zmq_debug_get_zerocopy_count();
extern "C" size_t zmq_debug_get_bytes_copied();
#endif
```

### Acceptance Criteria
- [ ] All existing tests pass
- [ ] All new Phase 1 tests pass (zerocopy, partial_write, error_recovery)
- [ ] ASAN reports no leaks or buffer overflows
- [ ] UBSAN reports no undefined behavior
- [ ] Debug counters confirm zero-copy path is active (`bytes_copied == 0` for large msgs)
- [ ] Throughput improves on TCP at large sizes (>64KB) by at least 5%
- [ ] Feature flag allows rollback to previous behavior

---

## Phase 2: Read Path Lazy Compaction (Memmove Reduction)

### Step 2.1: Test Preparation (BEFORE Implementation)

#### New Test Files to Create
```
tests/test_asio_partial_read.cpp   - Partial read and compaction scenarios
tests/test_asio_compaction.cpp     - Lazy compaction threshold testing
```

#### test_asio_partial_read.cpp
```cpp
// Test cases to implement:
// 1. test_partial_read_fragment - Read leaves tail fragment in buffer
// 2. test_partial_read_decoder_contiguity - Verify decoder gets contiguous view
// 3. test_partial_read_large_message - Message larger than buffer handling
// 4. test_partial_read_multiple_messages - Multiple small messages in buffer
```

#### test_asio_compaction.cpp
```cpp
// Test cases to implement:
// 1. test_compaction_threshold_not_met - No memmove when thresholds not met
// 2. test_compaction_threshold_met - memmove only when both thresholds met
// 3. test_compaction_counter - Verify compaction count matches expectations
// 4. test_compaction_pointer_consistency - All pointers updated after compact
// 5. test_compaction_feature_flag_off - Always compact when flag disabled

// Counter verification:
void test_compaction_counter() {
    size_t initial = zmq_debug_get_compaction_count();
    // ... trigger scenario where compaction should NOT occur ...
    TEST_ASSERT_EQUAL(initial, zmq_debug_get_compaction_count());

    // ... trigger scenario where compaction SHOULD occur ...
    TEST_ASSERT_EQUAL(initial + 1, zmq_debug_get_compaction_count());
}
```

### Step 2.2: Implementation
1) Replace unconditional `memmove` with **Lazy Compaction (Watermark)** strategy.
2) Keep decoder compatibility with contiguous buffer requirement.

### Step 2.3: Validation
```bash
# 1. All existing tests must pass
ctest --output-on-failure

# 2. All new Phase 2 tests must pass
ctest -R "partial_read|compaction" --output-on-failure

# 3. All Phase 1 tests still pass
ctest -R "zerocopy|partial_write|error_recovery" --output-on-failure

# 4. ASAN/UBSAN check
ctest --output-on-failure  # with sanitizers enabled

# 5. Verify debug counters show reduced compaction
# compaction_skipped_count should be >> compaction_count

# 6. Benchmark comparison
benchwithzmq/run_benchmarks.sh --with-libzmq --runs 3
```

### Problem
`start_async_read()` does `memmove` to shift partial data to the buffer start.
For large messages and partial reads, this can repeatedly move large blocks.

### Threshold Formula
```cpp
bool need_compaction =
    (_read_pos > _read_consumed_threshold) &&  // e.g., 2048 bytes consumed
    (remaining_space < std::max(2048, buffer_size * 0.2));  // combined absolute + relative
```

### Implementation Steps
- Add watermark thresholds to `asio_engine_t`:
  - `_read_compact_threshold` (e.g., buffer_size * 0.2)
  - `_read_consumed_threshold` (e.g., 2048 bytes)
  - Make thresholds configurable via socket options for tuning.
- Modify `start_async_read()`:
  - Check if compaction is needed before memmove.
  - Only memmove when threshold conditions are met.
- Add feature flag: `ZMQ_ASIO_LAZY_COMPACT` for rollback.
- Add safety assertions.

### Large Frame Handling
- If a single frame can exceed buffer capacity, define slow-path:
  - Temporary buffer grow or allocation.
  - Avoid repeated compactions for oversized frames.

### Debug Counters
```cpp
#ifdef ZMQ_DEBUG_COUNTERS
    std::atomic<size_t> compaction_count{0};
    std::atomic<size_t> compaction_bytes{0};
    std::atomic<size_t> compaction_skipped_count{0};
#endif
```

### Files
- `src/asio/asio_engine.cpp`
- `src/asio/asio_engine.hpp`

### Risks
- Decoder expectations around buffer contiguity (mitigated by keeping linear buffer).
- Edge cases with very small buffers or very large messages.

### Acceptance Criteria
- [ ] All existing tests pass
- [ ] All Phase 1 tests still pass
- [ ] All new Phase 2 tests pass (partial_read, compaction)
- [ ] ASAN reports no leaks or buffer overflows
- [ ] UBSAN reports no undefined behavior
- [ ] `memmove` calls reduced by 90%+ (verified via debug counters)
- [ ] Debug counters confirm: `compaction_skipped_count >> compaction_count`

---

## Phase 3: Small Message Batching (Syscall Reduction)

### Step 3.1: Test Preparation (BEFORE Implementation)

#### New Test Files to Create
```
tests/test_asio_batching.cpp       - Batching behavior testing
```

#### test_asio_batching.cpp
```cpp
// Test cases to implement:
// 1. test_batching_size_limit - Flush when max_bytes reached
// 2. test_batching_count_limit - Flush when max_messages reached
// 3. test_batching_timeout - Flush when timer expires
// 4. test_batching_large_message_priority - Large message flushes pending batch
// 5. test_batching_timer_cancellation - Timer cancelled on early flush
// 6. test_batching_strand_safety - No race between timer and write completion
// 7. test_batching_feature_flag_off - No batching when flag disabled
// 8. test_batching_tcp_nodelay_interaction - Verify behavior with Nagle on/off
```

### Step 3.2: Implementation
1) Batch multiple small messages into single `async_write` call.
2) Implement flush policy with timer.
3) Handle timer/strand synchronization.

### Step 3.3: Validation
```bash
# 1. All existing tests must pass
ctest --output-on-failure

# 2. All new Phase 3 tests must pass
ctest -R "batching" --output-on-failure

# 3. All Phase 1 & 2 tests still pass
ctest -R "zerocopy|partial_write|error_recovery|partial_read|compaction" --output-on-failure

# 4. Benchmark comparison
benchwithzmq/run_benchmarks.sh --with-libzmq --runs 3
```

### Problem
For small messages sent in bursts, each `async_write` call incurs syscall
overhead and context switching costs.

### Flush Policy
Flush batch on ANY of these conditions:
- Reaching `_write_batch_max_bytes` (e.g., 64KB)
- Reaching `_write_batch_max_messages` (e.g., 100)
- Timer elapsed (`_write_batch_timeout_us`, e.g., 100us)
- Encoder indicates no more immediate data (inter-batch gap)
- Large message arrives (don't block big frames behind small-batch timer)

### Timer/Strand Considerations
- Use a single `boost::asio::steady_timer` owned by the strand.
- Guard against cancellation races:
  - When write completes before timer fires.
  - When batch fills before timer fires.
- Cancel timer explicitly when batch is flushed early.
- **Important**: Timer callback must check if batch was already flushed.

### Implementation Steps
- Add batching parameters:
  - `_write_batch_max_bytes` (e.g., 64KB)
  - `_write_batch_max_messages` (e.g., 100)
  - `_write_batch_timeout_us` (e.g., 100us)
- Add `_batch_timer` (steady_timer) to `asio_engine_t`.
- Modify `process_output()` to accumulate encoder output up to batch limits.
- Use `ConstBufferSequence` to send batched data in single syscall.
- Large message fairness: flush existing batch before sending large frame.
- Add feature flag: `ZMQ_ASIO_WRITE_BATCHING`.

### TCP_NODELAY Interaction
- Batching is most beneficial with Nagle OFF (TCP_NODELAY=1).
- With Nagle ON, kernel already batches small writes.
- Phase 0 baseline matrix informs this decision.

### Debug Counters
- `batch_flush_count`: Number of batch flushes
- `batch_timeout_flush_count`: Flushes triggered by timer
- `batch_size_flush_count`: Flushes triggered by size/count limit
- `messages_per_batch_avg`: Average messages per batch

### Files
- `src/asio/asio_engine.cpp`
- `src/asio/asio_engine.hpp`

### Acceptance Criteria
- [ ] All existing tests pass
- [ ] All Phase 1 & 2 tests still pass
- [ ] All new Phase 3 tests pass (batching)
- [ ] Throughput improves for small message workloads (1KB messages)
- [ ] Syscall count per message decreases
- [ ] Latency impact is within acceptable bounds

---

## Phase 4: Handshake Buffer Ownership (Low Priority)

### Step 4.1: Test Preparation (BEFORE Implementation)

#### New Test Files to Create
```
tests/test_asio_handshake_boundary.cpp - Handshake boundary conditions
```

#### test_asio_handshake_boundary.cpp
```cpp
// Test cases to implement (NULL mechanism only - PLAIN/CURVE excluded initially):
// 1. test_handshake_null_mechanism - NULL mechanism boundary on TCP
// 2. test_handshake_null_ipc - NULL mechanism on IPC transport
// 3. test_handshake_buffer_no_copy - Verify reduced memcpy during handshake
// 4. test_handshake_fallback - Verify fallback when optimization not applicable
//
// NOTE: PLAIN mechanism tests deferred until NULL is validated
// CURVE is not supported in zlink (use TLS instead)
```

### Step 4.2: Implementation
1) Ensure handshake buffers are compatible with decoder directly.
2) Avoid the "external buffer copy into decoder buffer" step.

### Step 4.3: Validation
```bash
# 1. All existing tests must pass
ctest --output-on-failure

# 2. All new Phase 4 tests must pass
ctest -R "handshake_boundary" --output-on-failure

# 3. All Phase 1, 2 & 3 tests still pass
ctest -R "zerocopy|partial_write|error_recovery|partial_read|compaction|batching" --output-on-failure
```

### Problem
Decoder buffer copy occurs when data comes from handshake/internal buffers.

### Implementation Steps
- Use decoder-provided buffers during handshake instead of `_read_buffer`.
- Adjust handshake readers to write into decoder buffer directly.
- Limit scope to TCP/IPC; exclude WS/WSS/TLS paths initially.
- **Start with NULL mechanism only** - PLAIN deferred, CURVE not supported in zlink.

### Files
- `src/asio/asio_engine.cpp`
- `src/asio/asio_zmtp_engine.cpp`

### Transport Scope
- **Phase 1-3: TCP first**, then extend to SSL/WS/WSS after TCP validation.
- Phase 4: TCP only; WS/WSS/TLS require separate evaluation.
- IPC: Evaluated separately after TCP optimizations complete.

**Implementation Order for Phase 1:**
1. TCP transport (primary target)
2. SSL transport (after TCP validated)
3. WS/WSS transport (optional, if applicable)

### Acceptance Criteria
- [ ] All existing tests pass
- [ ] All Phase 1, 2 & 3 tests still pass
- [ ] All new Phase 4 tests pass (handshake_boundary)
- [ ] Fewer `memcpy` in handshake transitions
- [ ] No impact on WS/WSS/TLS paths

---

## Rollback & Feature Flags

| Phase | Feature Flag | Default | Notes |
|-------|--------------|---------|-------|
| Phase 1 | `ZMQ_ASIO_ZEROCOPY_WRITE` | **OFF** | Enable in CI first, then default ON after validation |
| Phase 2 | `ZMQ_ASIO_LAZY_COMPACT` | **OFF** | Enable in CI first, then default ON after validation |
| Phase 3 | `ZMQ_ASIO_WRITE_BATCHING` | OFF | Opt-in, requires latency trade-off consideration |
| Phase 4 | `ZMQ_ASIO_HANDSHAKE_ZEROCOPY` | OFF | High risk, requires per-mechanism testing |

### Toggle Types
- **Compile-time**: `#ifdef ZMQ_ASIO_ZEROCOPY_WRITE`
- **Runtime**: Socket option `ZMQ_ASIO_ZEROCOPY_WRITE` (int, 0/1)
- **Environment**: `ZMQ_ASIO_ZEROCOPY_WRITE=1` for global override

### Toggle Priority (Highest to Lowest)
1. **Environment variable** - Global override for testing/debugging
2. **Runtime socket option** - Per-socket control
3. **Compile-time default** - Fallback if not set

```cpp
// Resolution logic
bool is_zerocopy_enabled() {
    // 1. Check environment (highest priority)
    const char* env = getenv("ZMQ_ASIO_ZEROCOPY_WRITE");
    if (env) return strcmp(env, "1") == 0;

    // 2. Check runtime socket option
    if (_zerocopy_option_set)
        return _zerocopy_enabled;

    // 3. Use compile-time default
    #ifdef ZMQ_ASIO_ZEROCOPY_WRITE_DEFAULT
        return true;
    #else
        return false;
    #endif
}
```

---

## Test Summary

### New Test Files by Phase

| Phase | Test File | Test Count | Purpose |
|-------|-----------|------------|---------|
| 1 | `test_asio_zerocopy.cpp` | 5 | Zero-copy write verification |
| 1 | `test_asio_partial_write.cpp` | 4 | Partial write handling |
| 1 | `test_asio_error_recovery.cpp` | 5 | Error path testing |
| 2 | `test_asio_partial_read.cpp` | 4 | Partial read handling |
| 2 | `test_asio_compaction.cpp` | 5 | Lazy compaction verification |
| 3 | `test_asio_batching.cpp` | 8 | Batching behavior |
| 4 | `test_asio_handshake_boundary.cpp` | 4 | Handshake boundaries (NULL only) |
| **Total** | **7 new files** | **35 new tests** | |

### Cumulative Test Pass Requirements

| Phase | Existing Tests | New Tests | Total |
|-------|----------------|-----------|-------|
| 0 | ~61 pass | 0 | ~61 |
| 1 | ~61 pass | 14 | ~75 |
| 2 | ~61 pass | 9 | ~84 |
| 3 | ~61 pass | 8 | ~92 |
| 4 | ~61 pass | 4 | ~96 |

*Note: Exact existing test count should be verified at Phase 0*

---

## CI Integration

### Test Pipeline Per Phase
```yaml
# .github/workflows/phase-validation.yml
jobs:
  test:
    steps:
      - name: Build
        run: ./build-scripts/linux/build.sh x64 ON

      - name: Run existing tests
        run: cd build/linux-x64 && ctest --output-on-failure

      - name: Run phase-specific tests
        run: cd build/linux-x64 && ctest -R "$PHASE_TEST_PATTERN" --output-on-failure

      - name: ASAN check
        run: |
          cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
          cmake --build build-asan
          cd build-asan && ctest --output-on-failure

      - name: UBSAN check
        run: |
          cmake -B build-ubsan -DCMAKE_BUILD_TYPE=Debug -DENABLE_UBSAN=ON
          cmake --build build-ubsan
          cd build-ubsan && ctest --output-on-failure

      - name: Benchmark
        run: benchwithzmq/run_benchmarks.sh --with-libzmq --runs 3
```

### Phase Gate Criteria
Each phase MUST pass before proceeding to next:
1. ✅ All existing tests pass
2. ✅ All previous phase tests pass
3. ✅ All current phase tests pass
4. ✅ ASAN reports clean
5. ✅ UBSAN reports clean
6. ✅ Benchmark shows expected improvement (or no regression)
7. ✅ Debug counters confirm optimization paths are active

---

## Measurement Plan

### Benchmarks
Run with CPU pinned:
```bash
benchwithzmq/run_benchmarks.sh --with-libzmq --runs 3
```

### Target Metrics
| Metric | Phase 0 Baseline | Target |
|--------|------------------|--------|
| TCP 65536B throughput | TBD | +10% |
| TCP 131072B throughput | TBD | +15% |
| CPU usage (large msg) | TBD | -10% |
| memmove calls/msg | TBD | -90% (via counters) |
| memcpy bytes/msg | TBD | -80% (via counters, Phase 1) |
| syscalls/msg (small) | TBD | -50% (Phase 3) |

### Latency Percentiles (Future Enhancement)
Current benchmarks output averages only. To add p50/p99:
```cpp
// Add to benchmark code:
std::vector<double> latencies;
// ... collect per-message latencies ...
std::sort(latencies.begin(), latencies.end());
double p50 = latencies[latencies.size() * 0.50];
double p99 = latencies[latencies.size() * 0.99];
```

### Benchmark Methodology
- Warmup: 1000 messages before measurement.
- CPU affinity: Pin to specific cores.
- Runs: Minimum 3 runs, report median.
- Variance: Report stddev, flag if >5%.
- Report CPU utilization (user/sys).
- Report memmove/memcpy counts from debug counters.
- Record kernel and Boost.Asio versions.

### Benchmark Environment Policy
- **CI benchmarks**: Use for **regression detection** only (flag >10% degradation)
- **Official performance numbers**: Measure on **dedicated physical hardware**
  - Avoid virtualized/containerized environments for final numbers
  - Document hardware specs (CPU, RAM, NIC)
- CI variance is expected due to shared resources; don't block on small fluctuations

---

## Memory Pressure Considerations
- Zero-copy extends buffer lifetime until write completion.
- Peak memory usage may increase under high HWM conditions.
- Monitor memory usage in stress tests.
- Document HWM/backpressure interaction.

---

## Observability & Debug Counters Summary

| Counter | Phase | Purpose | Test Verification |
|---------|-------|---------|-------------------|
| `zerocopy_write_count` | 1 | Verify zero-copy path active | Assert > 0 for large msgs |
| `fallback_write_count` | 1 | Monitor fallback rate | Assert == 0 for large msgs |
| `scatter_gather_write_count` | 1 | Verify scatter-gather usage | Assert > 0 when enabled |
| `partial_write_count` | 1 | Track partial writes | N/A |
| `bytes_copied` | 1 | Should approach 0 | Assert == 0 for large msgs |
| `compaction_count` | 2 | Track memmove frequency | Assert reduced |
| `compaction_bytes` | 2 | Track memmove volume | Assert reduced |
| `compaction_skipped_count` | 2 | Verify lazy compaction | Assert >> compaction_count |
| `batch_flush_count` | 3 | Track batching activity | N/A |
| `batch_timeout_flush_count` | 3 | Timer-triggered flushes | N/A |
| `messages_per_batch_avg` | 3 | Batching efficiency | Assert > 1 when enabled |

### Counter API for Tests
```cpp
// src/zmq_debug.h (only when ZMQ_DEBUG_COUNTERS defined)
extern "C" {
    size_t zmq_debug_get_zerocopy_count();
    size_t zmq_debug_get_fallback_count();
    size_t zmq_debug_get_bytes_copied();
    size_t zmq_debug_get_compaction_count();
    size_t zmq_debug_get_compaction_skipped_count();
    void zmq_debug_reset_counters();
}
```

### Debug Counter Header Policy
- **Header location**: `src/zmq_debug.h` (internal, not in `include/`)
- **Visibility**: Only accessible to `tests/` via CMake `target_include_directories`
- **Build flag**: `ZMQ_DEBUG_COUNTERS` - enabled only in Debug builds and CI
- **Production builds**: Header not included, counters compiled out
- **Linkage**: Counters are weak symbols or conditionally linked to avoid runtime overhead

```cmake
# In tests/CMakeLists.txt
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR ENABLE_DEBUG_COUNTERS)
    target_compile_definitions(test_target PRIVATE ZMQ_DEBUG_COUNTERS)
    target_include_directories(test_target PRIVATE ${CMAKE_SOURCE_DIR}/src)
endif()
```

---

## Rollout Order (Recommended)

```
Phase 0: Socket Tuning + Baseline
├── Verify: Record exact test pass count
├── Benchmark: Document baseline throughput
└── Gate: Proceed only if all pass

Phase 1: Write Zero-Copy
├── Write: 14 new tests (zerocopy, partial_write, error_recovery)
├── Implement: Transport interface change, buffer pinning, scatter-gather
├── Verify: All tests pass + debug counters confirm zero-copy
├── Verify: ASAN/UBSAN clean
├── Benchmark: Compare to baseline
└── Gate: Proceed only if all pass

Phase 2: Read Lazy Compaction
├── Write: 9 new tests (partial_read, compaction)
├── Implement: Lazy compaction with thresholds
├── Verify: All tests pass + debug counters confirm reduced memmove
├── Verify: ASAN/UBSAN clean
├── Benchmark: Compare to Phase 1
└── Gate: Proceed only if all pass

Phase 3: Write Batching
├── Write: 8 new tests (batching)
├── Implement: Batching with timer
├── Verify: All tests pass
├── Benchmark: Compare to Phase 2
└── Gate: Proceed only if all pass

Phase 4: Handshake Buffer (Optional)
├── Write: 5 new tests (handshake_boundary)
├── Implement: Handshake optimization
├── Verify: All tests pass
└── Benchmark: Compare to Phase 3
```

---

## Validation Checklist

### Per-Phase Checklist
- [ ] All existing tests pass
- [ ] All previous phase tests pass
- [ ] All current phase tests pass
- [ ] ASAN reports no leaks or buffer overflows
- [ ] UBSAN reports no undefined behavior
- [ ] Benchmark shows expected improvement
- [ ] Feature flag allows rollback
- [ ] Debug counters confirm optimization paths are active

### Final Validation
- [ ] All ~96 tests pass (existing + new)
- [ ] No WS/WSS/TLS regressions
- [ ] Memory usage within acceptable bounds
- [ ] Performance targets met

---

## Future Work

### IPC Optimization (Post-TCP)
After TCP optimizations are validated (Phase 0-4 complete):
1. Profile IPC transport to identify bottlenecks
2. Apply relevant optimizations (scatter-gather may not apply to Unix sockets)
3. IPC-specific tuning (e.g., `SO_SNDBUF`/`SO_RCVBUF` behavior differs on Unix)

### Code Stabilization & Technical Debt Cleanup
After all phases are stable in production:
1. **Remove feature flags**: Change defaults to ON, remove toggle code
2. **Remove legacy code paths**: Delete fallback implementations
3. **Clean up debug counters**: Move to optional build or remove entirely
4. **Update documentation**: Reflect final optimized behavior

Timeline: 2-3 release cycles after Phase 4 completion

### SSL/WS/WSS Extension
After TCP-only phases are validated:
1. Extend scatter-gather to SSL transport
2. Evaluate WebSocket transport applicability
3. Add transport-specific tests
