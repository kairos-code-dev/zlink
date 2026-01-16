# Phase 5 Progress Log (IPC Deadlock)

## Goal
Resolve IPC deadlock at >2K messages and validate across patterns.

## Work Performed
1. Rolled back strand serialization in `asio_engine`.
2. Added synchronous `read_some()` to `i_asio_transport` and implemented it in TCP/IPC/SSL; WS/WSS returns EAGAIN unless frame buffer has data.
3. Added `asio_engine_t::speculative_read()` and invoked it after backpressure clears in `restart_input_internal()`.
4. Identified that IPC speculative synchronous writes trigger timeouts; forced async write path fixes the hang.
5. Added `supports_speculative_write()` to the transport interface and disabled speculative sync write for IPC by default (opt-in via `ZMQ_ASIO_IPC_SYNC_WRITE=1`).
6. Rebuilt and ran IPC benchmarks (PAIR and other patterns).

## Key Finding
- IPC timeouts disappear when `ZMQ_ASIO_IPC_FORCE_ASYNC=1`, indicating the sync write path was the trigger. The fix is to keep IPC on async write by default and gate speculative sync writes.

## Tests Run
### PAIR (ipc, 64B)
- 2K messages: 5/5 success, throughput 3.15 ~ 4.12 M/s
- 10K messages: 3/3 success, throughput 4.83 ~ 4.93 M/s
- 200K messages: 1/1 success, throughput 5.05 M/s

### Other Patterns (ipc, 64B, 10K messages)
- PUBSUB: 4.83 M/s
- DEALER_DEALER: 4.37 M/s
- DEALER_ROUTER: 4.24 M/s
- ROUTER_ROUTER: 3.94 M/s
- ROUTER_ROUTER_POLL: 3.85 M/s

## Commands Used
```bash
# Build
cmake --build build --target comp_zlink_pair

# PAIR
for i in 1 2 3 4 5; do
  BENCH_MSG_COUNT=2000 timeout 10 ./build/bin/comp_zlink_pair zlink ipc 64
  done | grep throughput

for i in 1 2 3; do
  BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_pair zlink ipc 64
  done | grep throughput

BENCH_MSG_COUNT=200000 timeout 60 ./build/bin/comp_zlink_pair zlink ipc 64 | grep throughput

# Patterns
for pattern in pubsub dealer_dealer dealer_router router_router router_router_poll; do
  BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_${pattern} zlink ipc 64
  done | grep throughput
```

## Result
IPC deadlock is resolved under the tested scenarios (PAIR 2K/10K/200K and all major patterns at 10K). The new transport gate keeps IPC on async write by default while retaining speculative write for other transports.

## Follow-ups
- Run `ctest --output-on-failure` for regression coverage.
