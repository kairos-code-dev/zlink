# socket matrix failures classification: `core` vs `perf`

## Summary

2026-03-20에 추가된 socket/perf matrix bug report들 중 일부는 처음에는 `core`
라이브러리 버그 가능성을 열어 두고 추적했다. 하지만 `core/tests` 기준 회귀테스트로
동일 contract를 직접 검증해 본 결과, 적어도 `DEALER/ROUTER`, `ROUTER/ROUTER`
계열의 기본 socket/monitor contract는 `core` 단독으로는 재현되지 않았다.

즉 아래 문서들이 모두 "가짜 버그"라는 뜻은 아니지만, 그 안의 일부 실패는
`core` 버그가 아니라 `perf` 구현/사용 버그로 재분류하는 것이 맞다.

관련 문서:

- [`2026-03-20-single-current-socket-matrix-gaps.md`](/home/hep7/project/kairos/zlink/doc/bug/2026-03-20-single-current-socket-matrix-gaps.md)
- [`2026-03-20-multi-recv-matrix-gaps.md`](/home/hep7/project/kairos/zlink/doc/bug/2026-03-20-multi-recv-matrix-gaps.md)
- [`2026-03-20-multi-pubsub-warmup-regression.md`](/home/hep7/project/kairos/zlink/doc/bug/2026-03-20-multi-pubsub-warmup-regression.md)

## Evidence

`core/tests`에 `ROUTER/ROUTER` 회귀를 추가해서 아래 contract를 직접 고정했다.

- server monitor `recv` x socket `recv`
- server monitor `recv` x socket `callback`
- server monitor `callback` x socket `recv`
- server monitor `callback` x socket `callback`

검증 포인트:

- `ZLINK_EVENT_CONNECTION_READY` 수신 직후 첫 routed request를 시작한다.
- `ROUTER/ROUTER`에서는 connect side가 `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID`
  를 사용한 뒤 즉시 routed send/reply를 수행한다.
- `DEALER/ROUTER`도 같은 방식으로 `CONNECTION_READY` 직후 첫 request/reply를 수행한다.

회귀테스트 파일:

- [`core/tests/integration/monitoring/test_monitor_socket_contract.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/monitoring/test_monitor_socket_contract.cpp)

실행 결과:

```bash
./core/build/bin/test_monitor_socket_contract
```

- `20 Tests 0 Failures`
- 추가된 `ROUTER/ROUTER` 4조합 모두 통과
- 기존 `DEALER/ROUTER` 4조합도 계속 통과

## Classification

### 1. `DEALER_ROUTER`

현재 분류: `perf` bug 가능성이 높음

이유:

- `core/tests` 기준으로는 `CONNECTION_READY` 직후 첫 routed request/reply가 정상 동작한다.
- single/multi perf에서만 timeout 또는 non-zero exit로 남아 있다.
- 따라서 현재 관찰된 gap은 `core`보다 `perf` runner/pattern implementation 차이일
  가능성이 높다.

### 2. `ROUTER_ROUTER`

현재 분류: `perf` bug 가능성이 높음

이유:

- `core/tests`에 새로 추가한 `ROUTER/ROUTER` 회귀에서 monitor/socket recv-callback
  교차 조합이 모두 통과했다.
- `core`의 기본 routed send/recv contract는 재현되지 않았다.
- 따라서 single `no_data`, multi `non-zero exit`는 현재로선 `perf` 쪽 분류가 맞다.

### 3. `PUBSUB warmup`

현재 분류: 미확정

이유:

- 이 문제는 perf phase protocol (`CLIENT_READY`, `START,<size>`, warmup/drain/active`)
  을 포함한다.
- 같은 현상을 `core/tests` 단독 contract로 아직 재현하지 못했다.
- 따라서 지금 단계에서 `core` 버그 아님으로 단정하면 안 된다.

## Repo decision

- `DEALER_ROUTER`, `ROUTER_ROUTER` matrix failure는 우선 `perf` 쪽 bug로 본다.
- `PUBSUB warmup`은 아직 미분류 상태로 유지한다.
- `core` 버그라고 주장하려면 반드시 `core/tests` 회귀로 단독 재현되어야 한다.
