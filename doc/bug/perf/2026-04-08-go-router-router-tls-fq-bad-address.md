# Go single `ROUTER_ROUTER/tls` triggers core `fq.cpp` `Bad address`

## Summary

`bindings/go/perf` 단일 smoke를 정책대로 복구하는 중,
`ROUTER_ROUTER --transport tls` 가 binding 예외가 아니라 core abort로
종료되는 것이 재현됐다.

현재 증거상 abort 지점은:

- `core/src/sockets/fq.cpp:68`
- 메시지: `Bad address`

Go binding 쪽에서 recv/send ownership 정리를 일부 진행해도,
`ROUTER_ROUTER/tls` 는 여전히 core `fq_t::recvpipe()` 경로까지 올라가
프로세스를 abort 시킨다.

즉 이 케이스는 perf wrapper 우회로 닫지 말고 core bug로 추적해야 한다.

## Reproduction

작업 디렉터리:

```bash
cd /home/hep7/project/kairos/zlink/bindings/go
```

직접 재현:

```bash
go run ./perf/single --pattern ROUTER_ROUTER --transport tls --msg-size 64 --duration 5
```

Observed result:

```text
Bad address (/home/hep7/project/kairos/zlink/core/src/sockets/fq.cpp:68)
SIGABRT: abort
...
zlink.(*directSocket).Recv(...)
main.runRouterRouter(...)
main.startRouterRouterEchoServer.func1(...)
```

추가로 같은 작업 중 확인된 점:

- `PAIR/tcp`
- `PAIR/tls`
- `ROUTER_ROUTER/tcp`

는 binding-level recv/close 정리 후 통과했다.

즉 현재 남은 재현은 `ROUTER_ROUTER/tls` 특이 케이스다.

## Why this is a core bug

- abort 지점이 binding 코드가 아니라 `core/src/sockets/fq.cpp` 다.
- Go perf가 잘못된 RESULT line을 찍는 수준이 아니라 core가 `SIGABRT` 로 종료된다.
- perf policy 기준 smoke는 core contract 위에서 돌아야 하며,
  binding perf가 socket/fair-queue abort를 우회해서 숨기면 안 된다.

## Current binding-side context

재현 시점의 Go perf 변화:

- `PAIR` 는 `TryRecv()` 기반 경로를 `Recv()`로 단순화해 `EFAULT(14)`를 제거했다.
- `ROUTER_ROUTER/tcp` 는 recv close 누락을 정리하고 HWM/socket option/ready probe를
  추가해 복구됐다.
- 그 뒤에도 `ROUTER_ROUTER/tls` 는 server/client 양쪽 `Recv()` 경로에서 core abort가
  남는다.

즉 현재 상태는 “binding obvious ownership bug를 먼저 제거해도 core abort가 남는
최소 재현”에 가깝다.

## Expected result

- `ROUTER_ROUTER/tls` recv path가 core abort 없이 동작해야 한다.
- 문제가 있더라도 `SIGABRT` 가 아니라 recoverable error contract로 surface 되어야 한다.
- perf는 workaround 없이 동일 runner 계약으로 smoke `complete` 를 낼 수 있어야 한다.

## Non-goals

아래는 해결로 인정하지 않는다.

- `ROUTER_ROUTER/tls` 를 perf support matrix에서 제외
- Go perf에서 TLS only skip/UNSUPPORTED 추가
- recv path를 억지로 다른 pattern과 다르게 우회해 core abort를 숨김

## Suspected fix areas

- `core/src/sockets/fq.cpp` 의 pipe activation/deactivation 또는 multipart recv state
- TLS transport에서 routed socket read side가 `fq_t` 에 넘기는 pipe lifecycle
- routed socket close/teardown 중 `fq_t` 가 stale pipe를 읽는지 여부

## Current repo decision

- 이 문제는 binding perf workaround로 닫지 않는다.
- core `fq` / routed TLS recv lifecycle bug로 추적한다.

## Processing result

- 2026-04-09 처리 완료.
- 현재 기준 direct 재현 `go run ./perf/single --pattern ROUTER_ROUTER --transport tls --msg-size 64 --duration 2` 는 abort 없이 정상 RESULT line을 출력한다.
- 수정은 `core/src/sockets/fq.cpp` / `core/src/sockets/fq.hpp` 에 들어갔고, stale pipe index와 깨진 `_active/_current/_more` 상태를 정규화하도록 `normalize_state()` / `try_get_pipe_index()` 를 추가했다.
- teardown 중 routed internal multipart가 끊기는 경우는 fatal `EFAULT`/abort 대신 transient path로 흘리도록 `recvpipe()` 를 완화했다.
- Go 회귀 테스트는 `bindings/go/router_router_tls_regression_test.go` 로 추가했다.
