# TCP Optimization - Fix and Results

## Root Cause
TCP speculative synchronous writes (`speculative_write()`) monopolized the ASIO I/O thread.
In a single-threaded `io_context`, long sync write bursts starved read events and
caused throughput collapse. IPC already moved to async-only writes; applying the
same principle to TCP removed the head-of-line blocking.

## Fix
Disable speculative sync writes for TCP by default and gate them behind an
opt-in env var.

### Implementation
- Added `tcp_transport_t::supports_speculative_write()` returning `ZMQ_ASIO_TCP_SYNC_WRITE`.
- Default behavior: async-only writes for TCP.
- Opt-in: set `ZMQ_ASIO_TCP_SYNC_WRITE=1` to re-enable speculative sync writes.
- Switched TCP async writes to `boost::asio::async_write()` to complete full buffers.

## Results (10K messages, 64B, TCP)
| Pattern | Before | After | Improvement |
|---------|--------|-------|-------------|
| PAIR | 2.7 ~ 2.9 M/s | 4.69 M/s | +~70% |
| PUBSUB | 2.8 M/s | 4.82 M/s | +~72% |
| DEALER_DEALER | 2.9 M/s | 4.52 M/s | +~56% |
| DEALER_ROUTER | 2.5 M/s | 4.15 M/s | +~66% |
| ROUTER_ROUTER | 2.35 M/s | 4.26 M/s | +~81% |
| ROUTER_ROUTER_POLL | 2.16 M/s | 3.89 M/s | +~80% |

## Commands
```bash
# Build
cmake --build build --target comp_zlink_pair comp_zlink_pubsub comp_zlink_dealer_dealer \
  comp_zlink_dealer_router comp_zlink_router_router comp_zlink_router_router_poll

# TCP 10K baseline
for pattern in pair pubsub dealer_dealer dealer_router router_router router_router_poll; do
  BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_${pattern} zlink tcp 64
  done | grep throughput

# Opt-in speculative sync write (if needed)
ZMQ_ASIO_TCP_SYNC_WRITE=1 BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_pair zlink tcp 64

# Test
ctest -R test_asio_tcp --output-on-failure
```

## Notes
- This mirrors the IPC fix: async-only writes avoid starving the IO thread.
- The env toggle keeps the old path available for regression/perf comparisons.
