# Dotnet PERF

Policy-compliant perf suite for `bindings/dotnet`.

## Entrypoints

- `run_benchmarks.sh` / `run_benchmarks.ps1` (single)
- `run_benchmarks_multi.sh` / `run_benchmarks_multi.ps1` (multi)

`run_benchmarks.sh` and `run_benchmarks_multi.sh` keep the core/perf CLI
shape (options/defaults/result layout) and delegate through
`run_comparison.py` to `bindings/perf/run_policy_bench.py --binding dotnet`.

## Stream Client

For STREAM multi patterns, client execution uses the shared core stream client:

- `core/perf/common/streamclient/perf_stream_client`

The .NET multi runner handles server roles; shared C++ stream client handles
STREAM client roles.

## Results

Results are stored under:

- `results/single/{tmp,report,baseline}`
- `results/multi/{tmp,report,baseline}`

Filename format:

- `perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt`
