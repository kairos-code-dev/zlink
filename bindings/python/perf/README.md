# Python Perf

Python binding perf follows the `core/perf` and `doc/perf` policy shape with
separate single and multi suites.

Available entrypoints:

- `./perf/run_benchmarks.sh`
- `./perf/run_benchmarks_multi.sh`
- `./perf/single/run_benchmarks.sh`
- `./perf/multi/run_benchmarks.sh`

Current scope:

- single:
  - recv mode: `callback`
  - patterns: `PAIR`, `PUBSUB`, `DEALER_ROUTER`, `SPOT`
  - transport: `inproc`
- multi:
  - recv mode: `recv`
  - patterns: `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`,
    `SPOT`, `STREAM`
  - callback mode: `SPOT`, `STREAM`
  - transport: `tcp`

Each pattern has its own file under `perf/single/` or `perf/multi/` and prints
official-style `RESULT,current,...` lines.
