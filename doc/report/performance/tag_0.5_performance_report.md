# tag_0.5 Performance Report

## Benchmark Setup
- Date: 2026-01-24
- Runs: 10
- CPU pinning: disabled (`--no-taskset`)
- benchwithzmq baseline file: `/home/ulalax/project/ulalax/zlink-codex/benchwithzmq/baseline/20260124/bench_linux_ALL_20260124_105245.txt`
- benchwithzlink result file: `/home/ulalax/project/ulalax/zlink-codex/benchwithzlink/results/20260124/bench_linux_ALL_20260124_113726.txt`

## zlink vs standard libzmq (benchwithzmq)
Median throughput diff across sizes per pattern/transport (positive = zlink faster).

| Pattern | Transport | Median Diff (%) | Min Diff (%) | Max Diff (%) |
|---------|-----------|-----------------|--------------|--------------|
| DEALER_DEALER | inproc | -4.01 | -33.02 | +12.34 |
| DEALER_DEALER | ipc | -2.50 | -26.06 | +0.96 |
| DEALER_DEALER | tcp | -4.33 | -9.49 | +1.43 |
| DEALER_ROUTER | inproc | +4.58 | -11.35 | +22.34 |
| DEALER_ROUTER | ipc | -4.52 | -27.10 | -2.20 |
| DEALER_ROUTER | tcp | -4.49 | -21.15 | +0.23 |
| PAIR | inproc | -0.86 | -6.76 | +7.79 |
| PAIR | ipc | -4.72 | -13.83 | -0.84 |
| PAIR | tcp | -2.91 | -7.55 | +0.98 |
| PUBSUB | inproc | +2.52 | -5.00 | +16.29 |
| PUBSUB | ipc | -2.74 | -5.18 | +15.63 |
| PUBSUB | tcp | -0.12 | -5.53 | +2.53 |
| ROUTER_ROUTER | inproc | +1.54 | -1.44 | +9.90 |
| ROUTER_ROUTER | ipc | -2.50 | -5.91 | +0.51 |
| ROUTER_ROUTER | tcp | -1.28 | -8.41 | +0.68 |
| ROUTER_ROUTER_POLL | inproc | +2.20 | -10.01 | +7.76 |
| ROUTER_ROUTER_POLL | ipc | -1.46 | -7.09 | +1.33 |
| ROUTER_ROUTER_POLL | tcp | -2.42 | -9.16 | +21.93 |

## current vs baseline (benchwithzlink)
Median throughput diff across sizes per pattern/transport (positive = current faster).

| Pattern | Transport | Median Diff (%) | Min Diff (%) | Max Diff (%) |
|---------|-----------|-----------------|--------------|--------------|
| DEALER_DEALER | inproc | +0.63 | -2.84 | +11.05 |
| DEALER_DEALER | ipc | +1.11 | -4.40 | +6.60 |
| DEALER_DEALER | tcp | +1.28 | -0.90 | +15.09 |
| DEALER_DEALER | tls | +0.57 | -2.10 | +2.01 |
| DEALER_DEALER | ws | +21.84 | -8.54 | +71.45 |
| DEALER_DEALER | wss | +16.93 | -0.29 | +65.29 |
| DEALER_ROUTER | inproc | +0.36 | -3.00 | +2.45 |
| DEALER_ROUTER | ipc | +1.15 | -1.11 | +3.91 |
| DEALER_ROUTER | tcp | +0.46 | -3.15 | +2.08 |
| DEALER_ROUTER | tls | -0.38 | -1.38 | +0.62 |
| DEALER_ROUTER | ws | +23.18 | -8.58 | +62.78 |
| DEALER_ROUTER | wss | +20.09 | +0.97 | +61.89 |
| PAIR | inproc | +2.62 | -3.82 | +9.91 |
| PAIR | ipc | -0.38 | -1.26 | +2.95 |
| PAIR | tcp | -0.79 | -5.74 | +1.66 |
| PAIR | tls | -0.20 | -1.13 | +1.37 |
| PAIR | ws | +20.23 | -7.13 | +64.37 |
| PAIR | wss | +19.56 | -0.05 | +65.51 |
| PUBSUB | inproc | -0.19 | -13.54 | +4.23 |
| PUBSUB | ipc | -1.25 | -5.02 | +2.54 |
| PUBSUB | tcp | +0.72 | -2.81 | +2.25 |
| PUBSUB | tls | +0.22 | -0.62 | +1.73 |
| PUBSUB | ws | +2.64 | -6.68 | +65.99 |
| PUBSUB | wss | +22.68 | +7.75 | +69.81 |
| ROUTER_ROUTER | inproc | +0.48 | -10.44 | +3.94 |
| ROUTER_ROUTER | ipc | -0.95 | -6.91 | +4.81 |
| ROUTER_ROUTER | tcp | +0.53 | -1.77 | +1.57 |
| ROUTER_ROUTER | tls | -0.73 | -2.11 | +1.60 |
| ROUTER_ROUTER | ws | -0.90 | -8.98 | +56.52 |
| ROUTER_ROUTER | wss | +22.80 | +0.26 | +63.27 |
| ROUTER_ROUTER_POLL | inproc | -0.11 | -0.82 | +1.85 |
| ROUTER_ROUTER_POLL | ipc | -0.99 | -3.25 | +4.43 |
| ROUTER_ROUTER_POLL | tcp | -0.36 | -4.40 | +7.44 |
| ROUTER_ROUTER_POLL | tls | -0.58 | -4.82 | +1.08 |
| ROUTER_ROUTER_POLL | ws | -0.17 | -11.54 | +62.65 |
| ROUTER_ROUTER_POLL | wss | +18.85 | -0.86 | +67.35 |
| STREAM | tcp | +1.86 | -3.98 | +3.06 |
| STREAM | tls | +0.49 | -3.57 | +6.12 |
| STREAM | ws | -0.84 | -3.00 | +3.68 |
| STREAM | wss | +1.41 | -0.19 | +4.59 |

