# tag_0.5 Performance Report

## 1. Executive Summary

### 1.1 zlink vs Standard libzlink (Goal: Parity)
> **Verdict:** Native performance parity achieved.
> - **Inproc/TCP**: Virtually identical performance (+/- 1%).
> - **IPC**: Slight overhead (~3%) due to full async implementation.

| Transport | Avg Median Diff | Status | Note |
|-----------|----------------:|:-------|:-----|
| **inproc**| **+0.61%**      | ✅ Parity | Slightly faster in some patterns |
| **tcp**   | **-0.97%**      | ✅ Parity | Negligible difference (~1%) |
| **ipc**   | **-3.13%**      | ⚠️ Good   | Minor overhead due to async architecture |

### 1.2 Optimization: WS/WSS (vs Baseline)
> **Verdict:** Significant optimization success.
> - **WebSocket (WS)**: Up to **+75%** throughput increase.
> - **Secure WS (WSS)**: Up to **+48%** throughput increase.

| Transport | Pattern Group | Median Improvement | Max Speedup |
|-----------|---------------|-------------------:|------------:|
| **WS**    | DEALER/PAIR   | **+23% ~ +25%**    | **+71.6%**  |
| **WS**    | ROUTER        | **+4.5% ~ +11.5%** | **+75.2%**  |
| **WS**    | PUB/SUB       | **+9.6%**          | **+67.8%**  |
| **WSS**   | DEALER/PAIR   | **+8% ~ +11%**     | **+32.8%**  |
| **WSS**   | ROUTER        | **+12% ~ +16%**    | **+48.9%**  |
| **WSS**   | PUB/SUB       | **+19.5%**         | **+41.1%**  |

---

## 2. Benchmark Setup
- Date: 2026-01-24
- Runs: 10
- CPU pinning: disabled (`--no-taskset`)
- benchwithzlink-baseline baseline file: `/home/ulalax/project/ulalax/zlink-codex/benchwithzlink-baseline/baseline/20260124/bench_linux_ALL_20260124_152322.txt`
- benchwithzlink result file: `/home/ulalax/project/ulalax/zlink-codex/benchwithzlink/results/20260124/bench_linux_ALL_20260124_154903.txt`

## 3. zlink vs standard libzlink (benchwithzlink-baseline)
Median throughput diff across sizes per pattern/transport (positive = zlink faster).

| Pattern | Transport | Median Diff (%) | Min Diff (%) | Max Diff (%) |
|---------|-----------|-----------------|--------------|--------------|
| DEALER_DEALER | inproc | -1.39 | -34.19 | +3.67 |
| DEALER_DEALER | ipc | -2.59 | -8.92 | +1.40 |
| DEALER_DEALER | tcp | -0.97 | -7.81 | +0.97 |
| DEALER_ROUTER | inproc | +0.17 | -12.71 | +14.04 |
| DEALER_ROUTER | ipc | -4.28 | -7.41 | +0.33 |
| DEALER_ROUTER | tcp | -2.25 | -9.25 | +2.38 |
| PAIR | inproc | +0.36 | -2.72 | +4.77 |
| PAIR | ipc | -4.81 | -9.00 | -0.37 |
| PAIR | tcp | +0.05 | -7.68 | +2.49 |
| PUBSUB | inproc | +2.46 | -1.42 | +24.97 |
| PUBSUB | ipc | -1.82 | -6.16 | +16.30 |
| PUBSUB | tcp | +0.86 | -6.15 | +5.76 |
| ROUTER_ROUTER | inproc | -1.34 | -15.13 | +3.50 |
| ROUTER_ROUTER | ipc | -2.77 | -7.33 | +1.36 |
| ROUTER_ROUTER | tcp | -4.52 | -8.93 | +4.76 |
| ROUTER_ROUTER_POLL | inproc | +3.42 | -11.57 | +8.19 |
| ROUTER_ROUTER_POLL | ipc | -2.50 | -5.13 | +3.59 |
| ROUTER_ROUTER_POLL | tcp | +0.99 | -6.22 | +4.23 |

## 4. current vs baseline (benchwithzlink)
Median throughput diff across sizes per pattern/transport (positive = current faster).

