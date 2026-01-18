# Runs=10 Benchmark Summary (TL pool + mimalloc default)

## Setup

- Patterns: PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, ROUTER_ROUTER_POLL
- Transports: tcp, inproc, ipc
- Sizes: 64B, 256B, 1024B, 65536B, 131072B, 262144B
- Iterations: 10 per size/transport/pattern

## Observations (relative to standard libzmq)

- Small sizes (64B/256B/1024B): throughput consistently higher across tcp/ipc; inproc shows large gains.
- 64KB: tcp/ipc throughput generally improves; inproc is mixed and sometimes regresses.
- 128KB/256KB: inproc throughput regresses sharply; tcp/ipc show mixed results with several regressions at 256KB.
- Latency: 64KB+ often regresses on tcp/ipc (higher latency), and inproc latency regresses for larger sizes.

## Takeaway

- TL pool + mimalloc helps small/medium sizes.
- Large-message behavior (64KB+) still shows regressions, especially inproc and latency-sensitive paths.
