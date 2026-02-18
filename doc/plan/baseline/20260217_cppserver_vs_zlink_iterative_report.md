# cppserver vs zlink STREAM Iterative Report (2026-02-17)

## Test Command

```bash
./core/tests/scenario/stream/run_stream_compare.sh \
  --stack <cppserver|zlink> \
  --size <64|1024> \
  --ccu 10000 \
  --duration 5 \
  --repeats 3 \
  --inflight 1 \
  --client-io-threads 4 \
  --server-io-threads 2
```

## Baseline Gap (Before Improvements)

Source:

- cppserver: `20260217_cmp_cppserver_s64/summary.json`, `20260217_cmp_cppserver_s1024/summary.json`
- zlink(before): `20260217_cmp_zlink_s64/summary.json`, `20260217_cmp_zlink_s1024/summary.json`

| size | metric | cppserver | zlink(before) | zlink vs cppserver |
|---:|---|---:|---:|---:|
| 64 | throughput_msg_s | 390650.20 | 109720.00 | -71.91% |
| 64 | p95_us | 26802.95 | 173008.18 | +545.48% |
| 64 | p99_us | 31951.77 | 1261140.07 | +3847.01% |
| 1024 | throughput_msg_s | 343005.00 | 130674.80 | -61.90% |
| 1024 | p95_us | 29692.63 | 74391.99 | +150.54% |
| 1024 | p99_us | 32681.09 | 101059.29 | +209.23% |

## Applied Improvements

1. STREAM single-frame fastpath receive/send in zlink scenario server:
   - enable `ZLINK_STREAM_SINGLE_FRAME_RECV`
   - use message routing-id metadata path (`zlink_msg_get_routing_id`) for echo
   - remove multipart routing-id frame echo path
2. Remove per-message `ZLINK_RCVMORE` getsockopt call from hot path.
3. Core STREAM recv hot path simplification:
   - `core/src/sockets/stream.cpp` single-frame branch avoids recursive prefetch-move path.
4. zlink scenario server parallelization:
   - `core/tests/scenario/stream/zlink/test_scenario_stream_zlink.cpp`
   - `--io-threads` is used to start multiple zlink STREAM workers (SO_REUSEPORT fan-in),
     reducing single-thread application bottleneck.

## Final Comparison (After Improvements)

Source:

- cppserver(rerun): `20260217_cmp_cppserver_s64_rerun/summary.json`, `20260217_cmp_cppserver_s1024_rerun/summary.json`
- zlink(final): `20260217_cmp_zlink_s64_after_multisock/summary.json`, `20260217_cmp_zlink_s1024_after_multisock/summary.json`

| size | metric | cppserver | zlink(final) | zlink vs cppserver | zlink vs zlink(before) |
|---:|---|---:|---:|---:|---:|
| 64 | throughput_msg_s | 391252.20 | 281100.80 | -28.15% | +156.20% |
| 64 | p95_us | 26738.13 | 38154.06 | +42.70% | -77.95% |
| 64 | p99_us | 28682.65 | 42496.60 | +48.16% | -96.63% |
| 1024 | throughput_msg_s | 351254.60 | 263153.40 | -25.08% | +101.38% |
| 1024 | p95_us | 29368.80 | 40182.71 | +36.82% | -45.99% |
| 1024 | p99_us | 32717.28 | 47911.62 | +46.44% | -52.59% |

## Summary

- zlink STREAM is still behind cppserver, but the gap was reduced substantially.
- throughput gap improved from ~62-72% down to ~25-28%.
- tail latency gap improved from several-x to roughly ~1.37-1.48x.
