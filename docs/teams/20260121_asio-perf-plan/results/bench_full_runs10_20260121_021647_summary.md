# Full Bench Summary (runs=10, cached libzmq baseline)

Run
- Command: benchwithzmq/run_benchmarks.sh --runs 10 --reuse-build --skip-libzmq
- Output: docs/teams/20260121_asio-perf-plan/results/bench_full_runs10_20260121_021647.txt
- Baseline: benchwithzmq/libzmq_cache.json

High-level signal (zlink vs libzmq, diff% counts)
- PAIR: throughput +4 / -14, latency +1 / -17 (대부분 회귀)
- PUBSUB: throughput +12 / -6, latency +14 / -4 (대체로 개선)
- DEALER_DEALER: throughput +5 / -13, latency +2 / -16 (대부분 회귀)
- DEALER_ROUTER: throughput +7 / -11, latency +6 / -12 (혼재, 회귀 우세)
- ROUTER_ROUTER: throughput +9 / -9, latency +12 / -6 (혼재, 지연 개선 우세)
- ROUTER_ROUTER_POLL: throughput +10 / -8, latency +13 / -5 (혼재, 지연 개선 우세)

Top improvements (diff% 기준)
- PUBSUB tcp 131072B Throughput +20.43%
- PUBSUB tcp 131072B Latency +16.97%
- ROUTER_ROUTER_POLL ipc 131072B Latency +15.79%
- ROUTER_ROUTER_POLL ipc 65536B Latency +15.06%
- ROUTER_ROUTER ipc 65536B Latency +14.72%

Top regressions (diff% 기준)
- DEALER_ROUTER ipc 65536B Latency -22.16%
- DEALER_DEALER tcp 131072B Throughput -21.54%
- PAIR tcp 262144B Latency -20.98%
- DEALER_DEALER tcp 262144B Latency -19.60%
- DEALER_ROUTER tcp 262144B Latency -18.75%

Notes
- 결과는 cached libzmq baseline 대비이며, 환경 변화가 있다면 갱신 필요.
- 개선은 PUBSUB과 ROUTER 계열(특히 ipc latency)에서 더 자주 관찰됨.
- PAIR/DEALER 계열은 회귀 우세.