## Notable Throughput Changes (current vs baseline)
Changes >= ±10% (throughput only).

| Diff (%) | Pattern | Transport | Size |
|----------|---------|-----------|------|
| -13.54 | PUBSUB | inproc | 65536B |
| -11.54 | ROUTER_ROUTER_POLL | ws | 131072B |
| -10.44 | ROUTER_ROUTER | inproc | 262144B |
| +11.05 | DEALER_DEALER | inproc | 131072B |
| +12.38 | ROUTER_ROUTER | wss | 262144B |
| +13.84 | ROUTER_ROUTER_POLL | wss | 262144B |
| +15.09 | DEALER_DEALER | tcp | 65536B |
| +18.50 | PUBSUB | wss | 131072B |
| +23.86 | ROUTER_ROUTER_POLL | wss | 64B |
| +26.86 | PUBSUB | wss | 64B |
| +28.86 | DEALER_DEALER | wss | 64B |
| +33.21 | DEALER_ROUTER | wss | 64B |
| +33.21 | ROUTER_ROUTER | wss | 64B |
| +35.05 | PAIR | wss | 64B |
| +37.38 | DEALER_DEALER | ws | 64B |
| +37.48 | PAIR | ws | 64B |
| +43.34 | DEALER_ROUTER | ws | 64B |
| +47.02 | ROUTER_ROUTER | ws | 256B |
| +51.15 | ROUTER_ROUTER_POLL | ws | 256B |
| +54.29 | DEALER_ROUTER | ws | 256B |
| +56.52 | ROUTER_ROUTER | ws | 1024B |
| +57.41 | ROUTER_ROUTER_POLL | wss | 256B |
| +57.42 | ROUTER_ROUTER | wss | 256B |
| +60.37 | DEALER_ROUTER | wss | 256B |
| +61.89 | DEALER_ROUTER | wss | 1024B |
| +62.65 | ROUTER_ROUTER_POLL | ws | 1024B |
| +62.78 | DEALER_ROUTER | ws | 1024B |
| +63.01 | DEALER_DEALER | wss | 256B |
| +63.16 | PAIR | ws | 256B |
| +63.27 | ROUTER_ROUTER | wss | 1024B |
| +63.67 | DEALER_DEALER | ws | 256B |
| +64.37 | PAIR | ws | 1024B |
| +65.07 | PAIR | wss | 256B |
| +65.13 | PUBSUB | ws | 1024B |
| +65.29 | DEALER_DEALER | wss | 1024B |
| +65.50 | PUBSUB | wss | 256B |
| +65.51 | PAIR | wss | 1024B |
| +65.99 | PUBSUB | ws | 256B |
| +67.35 | ROUTER_ROUTER_POLL | wss | 1024B |
| +69.81 | PUBSUB | wss | 1024B |
| +71.45 | DEALER_DEALER | ws | 1024B |

## Raw Result Files
- `/home/ulalax/project/ulalax/zlink-codex/benchwithzmq/baseline/20260124/bench_linux_ALL_20260124_105245.txt`
- `/home/ulalax/project/ulalax/zlink-codex/benchwithzlink/results/20260124/bench_linux_ALL_20260124_113726.txt`
