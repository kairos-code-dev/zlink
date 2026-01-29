# 2026-01-29 Benchmark Report (Full Runs, 10x)

## Setup
- Date: 2026-01-29
- Runs: 10
- CPU pinning: enabled (taskset)
- benchwithzlink baseline libzlink: not found (benchwithzlink/baseline/lib contains libzmq only)
- benchwithzmq standard libzmq: not found (benchwithzmq/libzmq/libzmq_dist has no lib/)

## Raw Result Files
- benchwithzlink: `benchwithzlink/results/20260129/bench_linux_ALL_20260129_184803.txt`
- benchwithzmq (zlink-only): `benchwithzmq/baseline/20260129/bench_linux_ALL_20260129_194404.txt`
- benchwithbeast (STREAM): `benchwithbeast/results/20260129/bench_beast_STREAM_20260129_200645.csv`

## benchwithzlink (zlink-only) Summary
| Pattern | Transport | 64B Throughput | 262144B Throughput | 64B Latency | 262144B Latency |
|---|---|---|---|---|---|
| DEALER_DEALER | inproc | 5567.07 Kmsg/s | 58.36 Kmsg/s | 0.11 us | 7.14 us |
| DEALER_DEALER | ipc | 3831.39 Kmsg/s | 9.12 Kmsg/s | 4.72 us | 34.62 us |
| DEALER_DEALER | tcp | 3734.00 Kmsg/s | 9.30 Kmsg/s | 5.32 us | 34.98 us |
| DEALER_DEALER | tls | 3268.22 Kmsg/s | 4.04 Kmsg/s | 7.20 us | 153.50 us |
| DEALER_DEALER | ws | 3676.47 Kmsg/s | 7.27 Kmsg/s | 6.57 us | 55.70 us |
| DEALER_DEALER | wss | 3139.66 Kmsg/s | 3.33 Kmsg/s | 8.25 us | 190.08 us |
| DEALER_ROUTER | inproc | 5352.85 Kmsg/s | 55.73 Kmsg/s | 0.14 us | 7.62 us |
| DEALER_ROUTER | ipc | 3138.13 Kmsg/s | 9.34 Kmsg/s | 4.72 us | 34.80 us |
| DEALER_ROUTER | tcp | 3113.17 Kmsg/s | 8.73 Kmsg/s | 5.42 us | 35.59 us |
| DEALER_ROUTER | tls | 2797.74 Kmsg/s | 4.04 Kmsg/s | 7.14 us | 153.57 us |
| DEALER_ROUTER | ws | 2959.29 Kmsg/s | 7.20 Kmsg/s | 6.45 us | 55.73 us |
| DEALER_ROUTER | wss | 2545.28 Kmsg/s | 3.32 Kmsg/s | 8.21 us | 190.02 us |
| PAIR | inproc | 5375.19 Kmsg/s | 60.89 Kmsg/s | 0.11 us | 6.89 us |
| PAIR | ipc | 3777.25 Kmsg/s | 8.96 Kmsg/s | 4.75 us | 35.39 us |
| PAIR | tcp | 3668.59 Kmsg/s | 10.01 Kmsg/s | 5.42 us | 35.89 us |
| PAIR | tls | 3243.28 Kmsg/s | 3.98 Kmsg/s | 7.03 us | 156.71 us |
| PAIR | ws | 3704.55 Kmsg/s | 7.12 Kmsg/s | 6.87 us | 55.52 us |
| PAIR | wss | 3093.79 Kmsg/s | 3.31 Kmsg/s | 8.47 us | 192.97 us |
| PUBSUB | inproc | 4951.49 Kmsg/s | 59.30 Kmsg/s | 0.20 us | 16.87 us |
| PUBSUB | ipc | 3558.89 Kmsg/s | 9.23 Kmsg/s | 0.28 us | 108.34 us |
| PUBSUB | tcp | 3446.78 Kmsg/s | 10.09 Kmsg/s | 0.29 us | 99.13 us |
| PUBSUB | tls | 3123.45 Kmsg/s | 4.05 Kmsg/s | 0.32 us | 247.00 us |
| PUBSUB | ws | 3468.18 Kmsg/s | 8.55 Kmsg/s | 0.29 us | 116.97 us |
| PUBSUB | wss | 3046.38 Kmsg/s | 3.56 Kmsg/s | 0.33 us | 281.19 us |
| ROUTER_ROUTER | inproc | 3776.79 Kmsg/s | 58.99 Kmsg/s | 0.21 us | 7.51 us |
| ROUTER_ROUTER | ipc | 2890.25 Kmsg/s | 9.37 Kmsg/s | 3.60 us | 34.70 us |
| ROUTER_ROUTER | tcp | 2805.28 Kmsg/s | 10.37 Kmsg/s | 4.05 us | 42.33 us |
| ROUTER_ROUTER | tls | 2575.94 Kmsg/s | 4.09 Kmsg/s | 5.33 us | 177.53 us |
| ROUTER_ROUTER | ws | 2771.26 Kmsg/s | 7.23 Kmsg/s | 5.06 us | 66.14 us |
| ROUTER_ROUTER | wss | 2311.42 Kmsg/s | 3.31 Kmsg/s | 6.88 us | 271.88 us |
| ROUTER_ROUTER_POLL | inproc | 3893.12 Kmsg/s | 56.54 Kmsg/s | 0.53 us | 7.85 us |
| ROUTER_ROUTER_POLL | ipc | 2903.08 Kmsg/s | 9.38 Kmsg/s | 4.26 us | 35.14 us |
| ROUTER_ROUTER_POLL | tcp | 2850.88 Kmsg/s | 10.14 Kmsg/s | 4.78 us | 42.89 us |
| ROUTER_ROUTER_POLL | tls | 2608.49 Kmsg/s | 4.06 Kmsg/s | 5.85 us | 177.93 us |
| ROUTER_ROUTER_POLL | ws | 2830.04 Kmsg/s | 7.14 Kmsg/s | 5.62 us | 67.22 us |
| ROUTER_ROUTER_POLL | wss | 2314.04 Kmsg/s | 3.29 Kmsg/s | 7.04 us | 275.38 us |
| STREAM | tcp | 3074.12 Kmsg/s | 9.83 Kmsg/s | 5.48 us | 39.17 us |
| STREAM | tls | 2173.79 Kmsg/s | 3.91 Kmsg/s | 7.23 us | 154.79 us |
| STREAM | ws | 2320.51 Kmsg/s | 6.92 Kmsg/s | 6.58 us | 55.73 us |
| STREAM | wss | 2023.95 Kmsg/s | 3.27 Kmsg/s | 8.35 us | 192.28 us |

