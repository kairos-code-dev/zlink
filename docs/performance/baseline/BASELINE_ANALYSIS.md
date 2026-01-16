# zlink Baseline Performance Analysis

**Date**: 2026-01-15
**Branch**: feature/performance-optimization
**Commit**: 74c06e02 (post ASIO-only migration)
**Test Runs**: 10 iterations per configuration
**Total Tests**: 2,160 test runs (6 patterns × 3 transports × 6 sizes × 10 runs × 2 implementations)

## Executive Summary

This baseline establishes the performance characteristics of zlink (ASIO-based backend) compared to standard libzmq (traditional epoll/select backend) after the ASIO-only migration.

### Key Findings

**Critical Performance Gaps (Optimization Targets):**
- **ROUTER patterns**: -32% to -43% performance gap across all ROUTER-based patterns
  - DEALER_ROUTER: -32.30% (tcp), -31.98% (inproc), -34.66% (ipc)
  - ROUTER_ROUTER: -33.04% (tcp), -21.39% (inproc), -33.87% (ipc)
  - ROUTER_ROUTER_POLL: -33.91% (tcp), -22.69% (inproc), -34.53% (ipc)

**Moderate Performance Gaps:**
- **Simple patterns**: -11% to -16% performance gap
  - PAIR: -14.43% (tcp), -11.56% (inproc), -16.11% (ipc)
  - PUBSUB: -16.33% (tcp), -8.67% (inproc), -15.24% (ipc)
  - DEALER_DEALER: -12.58% (tcp), -14.15% (inproc), -14.07% (ipc)

**Message Size Impact:**
- Small messages (64B-1KB): Largest performance gaps (-11% to -43%)
- Large messages (64KB-256KB): Smaller gaps (-1% to -24%), some patterns near parity

**Transport Performance:**
- **inproc**: Best overall performance, smallest gaps for simple patterns (-8% to -14%)
- **tcp**: Moderate gaps for simple patterns (-12% to -16%), large gaps for ROUTER (-32% to -38%)
- **ipc**: Largest gaps across all patterns (-13% to -43%)

## Test Configuration

### Benchmark Parameters
- **Iterations**: 10 runs per test configuration
- **Message Count**: 1,000,000 messages per test
- **Warmup**: Included in each test run

### Socket Patterns Tested
1. **PAIR**: Bidirectional exclusive pair communication
2. **PUBSUB**: Publish-subscribe pattern (no topic filtering)
3. **DEALER_DEALER**: Async dealer-to-dealer communication
4. **DEALER_ROUTER**: Async dealer connecting to router
5. **ROUTER_ROUTER**: Bidirectional router communication
6. **ROUTER_ROUTER_POLL**: Router communication with explicit polling

