# multi `PUBSUB` / `DEALER_DEALER` connect-ready monitor gap

## Summary

현재 multi perf에서 `PUBSUB`와 `DEALER_DEALER`는 `tcp` 기준 기본 연결이 성립한 뒤에도
connect-ready barrier를 통과하지 못한다.

관찰상 문제는 perf wrapper의 mode 분기보다 아래쪽에 있다.

- server monitor가 `ACCEPTED` / `CONNECTION_READY`를 0으로 유지한다.
- client monitor도 `CONNECTED` / `CONNECTION_READY`를 0으로 유지한다.
- 동시에 여러 benchmark socket option 적용이 `EINVAL`로 실패한다.

이 상태에서는 `recv`/`callback` 구현을 추가해도 full matrix를 완료할 수 없다.

## Why this is a bug

perf policy에서 mode matrix는 "기본 연결 경로가 정상"이라는 전제 위에 있다.

그런데 현재는 아래 두 패턴이 callback 이전 단계에서 이미 실패한다.

- `PUBSUB --recv recv`
- `DEALER_DEALER --recv callback`

즉 미구현이 아니라 core monitor or socket option contract bug로 취급해야 한다.

## Reproduction

### 1. `PUBSUB` runner smoke

```bash
timeout 35s env PYTHONDONTWRITEBYTECODE=1 \
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

Actual result:

- result file status: `partial`
- failure: `PUBSUB current tcp 64B: non_zero_exit_1_size_64`

### 2. Direct `PUBSUB` server/client

Server:

```bash
env PERF_DEBUG=1 PERF_RECV_MODE=recv PERF_MSG_SIZES=64 \
  PERF_WARMUP_SECONDS=0 PERF_DURATION_SECONDS=1 PERF_SETTLE_MS=500 \
  PERF_CLIENTS=2 \
  /home/hep7/project/kairos/zlink/core/build/bin/comp_src_pubsub_server \
  current tcp
```

Client:

```bash
env PERF_DEBUG=1 PERF_RECV_MODE=recv PERF_MSG_SIZES=64 \
  PERF_WARMUP_SECONDS=0 PERF_DURATION_SECONDS=1 PERF_SETTLE_MS=500 \
  PERF_CLIENTS=2 \
  /home/hep7/project/kairos/zlink/core/build/bin/comp_src_pubsub_client \
  current tcp 64 --endpoint tcp://127.0.0.1:<port>
```

Observed logs:

- server: `[perf-multi] connect ready timeout connected=0 accepted=0 ready=0 expected=2`
- server: `[multi-pubsub-server] connect-ready timeout peers=0 expected=2`
- client: `[perf-multi] connect ready timeout connected=0 accepted=0 ready=0 expected=1`
- client: `[perf-multi-one-way] active metrics invalid recv=0 lat_count=0 run_id=1 msg_size=64`

### 3. Direct `DEALER_DEALER` server/client

Server:

```bash
env PERF_DEBUG=1 PERF_RECV_MODE=callback PERF_MSG_SIZES=64 \
  PERF_WARMUP_SECONDS=0 PERF_DURATION_SECONDS=1 PERF_SETTLE_MS=500 \
  PERF_CLIENTS=2 \
  /home/hep7/project/kairos/zlink/core/build/bin/comp_src_dealer_dealer_server \
  current tcp
```

Client:

```bash
env PERF_DEBUG=1 PERF_RECV_MODE=callback PERF_MSG_SIZES=64 \
  PERF_WARMUP_SECONDS=0 PERF_DURATION_SECONDS=1 PERF_SETTLE_MS=500 \
  PERF_CLIENTS=2 \
  /home/hep7/project/kairos/zlink/core/build/bin/comp_src_dealer_dealer_client \
  current tcp 64 --endpoint tcp://127.0.0.1:<port>
```

Observed logs:

- server: `[perf-multi] connect ready timeout connected=0 accepted=0 ready=0 expected=2`
- client: `[perf-multi] connect ready timeout connected=0 accepted=0 ready=0 expected=1`
- client: `[multi-dealer-dealer-client] connect-ready wait failed`

## Additional observed evidence

위 재현에서 아래 option 적용도 반복적으로 `EINVAL`을 반환했다.

- `ZLINK_LINGER`
- `ZLINK_SNDHWM`
- `ZLINK_RCVHWM`
- `ZLINK_SNDTIMEO`
- `ZLINK_RCVTIMEO`
- `ZLINK_XPUB_NODROP`
- `ZLINK_XPUB_VERBOSER`
- `ZLINK_BLOCKY` on context

반면 같은 환경에서 multi `STREAM --recv recv` 와 `STREAM --recv callback` smoke는
통과했다. 즉 perf 실행 환경 전체가 깨진 것이 아니라, 특정 socket/monitor contract
경로가 비어 있거나 잘못 연결된 가능성이 높다.

## Expected result

- `PUBSUB`와 `DEALER_DEALER`가 `tcp`에서 정상적으로 connect-ready 또는 동등한
  readiness 신호를 관측해야 한다.
- benchmark socket option 적용이 지원 대상에서는 성공해야 한다.
- 지원하지 않는 option이면 benchmark code가 아닌 core contract 차원에서 명확히
  구분되거나, 적어도 readiness/traffic 경로를 깨뜨리지 않아야 한다.
- perf 쪽은 fallback이나 barrier skip으로 숨기지 않고 정상 신호 위에서 동작해야 한다.

## Non-goals

아래는 해결로 인정하지 않는다.

- perf runner에서 connect-ready timeout을 무시하고 진행
- pattern별 예외 처리로 barrier를 skip해서 결과를 억지로 생산
- 문서 support matrix만 축소해서 문제를 숨김

## Suspected fix areas

- `zlink_socket_monitor_open()` + `zlink_socket_monitor_handler()` event delivery for
  `SUB` / `XPUB` / `DEALER`
- `zlink_set_option()` support matrix for common benchmark socket options on those
  socket types
- monitor snapshot / ready peer accounting consistency across socket types

## Current repo decision

- 이 문제는 perf workaround로 닫지 않는다.
- core monitor / socket option / readiness contract bug로 추적한다.
