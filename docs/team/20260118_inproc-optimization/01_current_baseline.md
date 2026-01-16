# Baseline (pre-fix)

## Commands

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

## Results (10K, 64B)

### zlink

- PAIR: 4.83 M/s
- PUBSUB: 4.07 M/s
- DEALER_DEALER: 4.55 M/s
- DEALER_ROUTER: 4.28 M/s
- ROUTER_ROUTER: 3.76 M/s
- ROUTER_ROUTER_POLL: 3.55 M/s

### libzmq

- PAIR: 5.47 M/s
- PUBSUB: 4.94 M/s
- DEALER_DEALER: 5.69 M/s
- DEALER_ROUTER: 4.96 M/s
- ROUTER_ROUTER: 4.34 M/s
- ROUTER_ROUTER_POLL: 3.62 M/s
