# Node Perf

Node perf now exposes policy-shaped single and multi runners for the aligned
binding surface.

Available entrypoints:

- `./perf/run_benchmarks.sh`
- `./perf/run_benchmarks_multi.sh`
- `./perf/single/run_benchmarks.sh`
- `./perf/multi/run_benchmarks.sh`

Current implemented scope:

- single patterns:
  - `PAIR`
  - `PUBSUB`
  - `DEALER_ROUTER`
  - `SPOT`
- single recv mode:
  - `callback` only
- multi patterns:
  - `STREAM`
- multi recv modes:
  - `recv`
  - `callback`

Result files are written under:

- `perf/results/single/report/`
- `perf/results/multi/report/`

The runners emit official-style `RESULT,current,...` lines and keep the pattern
hot path inside each benchmark file.