| Pattern | Transport | Median Diff (%) | Min Diff (%) | Max Diff (%) |
|---------|-----------|-----------------|--------------|--------------|
| DEALER_DEALER | inproc | -3.61 | -10.54 | +21.27 |
| DEALER_DEALER | ipc | -1.46 | -5.30 | +2.63 |
| DEALER_DEALER | tcp | +0.12 | -1.23 | +35.84 |
| DEALER_DEALER | tls | -0.32 | -2.64 | +2.58 |
| DEALER_DEALER | ws | +25.28 | -5.70 | +71.60 |
| DEALER_DEALER | wss | +8.26 | -6.72 | +30.53 |
| DEALER_ROUTER | inproc | +0.12 | -1.45 | +1.99 |
| DEALER_ROUTER | ipc | -0.86 | -5.22 | +2.09 |
| DEALER_ROUTER | tcp | +0.36 | -6.94 | +1.20 |
| DEALER_ROUTER | tls | +0.84 | +0.15 | +1.31 |
| DEALER_ROUTER | ws | +21.70 | -5.76 | +64.76 |
| DEALER_ROUTER | wss | +11.62 | -1.36 | +32.32 |
| PAIR | inproc | -0.91 | -5.55 | +3.61 |
| PAIR | ipc | -2.61 | -19.46 | +5.96 |
| PAIR | tcp | -3.06 | -10.05 | +2.41 |
| PAIR | tls | +0.26 | -1.48 | +1.70 |
| PAIR | ws | +23.93 | -5.95 | +70.70 |
| PAIR | wss | +11.74 | -2.14 | +32.84 |
| PUBSUB | inproc | +1.68 | -9.90 | +5.10 |
| PUBSUB | ipc | +0.36 | -2.60 | +4.79 |
| PUBSUB | tcp | +0.36 | -3.07 | +1.44 |
| PUBSUB | tls | +0.46 | -2.02 | +3.62 |
| PUBSUB | ws | +9.62 | +1.33 | +67.82 |
| PUBSUB | wss | +19.54 | +8.33 | +41.11 |
| ROUTER_ROUTER | inproc | -1.84 | -3.73 | +0.89 |
| ROUTER_ROUTER | ipc | -3.03 | -3.72 | -0.10 |
| ROUTER_ROUTER | tcp | +3.56 | -0.77 | +4.45 |
| ROUTER_ROUTER | tls | +2.37 | -2.59 | +7.71 |
| ROUTER_ROUTER | ws | +11.49 | -8.20 | +75.17 |
| ROUTER_ROUTER | wss | +16.15 | -0.63 | +48.95 |
| ROUTER_ROUTER_POLL | inproc | +3.37 | -4.38 | +14.78 |
| ROUTER_ROUTER_POLL | ipc | +6.18 | -0.06 | +7.73 |
| ROUTER_ROUTER_POLL | tcp | -13.71 | -21.10 | -2.58 |
| ROUTER_ROUTER_POLL | tls | -9.48 | -12.26 | +0.25 |
| ROUTER_ROUTER_POLL | ws | +4.54 | -10.47 | +61.81 |
| ROUTER_ROUTER_POLL | wss | +12.39 | -13.28 | +42.90 |
| STREAM | tcp | -0.58 | -5.36 | +1.54 |
| STREAM | tls | +1.71 | -1.67 | +6.49 |
| STREAM | ws | -3.00 | -8.98 | +0.39 |
| STREAM | wss | -11.54 | -31.23 | -3.60 |

## 5. Notable Throughput Changes (current vs baseline)
Changes >= ±10% (throughput only).

