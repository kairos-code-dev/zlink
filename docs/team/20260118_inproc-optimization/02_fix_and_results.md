# Fix & Results (Phase 1)

## Changes Applied

1. **mailbox_t switched back to signaler-based wakeup**
   - Removed condition_variable_t in mailbox_t.
   - Added signaler_t + _active state for sleeping/active tracking.
   - recv path no longer takes _sync lock (single-reader assumption).

2. **mailbox_t validity and fork handling restored**
   - valid() now uses _signaler.valid().
   - forked() now calls _signaler.forked().

3. **ASIO schedule path simplified**
   - schedule_if_needed() no longer calls _cpipe.check_read() from sender thread.

## Code References

- `src/mailbox.hpp`
- `src/mailbox.cpp`

## Commands

```
cmake --build build

BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_pair zlink inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_pubsub zlink inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_dealer_dealer zlink inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_dealer_router zlink inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_router_router zlink inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_router_router_poll zlink inproc 64 | rg throughput

BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_std_zmq_pair libzmq inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_std_zmq_pubsub libzmq inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_std_zmq_dealer_dealer libzmq inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_std_zmq_dealer_router libzmq inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_std_zmq_router_router libzmq inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_std_zmq_router_router_poll libzmq inproc 64 | rg throughput
```

## Results (10K, 64B)

### zlink

- PAIR: 4.79 M/s
- PUBSUB: 4.28 M/s
- DEALER_DEALER: 4.94 M/s
- DEALER_ROUTER: 4.30 M/s
- ROUTER_ROUTER: 3.89 M/s
- ROUTER_ROUTER_POLL: 3.16 M/s

### libzmq (same run)

- PAIR: 5.76 M/s
- PUBSUB: 4.93 M/s
- DEALER_DEALER: 5.68 M/s
- DEALER_ROUTER: 4.99 M/s
- ROUTER_ROUTER: 4.65 M/s
- ROUTER_ROUTER_POLL: 3.83 M/s

## Notes

- ROUTER_ROUTER_POLL is noisy because the zlink bench prints debug logs (pre-existing).
- Single-run numbers vary; trends are still below libzmq, but several patterns improved vs baseline.
- Additional profiling may be needed for full parity.
