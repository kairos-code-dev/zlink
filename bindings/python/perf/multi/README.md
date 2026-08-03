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

- `DEALER_DEALER`
- `DEALER_ROUTER`
- `ROUTER_ROUTER`
- `PUBSUB`
- `STREAM`

Result line names emitted by the suite are:

- `MULTI_DEALER_DEALER`
- `MULTI_DEALER_ROUTER`
- `MULTI_ROUTER_ROUTER`
- `MULTI_PUBSUB`
- `MULTI_STREAM`

Each pattern keeps its own server entrypoint. Client entrypoints stay explicit
except for `STREAM`, which uses the shared core `perf_stream_client` contract
required by the perf policy and execution guide.

## Transport And Receive Modes

- policy transport matrix: `tcp`, `tls`, `ws`, `wss`
- combinations outside that matrix, or runtimes that report `protocol not
  supported`, are reported as `UNSUPPORTED,current,...`; other execution
  failures remain `fail`

The suite uses the recv path only. Server and client context I/O threads
default to `4` for Python multi perf, matching the current C multi baseline
resource profile.

`MULTI_STREAM` uses the shared core `perf_stream_client`. Python passes a
stream completion wait to that client so the slower public Python stream server
can finish in-flight replies after the active window. Override the default
`10000` ms with `PERF_MULTI_STREAM_COMPLETION_WAIT_MS` or
`PERF_STREAM_COMPLETION_WAIT_MS`.

## Verification

Normal smoke path:

```bash
./perf/multi/run_benchmarks.sh --pattern PUBSUB --msg-sizes 64 --clients 2 --duration 0.2
```

Output must contain `RESULT,current,...` lines for the selected pattern.
Saved reports must use `results/multi/report/perf_<lang>_<suite>_<platform>_...txt`.
