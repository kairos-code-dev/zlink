# Python Multi PERF

Python multi perf provides the official process-isolated performance surface
under `perf/multi/`.

This suite must stay aligned with:

- `bindings/README.md` perf policy
- `doc/perf/PERF_POLICY.md`
- `doc/perf/PERF_MULTI_TEST_POLICY.md`
- [README.md](/home/hep7/project/kairos/zlink/bindings/python/perf/README.md)

## Entrypoints

- `./perf/multi/run_benchmarks.sh`
- `./perf/run_benchmarks_multi.sh`

The shell wrapper delegates directly to `run_benchmarks.py`, which orchestrates
server and client processes without introducing a second benchmark contract.

## Patterns

Canonical multi patterns are:

- `PUBSUB`
- `DEALER_ROUTER`
- `DEALER_DEALER`
- `ROUTER_ROUTER`
- `STREAM`
- `SPOT`

Result line names emitted by the suite are:

- `MULTI_PUBSUB`
- `MULTI_DEALER_ROUTER`
- `MULTI_DEALER_DEALER`
- `MULTI_ROUTER_ROUTER`
- `MULTI_STREAM`
- `MULTI_SPOT`

Each pattern keeps its own server entrypoint. Client entrypoints stay explicit
except for `STREAM`, which uses the shared core `perf_stream_client` contract
required by the perf policy and execution guide.

## Transport And Receive Modes

- transport: `tcp`
- `recv` mode: all patterns
- `callback` mode: `MULTI_SPOT`, `MULTI_STREAM`

The allowed callback subset is intentionally narrow because the suite should
reflect the real public cost model, not force callback support where the public
surface is not canonical.

## Verification

Normal smoke path:

```bash
./perf/multi/run_benchmarks.sh --pattern PUBSUB --msg-sizes 64 --clients 2 --warmup 0.1 --duration 0.2
```

Output must contain `RESULT,current,...` lines for the selected pattern.
Saved reports must use `results/multi/report/perf_<platform>_<recv_mode>_...txt`.
