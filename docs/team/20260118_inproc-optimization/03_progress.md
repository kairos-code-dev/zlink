# Progress Log

## Phase 1: mailbox_t refactor (signaler-based)

### Goal

- Reduce recv-side locking and wakeup overhead in inproc command path.

### Actions

1. Replaced mailbox_t condition_variable-based wakeup with signaler.
2. Restored _active flag to avoid redundant waits.
3. Removed recv-side _sync lock (single-reader assumption).
4. Restored valid()/forked() semantics based on signaler.
5. Simplified schedule_if_needed() (no sender-side cpipe check).

### Build

```
cmake --build build
```

### Bench Commands

```
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

### Results (10K, 64B)

zlink:
- PAIR 4.79 M/s
- PUBSUB 4.28 M/s
- DEALER_DEALER 4.94 M/s
- DEALER_ROUTER 4.30 M/s
- ROUTER_ROUTER 3.89 M/s
- ROUTER_ROUTER_POLL 3.16 M/s

libzmq:
- PAIR 5.76 M/s
- PUBSUB 4.93 M/s
- DEALER_DEALER 5.68 M/s
- DEALER_ROUTER 4.99 M/s
- ROUTER_ROUTER 4.65 M/s
- ROUTER_ROUTER_POLL 3.83 M/s

### Status

- 일부 패턴 개선 확인.
- 여전히 libzmq 대비 10-20% 정도 갭 존재.
- 추가 프로파일링/벤치 반복 필요.
