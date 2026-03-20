# 2026-03-20 full perf matrix validation update

## Scope

Linux workspace validation on 2026-03-20 with:

- single: `./core/perf/run_benchmarks.sh --reuse-build --msg-sizes 64 --warmup 1 --duration 1`
- single callback spot:
  `./core/perf/run_benchmarks.sh --reuse-build --pattern SPOT --transports tcp,tls,ws,wss --msg-sizes 64 --warmup 1 --duration 1 --recv callback`
- multi recv:
  `./core/perf/run_benchmarks_multi.sh --reuse-build --msg-sizes 64 --clients 1 --warmup 1 --duration 1`
- multi callback spot/stream:
  `./core/perf/run_benchmarks_multi.sh --reuse-build --pattern MULTI_SPOT,MULTI_STREAM --transports tcp,tls,ws,wss --msg-sizes 64 --clients 1 --warmup 1 --duration 1 --recv callback`

This document is a validation summary only. It does not replace the detailed
root-cause bug reports already tracked elsewhere.

## Single current status

### recv

Pass:

- `PAIR`: `tcp`, `ws`, `inproc`, `ipc`
- `GATEWAY`: `tcp`, `tls`, `ws`, `wss`
- `SPOT`: `tcp`, `tls`, `ws`, `wss`

Fail:

- `PAIR`: `tls`, `wss` -> `no_data`
- `PUBSUB`: all tested transports -> `no_data`
- `DEALER_DEALER`: all tested transports -> `no_data`
- `DEALER_ROUTER`: all tested transports -> `no_data`
- `ROUTER_ROUTER`: all tested transports -> `no_data`

Related existing tracker:

- [`doc/bug/2026-03-20-single-current-socket-matrix-gaps.md`](/home/hep7/project/kairos/zlink/doc/bug/2026-03-20-single-current-socket-matrix-gaps.md)

### callback

Pass:

- `SPOT`: `tcp`, `tls`, `ws`, `wss`

Note:

- raw `PUBSUB` callback is not a supported public surface; perf contract test now
  checks `ENOTSUP` instead of expecting success.

## Multi current status

### recv

Pass:

- `MULTI_DEALER_DEALER`: `tcp`, `ws`
- `MULTI_DEALER_ROUTER`: `tcp`, `ws`
- `MULTI_ROUTER_ROUTER`: `tcp`, `ws`
- `MULTI_PUBSUB`: `tcp`, `ws`
- `MULTI_GATEWAY`: `tcp`, `tls`, `ws`, `wss`
- `MULTI_SPOT`: `tcp`, `tls`, `ws`
- `MULTI_STREAM`: `tcp`, `ws`

Fail:

- `MULTI_DEALER_DEALER`: `tls`, `wss` -> `server_exit_before_ready_1_size_64`
- `MULTI_DEALER_ROUTER`: `tls`, `wss` -> `server_exit_before_ready_1_size_64`
- `MULTI_ROUTER_ROUTER`: `tls`, `wss` -> `server_exit_before_ready_1_size_64`
- `MULTI_PUBSUB`: `tls`, `wss` -> `server_exit_before_ready_1_size_64`
- `MULTI_SPOT`: `wss` -> `non_zero_exit_1_size_64`
- `MULTI_STREAM`: `tls`, `wss` -> `server_exit_before_ready_1_size_64`

Related existing tracker:

- [`doc/bug/2026-03-20-multi-recv-matrix-gaps.md`](/home/hep7/project/kairos/zlink/doc/bug/2026-03-20-multi-recv-matrix-gaps.md)

### callback

Pass:

- `MULTI_SPOT`: `tcp`, `tls`, `ws`

Fail:

- `MULTI_SPOT`: `wss` -> `non_zero_exit_1_size_64`
- `MULTI_STREAM`: `tcp`, `ws` -> client exits with `connect_ok=0`, no result metrics
- `MULTI_STREAM`: `tls`, `wss` -> `server_exit_before_ready_1_size_64`

Related existing tracker:

- [`doc/bug/2026-03-19-core-socket-callback-support-gap.md`](/home/hep7/project/kairos/zlink/doc/bug/2026-03-19-core-socket-callback-support-gap.md)
- [`doc/bug/2026-03-20-multi-spot-server-exit-before-ready.md`](/home/hep7/project/kairos/zlink/doc/bug/2026-03-20-multi-spot-server-exit-before-ready.md)

## Policy work completed in this workspace

- perf start gate uses monitor delivery-ready events only
- perf monitor consumption is callback-only
- perf no longer uses `monitor_snapshot`
- perf hot path no longer uses `yield` or ad-hoc `sleep_for(1ms)`
- perf policy tests no longer use `monitor_snapshot`

## Remaining blockers

- public monitor event surface still has no queue-depth telemetry; tracked in
  [`2026-03-20-monitor-event-only-queue-pending-gap.md`](/home/hep7/project/kairos/zlink/doc/bug/perf/2026-03-20-monitor-event-only-queue-pending-gap.md)
- multiple pattern/transport matrix failures remain outside the specific
  policy-refactor changes made in this workspace
- PowerShell runtime validation is still blocked because `pwsh`/`powershell`
  is unavailable in this Linux session
