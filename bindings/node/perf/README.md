# Node Perf

Node perf exposes in-repo single and multi runners for the aligned binding
surface. The authoritative policy contract remains:

- [`bindings/README.md`](/home/hep7/project/kairos/zlink/bindings/README.md)
- [`doc/perf/PERF_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_POLICY.md)
- [`doc/perf/PERF_SINGLE_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_SINGLE_TEST_POLICY.md)
- [`doc/perf/PERF_MULTI_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_MULTI_TEST_POLICY.md)

Available entrypoints:

- `./perf/run_benchmarks.sh`
- `./perf/run_benchmarks_multi.sh`
- `./perf/single/run_benchmarks.sh`
- `./perf/multi/run_benchmarks.sh`

Current implemented scope:

- single patterns:
  - `PAIR`
  - `PUBSUB`
  - `DEALER_DEALER`
  - `DEALER_ROUTER`
  - `ROUTER_ROUTER`
  - `SPOT`
- single recv mode:
  - `callback` only
- multi patterns:
  - `MULTI_DEALER_DEALER`
  - `MULTI_PUBSUB`
  - `STREAM`
- multi recv modes:
  - `MULTI_DEALER_DEALER`: `recv`
  - `MULTI_PUBSUB`: `recv`
  - `STREAM`: `recv`, `callback`

Current alignment notes:

- single runner rejects `--recv` values other than `callback`
- multi runner rejects callback mode for patterns other than `STREAM`
- result files are written under the shared `perf/results/{single,multi}/report`
  layout required by policy
- benchmark code is split by pattern file, and the entry scripts select the
  pattern through `--pattern`

Result files are written under:

- `perf/results/single/report/`
- `perf/results/multi/report/`

The runners emit official-style `RESULT,current,...` lines and keep the pattern
hot path inside each benchmark file.
