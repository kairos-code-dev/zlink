# Python Multi Perf

Python multi perf provides process-isolated pattern files under `perf/multi/`.

Available entrypoints:

- `./perf/multi/run_benchmarks.sh`

Current scope:

- suite: `multi`
- patterns:
  - `PUBSUB`
  - `DEALER_ROUTER`
  - `DEALER_DEALER`
  - `ROUTER_ROUTER`
  - `STREAM`
  - `SPOT`
- transport: `tcp`

Each pattern has its own `perf_multi_<pattern>_server.py` and
`perf_multi_<pattern>_client.py` file.
