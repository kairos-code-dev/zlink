# TCP Baseline (After IPC Phase 5, Pre-Fix)

## Summary
TCP throughput remains ~2.1 ~ 3.1 M/s at 10K messages (64B), about half of libzmq-ref.

## Results (10K messages, 64B)
- PAIR: 3.08 M/s
- PUBSUB: 2.80 M/s
- DEALER_DEALER: 2.98 M/s
- DEALER_ROUTER: 2.50 M/s
- ROUTER_ROUTER: 2.35 M/s
- ROUTER_ROUTER_POLL: 2.16 M/s

## Command
```bash
for pattern in pair pubsub dealer_dealer dealer_router router_router router_router_poll; do
  BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_${pattern} zlink tcp 64
  done | grep throughput
```
