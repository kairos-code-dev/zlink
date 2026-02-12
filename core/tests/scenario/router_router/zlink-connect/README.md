# zlink-connect Router-Router Scenario

- Source: `core/tests/scenario/router_router/zlink-connect/test_scenario_router_router_zlink_connect.cpp`
- Purpose: same flow/logic as `zmq/libzmq_native_router_router_bench.cpp`, but linked to zlink.
- Target: `test_scenario_router_router_zlink_connect`

## Build

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build --target test_scenario_router_router_zlink_connect -j"$(nproc)"
```

## Quick Run

```bash
LD_LIBRARY_PATH=core/build/lib core/build/bin/test_scenario_router_router_zlink_connect \
  --self-connect 1 --size 1024 --ccu 10000 --inflight 10 --duration 10 --senders 1
```

## Batch Run

```bash
core/tests/scenario/router_router/zlink-connect/run_zlink_connect_scenarios.sh
```

Warmup mode options:

- default: stable warmup
- `--warmup-legacy`: legacy warmup loop
- `--warmup-none`: start benchmark traffic immediately without ready gate

Logs are written to:

`core/tests/scenario/router_router/zlink-connect/result/<timestamp>/`

## Output Metrics

The runner prints standard summary lines and one machine-readable line:

```text
METRIC ready_wait_ms=... first_connected_ms=... first_connection_ready_ms=... connect_to_ready_ms=... ...
```

Event counters collected from monitor:

- `connected`: `ZLINK_EVENT_CONNECTED`
- `connection_ready`: `ZLINK_EVENT_CONNECTION_READY`
- `connect_delayed`: `ZLINK_EVENT_CONNECT_DELAYED`
- `connect_delayed_zero`: delayed events with `value == 0`
- `connect_delayed_nonzero`: delayed events with `value != 0`
- `connect_retried`: `ZLINK_EVENT_CONNECT_RETRIED`
- `disconnected`: `ZLINK_EVENT_DISCONNECTED`
- `handshake_failed`: handshake failure events (`NO_DETAIL/PROTOCOL/AUTH`)

Timing fields:

- `ready_wait_ms`: warmup start to ready condition
- `first_connected_ms`: first `CONNECTED` event offset from run start
- `first_connection_ready_ms`: first `CONNECTION_READY` event offset from run start
- `connect_to_ready_ms`: `first_connection_ready_ms - first_connected_ms` (or `-1` if unavailable)
