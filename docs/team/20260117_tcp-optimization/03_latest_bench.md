# Latest TCP Bench (follow-up)

## Commands

```
for p in pair pubsub dealer_dealer dealer_router router_router router_router_poll; do
  BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_${p} zlink tcp 64 | rg throughput
  done

for p in pair pubsub dealer_dealer dealer_router router_router router_router_poll; do
  BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_std_zmq_${p} libzmq tcp 64 | rg throughput
  done
```

## Results (10K, 64B)

### zlink

- PAIR: 5.43 M/s
- PUBSUB: 5.46 M/s
- DEALER_DEALER: 5.04 M/s
- DEALER_ROUTER: 4.54 M/s
- ROUTER_ROUTER: 4.62 M/s
- ROUTER_ROUTER_POLL: 4.12 M/s

### libzmq

- PAIR: 5.74 M/s
- PUBSUB: 4.89 M/s
- DEALER_DEALER: 5.79 M/s
- DEALER_ROUTER: 4.31 M/s
- ROUTER_ROUTER: 5.03 M/s
- ROUTER_ROUTER_POLL: 4.56 M/s

## Notes

- ROUTER_ROUTER_POLL (zlink) prints debug logs in current bench code.
- Single-run numbers have variance; repeat runs are recommended for averages.
