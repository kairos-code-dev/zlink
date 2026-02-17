# Performance Baseline Measurements

This directory stores performance baseline measurements for the zlink stream
engine. Baseline numbers must be captured **before and after** each
optimization phase so that improvements (or regressions) can be quantified.

## Environment Requirements

Before running any benchmarks, ensure the following environment settings are
applied to get stable, reproducible results:

| Setting | Command / Check |
|---------|----------------|
| CPU frequency governor | `sudo cpupower frequency-set -g performance` |
| File descriptor limit | `ulimit -n 65536` (or higher) |
| Core dumps disabled | `ulimit -c 0` |
| Kernel same-page merging off | Check `/sys/kernel/mm/ksm/run` is `0` |
| No other CPU-intensive processes | `top` / `htop` to verify idle system |
| Turbo boost consistent | Either always-on or always-off across runs |

## Baseline Commands

Run from the stream scenario directory:

```bash
cd core/tests/scenario/stream
./run_stream_compare.sh --stack zlink --size 64 --ccu 1000 --duration 5 --repeats 3
./run_stream_compare.sh --stack zlink --size 1024 --ccu 1000 --duration 5 --repeats 3
./run_stream_compare.sh --stack zlink --size 65536 --ccu 100 --duration 5 --repeats 3
```

### Parameter Summary

| Param | 64 B run | 1 KiB run | 64 KiB run |
|-------|----------|-----------|------------|
| `--size` | 64 | 1024 | 65536 |
| `--ccu` | 1000 | 1000 | 100 |
| `--duration` | 5 s | 5 s | 5 s |
| `--repeats` | 3 | 3 | 3 |

The small-message / high-CCU runs stress per-message overhead (syscall and
allocation cost), while the large-message / lower-CCU run stresses memcpy
throughput in the recv path.

## Recording Results

Save the output of each run into this directory with a descriptive filename,
for example:

```
baseline_pre-phase1_64B_ccu1000.txt
baseline_post-phase1_64B_ccu1000.txt
```

Actual measurements should be taken before and after each optimization phase
to track incremental gains.
