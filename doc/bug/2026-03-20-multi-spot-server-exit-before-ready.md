# multi `SPOT` server exits before `READY` in callback smoke

## Summary

현재 multi perf `SPOT --recv callback` 은 runner 기준으로
`server_exit_before_ready_1` 로 실패한다.

direct 실행으로도 `comp_src_spot_server current tcp` 가
아무 로그 없이 `exit 1` 하는 것을 확인했다.

즉 client-side handshake 문제가 아니라,
server가 `READY,<endpoint>` 를 출력하기 전 초기화 단계에서 이미 실패하고 있다.

## Why this is a bug

monitoring/perf policy 기준으로 `SPOT` start gate는 service delivery-ready event를
사용해야 한다.

- sub: `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED`
- pub: `ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED`

하지만 현재는 그 이벤트를 기다리기 전 단계에서 server가 죽는다.

따라서 이 문제는 perf handshake 미구현이 아니라
multi `SPOT` server init bug다.

## Reproduction

### 1. Runner smoke

```bash
timeout 45s env PYTHONDONTWRITEBYTECODE=1 \
  ./core/perf/run_benchmarks_multi.sh \
  --pattern SPOT \
  --recv callback \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 1 \
  --duration 1 \
  --clients 2
```

Actual result:

- failure: `SPOT current tcp 64B: server_exit_before_ready_1_size_64`

### 2. Direct server reproduction

```bash
timeout 10s env PYTHONDONTWRITEBYTECODE=1 PERF_DEBUG=1 \
  PERF_RECV_MODE=callback PERF_MSG_SIZES=64 \
  PERF_WARMUP_SECONDS=1 PERF_DURATION_SECONDS=1 \
  PERF_SETTLE_MS=500 PERF_CLIENTS=2 \
  ./core/build/bin/comp_src_spot_server current tcp
```

Actual result:

- exit code `1`
- no `READY,<endpoint>` line
- no result lines

## Scope narrowed by current evidence

server code상 `READY,<endpoint>` 출력 전에 실패할 수 있는 지점은 아래다.

- `zlink_spot_node_new()`
- `configure_spot_tls_server()`
- `bind_spot_endpoint()`
- `apply_spot_server_options()`
- `zlink_send_ready_handler(pub, &spot_server_send_ready, NULL)`

현재 증상상 `run_server_loop()` 나 delivery-ready wait 이전 단계다.

## Expected result

- multi `SPOT` server가 정상적으로 endpoint bind 후 `READY,<endpoint>` 를 출력해야 한다.
- 그 다음에만 runner가 `CLIENT_READY` / `START,<size>` 순서로 진행해야 한다.
- callback mode smoke가 result lines 를 출력하고 종료해야 한다.

## Non-goals

아래는 해결로 인정하지 않는다.

- runner에서 `SPOT`만 server-ready check를 약화
- service ready 없이 client를 먼저 띄워서 타이밍으로 통과시키기
- sleep/retry 를 넣어 조기 종료를 숨기기

## Suspected fix areas

- [`core/perf/multi/src/perf_multi_spot_server.cpp`](../../bindings/c/perf/multi/src/perf_multi_spot_server.cpp)
  의 `apply_spot_server_options()` / `zlink_send_ready_handler()` attach
- core `spot_node` / `spot_pub` send-ready handler attach contract
- `SPOT` service public option support matrix

## Current repo decision

- 이 문제는 perf workaround로 닫지 않는다.
- multi `SPOT` server init bug로 추적한다.