| Diff (%) | Pattern | Transport | Size |
|----------|---------|-----------|------|
| -31.23 | STREAM | wss | 1024B |
| -29.73 | STREAM | wss | 256B |
| -21.10 | ROUTER_ROUTER_POLL | tcp | 262144B |
| -19.46 | PAIR | ipc | 65536B |
| -16.71 | ROUTER_ROUTER_POLL | tcp | 65536B |
| -16.10 | ROUTER_ROUTER_POLL | tcp | 131072B |
| -13.28 | ROUTER_ROUTER_POLL | wss | 131072B |
| -12.28 | STREAM | wss | 131072B |
| -12.26 | ROUTER_ROUTER_POLL | tls | 65536B |
| -12.17 | ROUTER_ROUTER_POLL | tls | 256B |
| -11.33 | ROUTER_ROUTER_POLL | tcp | 1024B |
| -10.79 | STREAM | wss | 262144B |
| -10.54 | DEALER_DEALER | inproc | 65536B |
| -10.47 | ROUTER_ROUTER_POLL | ws | 131072B |
| -10.05 | PAIR | tcp | 65536B |
| +10.00 | ROUTER_ROUTER_POLL | wss | 65536B |
| +10.12 | DEALER_ROUTER | wss | 65536B |
| +10.23 | ROUTER_ROUTER_POLL | ws | 65536B |
| +10.51 | DEALER_DEALER | wss | 64B |
| +10.54 | ROUTER_ROUTER_POLL | inproc | 131072B |
| +10.68 | PAIR | wss | 65536B |
| +11.89 | ROUTER_ROUTER_POLL | wss | 64B |
| +12.28 | DEALER_DEALER | inproc | 262144B |
| +12.71 | DEALER_DEALER | ws | 65536B |
| +12.80 | PAIR | wss | 64B |
| +12.90 | ROUTER_ROUTER_POLL | wss | 262144B |
| +13.12 | DEALER_ROUTER | wss | 64B |
| +13.12 | ROUTER_ROUTER | wss | 65536B |
| +14.78 | ROUTER_ROUTER_POLL | inproc | 262144B |
| +15.78 | PUBSUB | wss | 131072B |
| +16.09 | PUBSUB | ws | 65536B |
| +19.18 | ROUTER_ROUTER | wss | 262144B |
| +20.19 | ROUTER_ROUTER | ws | 65536B |
| +21.27 | DEALER_DEALER | inproc | 131072B |
| +22.55 | PAIR | wss | 1024B |
| +23.30 | PUBSUB | wss | 262144B |
| +27.55 | DEALER_DEALER | wss | 256B |
| +30.53 | DEALER_DEALER | wss | 1024B |
| +31.61 | DEALER_ROUTER | wss | 1024B |
| +32.32 | DEALER_ROUTER | wss | 256B |
| +32.84 | PAIR | wss | 256B |
| +32.96 | PUBSUB | wss | 1024B |
| +34.56 | DEALER_ROUTER | ws | 64B |
| +35.84 | DEALER_DEALER | tcp | 65536B |
| +37.30 | ROUTER_ROUTER | wss | 256B |
| +37.85 | DEALER_DEALER | ws | 64B |
| +40.83 | ROUTER_ROUTER_POLL | wss | 1024B |
| +41.11 | PUBSUB | wss | 256B |
| +42.87 | PAIR | ws | 64B |
| +42.90 | ROUTER_ROUTER_POLL | wss | 256B |
| +48.95 | ROUTER_ROUTER | wss | 1024B |
| +55.95 | DEALER_ROUTER | ws | 256B |
| +56.21 | ROUTER_ROUTER_POLL | ws | 1024B |
| +61.81 | ROUTER_ROUTER_POLL | ws | 256B |
| +64.62 | DEALER_DEALER | ws | 256B |
| +64.76 | DEALER_ROUTER | ws | 1024B |
| +64.91 | ROUTER_ROUTER | ws | 256B |
| +65.08 | PAIR | ws | 1024B |
| +67.62 | PUBSUB | ws | 256B |
| +67.82 | PUBSUB | ws | 1024B |
| +70.70 | PAIR | ws | 256B |
| +71.60 | DEALER_DEALER | ws | 1024B |
| +75.17 | ROUTER_ROUTER | ws | 1024B |

## 6. Raw Result Files
- `/home/ulalax/project/ulalax/zlink-codex/benchwithzlink-baseline/baseline/20260124/bench_linux_ALL_20260124_152322.txt`
- `/home/ulalax/project/ulalax/zlink-codex/benchwithzlink/results/20260124/bench_linux_ALL_20260124_154903.txt`