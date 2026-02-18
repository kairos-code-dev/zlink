# STREAM Final Performance Report (2026-02-17)

## Measurement Setup

- worktree: `/home/hep7/project/kairos/zlink-perf-wt`
- branch: `perf/stream-phase0-5-codex-hybrid`
- target: `zlink` STREAM
- comparison baseline:
  - `doc/plan/baseline/20260217_phase0_zlink64/summary.json`
  - `doc/plan/baseline/20260217_phase0_zlink1024/summary.json`
- final run (PR gate run):
  - `doc/plan/baseline/20260217_final_pr_gate/zlink_s64/summary.json`
  - `doc/plan/baseline/20260217_final_pr_gate/zlink_s1024/summary.json`

Command:

```bash
./core/tests/scenario/stream/run_stream_pr_gate.sh \
  --result-root doc/plan/baseline/20260217_final_pr_gate \
  --ccu 1000 \
  --duration 5 \
  --repeats 3 \
  --inflight 1 \
  --client-io-threads 1 \
  --server-io-threads 1
```

## Median Comparison (Baseline -> Final)

| size | metric | baseline | final | delta |
|---:|---|---:|---:|---:|
| 64 | throughput_msg_s | 234588.40 | 263249.20 | +12.22% |
| 64 | throughput_mib_s | 28.64 | 32.13 | +12.19% |
| 64 | p50_us | 4064.75 | 3601.62 | -11.39% |
| 64 | p95_us | 5730.64 | 4857.64 | -15.23% |
| 64 | p99_us | 6524.23 | 5669.44 | -13.10% |
| 1024 | throughput_msg_s | 224915.20 | 220036.20 | -2.17% |
| 1024 | throughput_mib_s | 439.29 | 429.76 | -2.17% |
| 1024 | p50_us | 4244.05 | 4417.41 | +4.08% |
| 1024 | p95_us | 5638.96 | 5476.90 | -2.87% |
| 1024 | p99_us | 6379.32 | 6047.12 | -5.21% |

## KPI Assessment

- improvement KPI (`throughput +10%` or `p99 -15%`):
  - **met** on `64B` (`throughput +12.22%`)
- non-regression KPI (`throughput >= -3%`, `p99 <= +5%`):
  - `64B`: pass
  - `1024B`: pass (`throughput -2.17%`, `p99 -5.21%`)
- gate result:
  - `run_stream_pr_gate.sh`: **PASS**

## Notes

- an earlier ad-hoc run (`20260217_final_zlink64`, `20260217_final_zlink1024`) showed severe variance and one retry; those numbers were not used for final assessment.
- final KPI judgment is based on the gate run result directory `20260217_final_pr_gate`.
