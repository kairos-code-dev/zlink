# multi `PUBSUB` client exits during `SUB` monitor init in both `recv` and `callback`

## Summary

현재 multi perf `PUBSUB`는 handshake policy를 delivery-ready event 기준으로 바꾼 뒤에도
완료되지 않는다.

하지만 남은 문제는 perf handshake 로직이 아니라 client-side `SUB` 초기화 경로 자체다.

- server (`XPUB`) 는 정상적으로 bind하고 결과를 출력한다.
- client (`SUB`) 는 socket/monitor option 세팅 로그만 남기고 `exit 1`로 종료한다.
- 같은 증상은 `recv`/`callback` 모드 모두 동일하다.

즉 perf 쪽 fallback, sleep, snapshot polling으로 가릴 문제가 아니라
`SUB` + socket monitor + delivery-ready attach 초기화 contract bug로 추적해야 한다.

## Why this is a bug

perf policy와 monitoring guide는 `PUBSUB` start gate를 아래 이벤트로 정의한다.

- `SUB_DELIVERY_READY_CHANGED(value=1)`
- `PUB_DELIVERY_READY_CHANGED(value=1)`

그런데 현재는 그 이벤트를 기다리기 전 단계에서 client가 이미 종료한다.

- `run_benchmarks_multi.sh --pattern PUBSUB --recv recv`
- `run_benchmarks_multi.sh --pattern PUBSUB --recv callback`

둘 다 동일하게 partial/exit 1 이다.

따라서 이 문제는 "perf handshake 미구현"이 아니라
core의 `SUB` monitor/ready initialization bug다.

## Reproduction

### 1. Runner smoke

```bash
timeout 45s env PYTHONDONTWRITEBYTECODE=1 PERF_DEBUG=1 \
  ./core/perf/run_benchmarks_multi.sh \
  --pattern PUBSUB \
  --recv recv \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 0 \
  --duration 1 \
  --clients 2
```

```bash
timeout 45s env PYTHONDONTWRITEBYTECODE=1 PERF_DEBUG=1 \
  ./core/perf/run_benchmarks_multi.sh \
  --pattern PUBSUB \
  --recv callback \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 0 \
  --duration 1 \
  --clients 2
```

Actual result:

- result status: `partial`
- failure suffix: `non_zero_exit_1_setsockopt(ZLINK_OPT_RCVHWM) = 1000_size_64`

이 문자열은 마지막 stdout line이 option trace였기 때문에 붙은 것이고,
실제 실패는 그 이후 client가 조용히 `exit 1` 하는 것이다.

### 2. Direct server/client reproduction

Server:

```bash
env PYTHONDONTWRITEBYTECODE=1 PERF_DEBUG=1 \
  PERF_RECV_MODE=recv PERF_MSG_SIZES=64 \
  PERF_WARMUP_SECONDS=0 PERF_DURATION_SECONDS=1 \
  PERF_SETTLE_MS=500 PERF_CLIENTS=2 \
  ./core/build/bin/comp_src_pubsub_server current tcp
```

Observed:

- `READY,tcp://127.0.0.1:<port>` 출력
- 종료 코드 `0`
- result lines 정상 출력

Client:

```bash
env PYTHONDONTWRITEBYTECODE=1 PERF_DEBUG=1 \
  PERF_RECV_MODE=recv PERF_MSG_SIZES=64 \
  PERF_WARMUP_SECONDS=0 PERF_DURATION_SECONDS=1 \
  PERF_SETTLE_MS=500 PERF_CLIENTS=2 \
  ./core/build/bin/comp_src_pubsub_client \
  current tcp 64 --endpoint tcp://127.0.0.1:<port>
```

Observed:

- 종료 코드 `1`
- stdout/stderr 마지막 로그:

```text
setsockopt(ZLINK_OPT_LINGER) = 0
setsockopt(ZLINK_OPT_SNDHWM) = 1000
setsockopt(ZLINK_OPT_RCVHWM) = 1000
```

- 그 이후 `SUB_DELIVERY_READY_CHANGED` 대기 로그나 receive-phase 로그 없이 즉시 종료

같은 재현이 `PERF_RECV_MODE=callback` 에서도 동일하다.

## Scope narrowed by current evidence

현재 재현 기준으로 아래는 정상이다.

- `XPUB` server bind/start
- server monitor open + handler attach
- server benchmark result emission

현재 깨지는 쪽은 아래 중 하나다.

- `SUB` socket monitor open/handler attach path
- `SUB` connect path 직전 또는 직후의 초기화 contract
- `SUB` delivery-ready monitor plumbing
- `run_client_benchmark()`에서 `create_client_sockets()` 실패로 바로 빠지는 조건

즉 handshake wait loop 이전이다.

## Expected result

- multi `PUBSUB` client가 `recv`/`callback` 모두에서 정상적으로 socket + monitor
  초기화를 마쳐야 한다.
- 그 다음에만 `SUB_DELIVERY_READY_CHANGED(value=1)` / `PUB_DELIVERY_READY_CHANGED(value=1)`
  기반 start gate를 기다려야 한다.
- perf는 sleep/snapshot fallback 없이 문서화된 delivery-ready contract만 사용해야 한다.

## Non-goals

아래는 해결로 인정하지 않는다.

- perf에서 `PUBSUB`만 예외 처리해서 barrier를 skip
- `setsockopt(...)` trace가 마지막이었다는 이유로 option 적용을 그냥 무시
- sleep/snapshot polling을 다시 넣어서 client 조기 종료를 가림

## Suspected fix areas

- `zlink_socket_monitor_open()` / `zlink_socket_monitor_handler()` for `SUB`
- `SUB` socket option support/validation during monitor attach path
- `SUB_DELIVERY_READY_CHANGED` event plumbing after `zlink_recv_handler()` /
  `zlink_subscribe_handler()` support 확장
- multi perf client socket creation error path logging

## Current repo decision

- 이 문제는 perf workaround로 닫지 않는다.
- core `SUB` monitor / delivery-ready initialization bug로 추적한다.
