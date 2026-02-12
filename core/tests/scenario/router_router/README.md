# Router-Router Scenarios

Folder split:

- `core/tests/scenario/router_router/zlink/`: C zlink scenario runner (LZ-01..LZ-05).
- `core/tests/scenario/router_router/zlink-connect/`: zlink runner with the same core flow as the `zmq` comparison bench.
- `core/tests/scenario/router_router/zmq/`: libzmq-native comparison runner.
- `core/tests/scenario/router_router/run_connect_diff_check.sh`: repeat-run comparator for zlink vs libzmq timing/event deltas.

## zlink Build

```bash
cmake -S core -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build --target test_scenario_router_router -j"$(nproc)"
```

## zlink Quick Run

```bash
core/tests/scenario/router_router/zlink/run_lz_scenarios.sh
```

Logs are written under:

`core/tests/scenario/router_router/zlink/result/<timestamp>/`

## zlink-vs-zmq Diff Check

```bash
core/tests/scenario/router_router/run_connect_diff_check.sh
```

Example:

```bash
REPEATS=10 DURATION=4 SIZE=1024 CCU=10000 INFLIGHT=10 \
core/tests/scenario/router_router/run_connect_diff_check.sh
```

Environment variables:

- `REPEATS`: runs per lib per self mode (default `5`)
- `SIZE`: payload size (default `1024`)
- `CCU`: stage id modulus (default `10000`)
- `INFLIGHT`: max outstanding messages (default `10`)
- `DURATION`: benchmark seconds per run (default `4`)
- `SENDERS`: producer thread count (default `1`)
- `HWM`: `SNDHWM` / `RCVHWM` value (default `1000000`)
- `WARMUP_MODE`: `stable` (default), `legacy`, `none`
- `RATIO_LIMIT`: zlink/zmq median ratio threshold (default `3.0`)
- `ABS_LIMIT_MS`: absolute fail threshold when zmq median is `0` (default `5`)
- `BASE_PORT`: first port used by diff runs (default `33000`)
- `LOG_DIR`: explicit output directory
- `LIBZMQ_INCLUDE`: libzmq include directory for local compile
- `LIBZMQ_LIBDIR`: libzmq shared library directory for local link/run

Outputs:

- `core/tests/scenario/router_router/result/diff_check_<timestamp>/metrics.csv`
- `core/tests/scenario/router_router/result/diff_check_<timestamp>/SUMMARY.txt`

`metrics.csv` columns:

- `lib,self,run`
- `ready_wait_ms,first_connected_ms,first_connection_ready_ms,connect_to_ready_ms`
- `connected,connection_ready,connect_delayed,connect_delayed_zero,connect_delayed_nonzero`
- `connect_retried,disconnected,handshake_failed,throughput_msg_s,rc`

Fail criteria used by `run_connect_diff_check.sh`:

- Any run with non-zero `rc`
- Median `ready_wait_ms` ratio (`zlink/zmq`) greater than `RATIO_LIMIT`
- Median `connect_to_ready_ms` ratio (`zlink/zmq`) greater than `RATIO_LIMIT`
- If `zmq` median `connect_to_ready_ms <= 0` and `zlink` median exceeds `ABS_LIMIT_MS`
- Any non-zero zlink sum of `connect_retried`, `disconnected`, `handshake_failed`

Notes:

- zlink and zmq can differ in `connect_delayed` value semantics.
- `connect_delayed_zero/nonzero` is intentionally separated to make this visible.
