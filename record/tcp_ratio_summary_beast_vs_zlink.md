# TCP 대비 성능 비율 요약 (zlink vs Beast)

- zlink ALL source: `benchwithzlink/results/20260123/bench_linux_ALL_20260123_170718.txt`
- zlink STREAM 512/2048 source: `benchwithzlink/results/20260123/bench_linux_STREAM_20260123_193854.txt`
- Beast: `build/bin/bench_beast_stream` (STREAM)
- Runs: 10 (no-taskset)

## STREAM TCP 대비 Throughput 비율 (Beast 비교 + 튜닝 표시)

| Size | TLS (zlink / beast) | WS (zlink / beast) | WSS (zlink / beast) | Tune? |
|---|---:|---:|---:|:---:|
| 512B | 59.6% / 18.6% | 86.4% / 26.8% | 61.0% / 18.3% | OK |
| 1024B | 55.5% / 74.3% | 79.0% / 94.6% | 52.0% / 72.0% | TUNE |
| 2048B | 48.7% / 66.2% | 79.0% / 93.4% | 49.8% / 64.5% | TUNE |

## 기타 소켓 TCP 대비 Throughput 비율 (1KB, zlink)

| Pattern | TCP (Kmsg/s) | TLS % | WS % | WSS % |
|---|---:|---:|---:|---:|
| PAIR | 1010.12 | 66.8% | 76.8% | 47.7% |
| PUBSUB | 978.93 | 69.1% | 81.0% | 50.4% |
| DEALER_DEALER | 996.38 | 67.5% | 77.0% | 48.5% |
| DEALER_ROUTER | 1005.06 | 67.7% | 76.7% | 48.1% |
| ROUTER_ROUTER | 954.27 | 69.4% | 77.4% | 50.8% |
| ROUTER_ROUTER_POLL | 967.07 | 69.6% | 75.6% | 49.4% |

> Note: Beast baseline은 STREAM만 존재해서, 기타 소켓은 튜닝 표시를 하지 않았습니다.