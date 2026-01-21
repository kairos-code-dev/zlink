# ZMP v1 Benchmarks (runs=3)

Date: 2026-01-21
Command: `benchwithzmq/run_benchmarks.sh --runs 3`
Build: Release (`build/`)
Protocol: ZMP (default `ZLINK_PROTOCOL`)

Notes:
- libzmq baseline uses `benchwithzmq/libzmq/libzmq_dist`.
- Raw console output was not archived; this file summarizes key deltas.

## Summary
- Overall deltas are mixed; most results are within about +/-10%.
- tcp small-message latency regresses in some patterns.
- inproc mid/large message throughput improves in several patterns.

## Highlights (Diff % = zlink vs libzmq)
Regressions:
- DEALER_ROUTER tcp 64B latency: -23.4%
- ROUTER_ROUTER_POLL tcp 262144B latency: -18.9%
- DEALER_DEALER tcp 65536B throughput: -9.2%
- DEALER_DEALER inproc 64B throughput: -11.2%

Improvements:
- DEALER_ROUTER tcp 131072B throughput: +16.6%
- ROUTER_ROUTER tcp 65536B throughput: +8.3%
- ROUTER_ROUTER_POLL inproc 131072B throughput: +13.7%
- ROUTER_ROUTER inproc 64B throughput: +11.8%

## Artifacts
- `benchwithzmq/baseline/20260121/bench_ALL_20260121_141453_zmp_v1.txt`
- `benchwithzmq/baseline/20260121/bench_ALL_20260121_141758.txt`
- `benchwithzmq/baseline/20260121/bench_ALL_20260121_152423.txt`
