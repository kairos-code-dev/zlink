# STREAM Retest Report with Workspace `run_stream_compare.sh` (2026-02-17)

## Sync Status

- workspace (`/home/hep7/project/kairos/zlink`) `main` is up to date with `origin/main`:
  - commit: `64a1354b`
- copied script:
  - from: `zlink/core/tests/scenario/stream/run_stream_compare.sh`
  - to: `zlink-perf-wt/core/tests/scenario/stream/run_stream_compare.sh`
- conflict: none

## Retest Commands

```bash
./core/tests/scenario/stream/run_stream_pr_gate.sh \
  --result-root doc/plan/baseline/20260217_final_pr_gate_workspace_script \
  --ccu 1000 --duration 5 --repeats 3 --inflight 1 \
  --client-io-threads 1 --server-io-threads 1

./core/tests/scenario/stream/run_stream_pr_gate.sh \
  --result-root doc/plan/baseline/20260217_final_pr_gate_workspace_script_rerun2 \
  --ccu 1000 --duration 5 --repeats 3 --inflight 1 \
  --client-io-threads 1 --server-io-threads 1
```

## Baseline vs Retest (Median)

Baseline:

- `doc/plan/baseline/20260217_phase0_zlink64/summary.json`
- `doc/plan/baseline/20260217_phase0_zlink1024/summary.json`

Retest runs:

- run1: `doc/plan/baseline/20260217_final_pr_gate_workspace_script`
- run2: `doc/plan/baseline/20260217_final_pr_gate_workspace_script_rerun2`

| size | run | throughput_msg_s | delta vs baseline | p99_us | delta vs baseline |
|---:|---|---:|---:|---:|---:|
| 64 | run1 | 248681.60 | +6.01% | 5907.24 | -9.46% |
| 64 | run2 | 258231.60 | +10.08% | 5722.32 | -12.29% |
| 64 | avg(run1,run2) | 253456.60 | +8.04% | 5814.78 | -10.87% |
| 1024 | run1 | 215596.80 | -4.14% | 6299.27 | -1.25% |
| 1024 | run2 | 211267.00 | -6.07% | 6376.62 | -0.04% |
| 1024 | avg(run1,run2) | 213431.90 | -5.11% | 6337.94 | -0.65% |

## Comparison with Previous Gate Result

Previous reference (`doc/plan/baseline/20260217_final_pr_gate`) medians:

- 64: `263249.20` (`+12.22%`), p99 `5669.44` (`-13.10%`)
- 1024: `220036.20` (`-2.17%`), p99 `6047.12` (`-5.21%`)

Observation:

- 64B remains improved vs baseline in workspace-script retests.
- 1024B throughput dropped more than previous gate run.
- `run_stream_pr_gate.sh` failed in both workspace-script retests due 1024 throughput threshold:
  - run1 ratio: `0.9586` (< `0.9700`)
  - run2 ratio: `0.9393` (< `0.9700`)