### Transports Tested
1. **tcp**: TCP/IP loopback (127.0.0.1)
2. **inproc**: In-process shared memory
3. **ipc**: Unix domain sockets (/tmp/*.ipc)

### Message Sizes Tested
- 64 bytes (typical control messages)
- 256 bytes (small data packets)
- 1,024 bytes (1 KB - medium messages)
- 65,536 bytes (64 KB - large messages)
- 131,072 bytes (128 KB - large messages)
- 262,144 bytes (256 KB - very large messages)

## Detailed Performance Analysis

### Pattern 1: PAIR

**Throughput at 64B:**
- tcp: 4.78 M/s (libzmq) → 4.09 M/s (zlink) = **-14.43%**
- inproc: 6.02 M/s → 5.32 M/s = **-11.56%**
- ipc: 5.48 M/s → 4.60 M/s = **-16.11%**

**Characteristics:**
- Consistent performance gap across all transports (~11-16%)
- Best transport: inproc (smallest gap at -11.56%)
- Worst transport: ipc (-16.11%)
- Performance gap decreases with larger message sizes

**Analysis:**
Simple bidirectional communication pattern shows moderate performance regression. The gap is consistent, suggesting ASIO overhead in basic I/O operations rather than pattern-specific issues.

### Pattern 2: PUBSUB

**Throughput at 64B:**
- tcp: 4.41 M/s (libzmq) → 3.69 M/s (zlink) = **-16.33%**
- inproc: 6.06 M/s → 5.54 M/s = **-8.67%**
- ipc: 5.64 M/s → 4.78 M/s = **-15.24%**

**Characteristics:**
- inproc shows best performance (only -8.67% gap)
- tcp and ipc show similar gaps (~15-16%)
- Performance gap narrows significantly with larger messages

**Analysis:**
Publish-subscribe pattern performs reasonably well, especially on inproc transport. The smaller gap on inproc suggests ASIO handles in-process messaging more efficiently than cross-process communication.

### Pattern 3: DEALER_DEALER

**Throughput at 64B:**
- tcp: 4.73 M/s (libzmq) → 4.14 M/s (zlink) = **-12.58%**
- inproc: 6.10 M/s → 5.24 M/s = **-14.15%**
- ipc: 5.48 M/s → 4.71 M/s = **-14.07%**

**Characteristics:**
- Consistent gap across all transports (~12-14%)
- tcp shows smallest gap (-12.58%)
- Performance consistent across all message sizes

**Analysis:**
Dealer-to-dealer async communication shows moderate, consistent performance regression. Similar gap across all transports suggests this is ASIO-related overhead rather than transport-specific issues.

### Pattern 4: DEALER_ROUTER ⚠️ CRITICAL GAP

**Throughput at 64B:**
- tcp: 4.72 M/s (libzmq) → 3.20 M/s (zlink) = **-32.30%**
- inproc: 6.34 M/s → 4.31 M/s = **-31.98%**
- ipc: 5.83 M/s → 3.81 M/s = **-34.66%**

**Characteristics:**
- **Significant performance degradation** (>30% across all transports)
- ipc shows worst gap (-34.66%)
- Gap persists across all message sizes
- Large messages (256KB) show smaller gaps (~-1% to -24%)

**Analysis:**
**This is a critical optimization target.** The ROUTER socket pattern introduces significant overhead in the ASIO backend. The gap is consistent across transports, suggesting the issue is in ROUTER socket implementation rather than transport layer. Likely causes:
- Identity frame handling overhead
- Routing table management inefficiency
- Fair-queuing algorithm interaction with ASIO

### Pattern 5: ROUTER_ROUTER ⚠️ CRITICAL GAP

**Throughput at 64B:**
- tcp: 4.19 M/s (libzmq) → 2.80 M/s (zlink) = **-33.04%**
- inproc: 5.00 M/s → 3.93 M/s = **-21.39%**
- ipc: 4.27 M/s → 2.82 M/s = **-33.87%**

**Characteristics:**
- **Severe performance degradation** (>33% for tcp/ipc)
- inproc performs relatively better (-21.39%) but still significant
- Gap remains large even for medium-sized messages (1KB: ~-30% to -38%)
- Large messages (64KB+) show improvement (some near parity)

**Analysis:**
**Critical optimization target.** Router-to-router communication compounds the ROUTER socket inefficiencies. The better performance on inproc suggests the issue is exacerbated by cross-process communication. Priority areas:
1. ROUTER socket identity management
2. Bidirectional routing overhead
3. Message frame processing in ASIO context

### Pattern 6: ROUTER_ROUTER_POLL ⚠️ CRITICAL GAP

**Throughput at 64B:**
- tcp: 4.12 M/s (libzmq) → 2.72 M/s (zlink) = **-33.91%**
- inproc: 5.12 M/s → 3.96 M/s = **-22.69%**
- ipc: 4.24 M/s → 2.77 M/s = **-34.53%**

**Characteristics:**
- Similar to ROUTER_ROUTER pattern
- Explicit polling doesn't improve performance
- ipc shows worst gap (-34.53%)
- Performance gap consistent with ROUTER_ROUTER results

**Analysis:**
The addition of explicit polling (zmq_poll) doesn't improve ROUTER performance, confirming that the bottleneck is in the ROUTER socket implementation itself, not in the event notification mechanism.

## Transport Analysis

### TCP Transport
**Performance Summary:**
- PAIR: -14.43% (64B)
- PUBSUB: -16.33% (64B)
- DEALER_DEALER: -12.58% (64B)
- DEALER_ROUTER: -32.30% (64B) ⚠️
- ROUTER_ROUTER: -33.04% (64B) ⚠️
- ROUTER_ROUTER_POLL: -33.91% (64B) ⚠️

**Characteristics:**
- Simple patterns: ~12-16% overhead
- ROUTER patterns: ~32-34% overhead
- Consistent behavior across message sizes

**Analysis:**
TCP shows predictable overhead patterns. ASIO's proactor pattern adds ~12-16% overhead for simple socket types but ~32-34% for ROUTER sockets, suggesting the ROUTER implementation doesn't integrate well with ASIO's async I/O model.

### Inproc Transport
**Performance Summary:**
- PAIR: -11.56% (64B) ✓
- PUBSUB: -8.67% (64B) ✓ Best overall
- DEALER_DEALER: -14.15% (64B)
- DEALER_ROUTER: -31.98% (64B) ⚠️
- ROUTER_ROUTER: -21.39% (64B) ⚠️ Better than tcp/ipc
- ROUTER_ROUTER_POLL: -22.69% (64B) ⚠️

**Characteristics:**
- Best performance overall (smallest gaps)
- PUBSUB particularly efficient (-8.67%)
- ROUTER patterns still show significant gaps but less than tcp/ipc

**Analysis:**
Inproc (shared memory) shows the best performance characteristics. The smaller gaps suggest ASIO's overhead is less pronounced when no kernel context switches are involved. Even ROUTER patterns perform relatively better on inproc.

### IPC Transport
**Performance Summary:**
- PAIR: -16.11% (64B)
- PUBSUB: -15.24% (64B)
- DEALER_DEALER: -14.07% (64B)
- DEALER_ROUTER: -34.66% (64B) ⚠️ Worst
- ROUTER_ROUTER: -33.87% (64B) ⚠️
- ROUTER_ROUTER_POLL: -34.53% (64B) ⚠️

**Characteristics:**
- Generally worst performance across all patterns
- ROUTER patterns show largest gaps (>34%)
- Unix domain sockets seem particularly affected

**Analysis:**
IPC transport shows the largest performance gaps. This suggests ASIO's handling of Unix domain sockets may have additional overhead compared to standard libzmq's implementation. ROUTER patterns on IPC should be a specific optimization target.

## Message Size Analysis

### Small Messages (64B - 1KB)

**Performance Gaps:**
- 64B: -8.67% to -34.66% depending on pattern
- 256B: -8.49% to -38.69%
- 1KB: -9.94% to -42.84%

**Characteristics:**
- Largest performance gaps observed
- ROUTER patterns particularly affected (-32% to -43%)
- Simple patterns show -8% to -16% gaps

**Analysis:**
Small message performance is where zlink shows the most significant regression. This is typical of async I/O frameworks where per-message overhead is more pronounced. Optimization priorities:
1. Reduce per-message overhead in ASIO event handling
2. Implement message batching for small messages
3. Optimize ROUTER identity frame handling

### Medium Messages (64KB - 128KB)

**Performance Gaps:**
- 64KB: -1.19% to -16.14%
- 128KB: +0.25% to -21.37%

**Characteristics:**
- Much smaller gaps than small messages
- Some patterns achieve near-parity or better
- ROUTER patterns still show moderate gaps

**Analysis:**
Medium-sized messages show much better relative performance. The fixed ASIO overhead is amortized over larger data transfers. Some optimizations (likely buffering strategies) actually improve performance for certain patterns.

### Large Messages (256KB)

**Performance Gaps:**
- 256KB: +0.14% to -28.02%

**Characteristics:**
- Several patterns achieve parity or better than libzmq
- ROUTER patterns still lag but gap is smaller
- Some inverse improvements suggest better buffering

**Analysis:**
Large message performance is competitive. The async I/O model works well for bulk data transfer. Some patterns show better performance than libzmq, likely due to ASIO's efficient buffer management for large transfers.

## Optimization Priorities

### Priority 1: ROUTER Socket Implementation ⚠️ CRITICAL

**Target Patterns:**
- DEALER_ROUTER: -32% to -35% gap
- ROUTER_ROUTER: -21% to -34% gap
- ROUTER_ROUTER_POLL: -23% to -35% gap

**Impact:**
- Affects 3 out of 6 tested patterns
- Consistent >30% performance degradation
- Largest single optimization opportunity

**Recommended Actions:**
1. Profile ROUTER socket identity management
2. Analyze fair-queuing implementation with ASIO
3. Optimize routing table lookups
4. Review message frame handling in async context
5. Consider ROUTER-specific fast paths for common cases

**Expected Improvement:**
Reducing ROUTER overhead by 50% would bring performance to ~-15% to -17% gap, making it comparable to simple patterns.

### Priority 2: Small Message Batching

**Target:**
- All patterns with 64B-1KB message sizes
- Current gaps: -8% to -43%

**Impact:**
- Affects all 6 patterns
- Most common use case (control messages, small data)
- Compounds with ROUTER inefficiency

**Recommended Actions:**
1. Implement message batching in send/receive paths
2. Add configurable batch size option
3. Optimize for latency-throughput tradeoff
4. Consider zero-copy optimization for small messages

**Expected Improvement:**
Effective batching could reduce small message gaps by 30-50%, bringing simple patterns to -5% to -8% range.

### Priority 3: IPC Transport Optimization

**Target:**
- IPC transport showing worst performance
- Gaps: -14% to -43% depending on pattern

**Impact:**
- Affects Unix-based inter-process communication
- Common deployment scenario for microservices
- Compounds with small message and ROUTER inefficiencies

**Recommended Actions:**
1. Profile Unix domain socket handling in ASIO
2. Review buffering strategies for IPC
3. Optimize for local (same-machine) communication
4. Consider IPC-specific optimizations (shared memory hints)

**Expected Improvement:**
IPC-specific optimizations could reduce gaps by 20-30%, bringing IPC closer to TCP performance levels.

### Priority 4: General ASIO Integration

**Target:**
- Baseline overhead across all patterns (~8% to -16% for simple patterns)

**Impact:**
- Affects all patterns and transports
- Foundational performance characteristic

**Recommended Actions:**
1. Profile ASIO event loop overhead
2. Optimize mailbox polling integration
3. Review context switch patterns
4. Consider hot-path optimizations for common operations

**Expected Improvement:**
Reducing baseline ASIO overhead by 30-40% could improve simple patterns to -5% to -10% range.

## Statistical Summary

### Performance by Category

**Simple Patterns (PAIR, PUBSUB, DEALER_DEALER):**
- Average gap (64B): -13.78%
- Best case: -8.67% (PUBSUB/inproc)
- Worst case: -16.33% (PUBSUB/tcp)
- Large message performance: -1% to +3% (competitive)

**ROUTER Patterns (DEALER_ROUTER, ROUTER_ROUTER, ROUTER_ROUTER_POLL):**
- Average gap (64B): -32.87%
- Best case: -21.39% (ROUTER_ROUTER/inproc)
- Worst case: -34.66% (DEALER_ROUTER/ipc)
- Large message performance: -12% to -28% (still significant)

### Transport Rankings (64B messages)

**Best to Worst (Average Gap):**
1. **inproc**: -18.41% average gap
   - Best for: All patterns
   - Worst for: DEALER_ROUTER (-31.98%)

2. **tcp**: -23.60% average gap
   - Best for: DEALER_DEALER (-12.58%)
   - Worst for: ROUTER_ROUTER_POLL (-33.91%)

3. **ipc**: -24.76% average gap
   - Best for: DEALER_DEALER (-14.07%)
   - Worst for: DEALER_ROUTER (-34.66%)

### Message Size Impact

**Smallest Gaps (by message size):**
- 256KB: -2.62% to +2.79% for simple patterns
- 128KB: -0.95% to +3.12% for simple patterns
- 64KB: -3.25% to +0.53% for simple patterns

**Largest Gaps (by message size):**
- 1KB: -42.03% to -42.84% for DEALER_ROUTER
- 256B: -38.69% for DEALER_ROUTER/ipc
- 64B: -34.66% for DEALER_ROUTER/ipc

## Baseline Characteristics Summary

### Strengths
✓ Competitive performance for large messages (64KB+)
✓ Inproc transport shows good efficiency
✓ Simple patterns (PAIR, PUBSUB, DEALER_DEALER) have moderate, acceptable gaps
✓ Some patterns achieve parity or better with large messages

### Weaknesses
⚠️ ROUTER patterns show critical performance degradation (>30%)
⚠️ Small message performance significantly impacted across all patterns
⚠️ IPC transport shows largest gaps
⚠️ Consistent overhead even for simple patterns (~12-16%)

### Opportunities
→ **High impact**: ROUTER socket optimization (affects 50% of tested patterns)
→ **Medium impact**: Small message batching (affects all patterns)
→ **Medium impact**: IPC transport optimization (affects cross-process use cases)
→ **Low-medium impact**: General ASIO overhead reduction (affects baseline)

## Next Steps

1. **Deep Dive Analysis**:
   - Profile ROUTER socket implementation
   - Identify specific hot paths in ROUTER code
   - Analyze fair-queuing interaction with ASIO

2. **Proof of Concept**:
   - Implement ROUTER fast-path for common cases
   - Test message batching for small messages
   - Benchmark IPC-specific optimizations

3. **Iterative Optimization**:
   - Start with highest-impact optimizations (ROUTER)
   - Measure improvement after each change
   - Re-run 10x benchmarks to validate improvements

4. **Performance Validation**:
   - Compare optimized results against this baseline
   - Document performance improvements
   - Update CHANGELOG with optimization results

## Appendix: Raw Data Location

Full benchmark results available at:
- **Raw output**: `docs/performance/baseline/benchmark_baseline_10x.txt`
- **Test count**: 2,160 individual test runs
- **Data format**: Markdown tables with throughput (M/s) and latency (μs)