## benchwithzmq (zlink-only) Summary
| Pattern | Transport | 64B Throughput | 262144B Throughput | 64B Latency | 262144B Latency |
|---|---|---|---|---|---|
| DEALER_DEALER | inproc | 5066.86 Kmsg/s | 60.37 Kmsg/s | 0.11 us | 6.95 us |
| DEALER_DEALER | ipc | 3840.80 Kmsg/s | 9.40 Kmsg/s | 4.70 us | 34.84 us |
| DEALER_DEALER | tcp | 3757.51 Kmsg/s | 8.56 Kmsg/s | 5.36 us | 34.92 us |
| DEALER_ROUTER | inproc | 5499.56 Kmsg/s | 55.48 Kmsg/s | 0.15 us | 7.51 us |
| DEALER_ROUTER | ipc | 3076.88 Kmsg/s | 9.58 Kmsg/s | 4.81 us | 34.60 us |
| DEALER_ROUTER | tcp | 3131.27 Kmsg/s | 9.68 Kmsg/s | 5.37 us | 35.47 us |
| PAIR | inproc | 5480.21 Kmsg/s | 62.25 Kmsg/s | 0.11 us | 7.02 us |
| PAIR | ipc | 3793.68 Kmsg/s | 9.55 Kmsg/s | 4.85 us | 34.46 us |
| PAIR | tcp | 3656.43 Kmsg/s | 8.47 Kmsg/s | 5.29 us | 35.66 us |
| PUBSUB | inproc | 5075.82 Kmsg/s | 59.79 Kmsg/s | 0.20 us | 16.73 us |
| PUBSUB | ipc | 3587.16 Kmsg/s | 9.30 Kmsg/s | 0.28 us | 107.47 us |
| PUBSUB | tcp | 3480.77 Kmsg/s | 9.67 Kmsg/s | 0.29 us | 103.64 us |
| ROUTER_ROUTER | inproc | 3881.86 Kmsg/s | 57.85 Kmsg/s | 0.21 us | 7.75 us |
| ROUTER_ROUTER | ipc | 2876.83 Kmsg/s | 9.37 Kmsg/s | 3.62 us | 35.08 us |
| ROUTER_ROUTER | tcp | 2795.04 Kmsg/s | 10.32 Kmsg/s | 4.08 us | 42.64 us |
| ROUTER_ROUTER_POLL | inproc | 3962.12 Kmsg/s | 55.49 Kmsg/s | 0.53 us | 8.01 us |
| ROUTER_ROUTER_POLL | ipc | 2809.12 Kmsg/s | 9.41 Kmsg/s | 4.15 us | 34.42 us |
| ROUTER_ROUTER_POLL | tcp | 2855.28 Kmsg/s | 10.07 Kmsg/s | 4.65 us | 43.13 us |

## benchwithbeast STREAM Summary
| Transport | 64B Throughput | 262144B Throughput | 64B Latency | 262144B Latency |
|---|---|---|---|---|
| tcp | 2.82521e+06 | 36325.6 | 15.1802 | 41.1269 |
| tls | 509048 | 11648.5 | 18.899 | 108.807 |
| ws | 707273 | 8592.26 | 14.4485 | 87.215 |
| wss | 504147 | 5206.53 | 17.7451 | 160.129 |
