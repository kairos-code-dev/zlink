# 2026-03-17 single callback inproc dispatch gap

## Summary

- `single perf`를 callback 기반으로 전환한 뒤 `inproc` one-way 케이스가
  광범위하게 실패한다.
- 현재 확인된 주증상은 `PAIR`, `DEALER_DEALER`, `DEALER_ROUTER`,
  `ROUTER_ROUTER`, `PUBSUB`의 `inproc`가 `FAIL`로 끝나고,
  queue metric이 `snd_pending_max=2000`, `rcv_pending_max=2000`,
  `rcv_pending_end=2000`으로 고정되는 점이다.
- 이 현상은 transport별 정상 차이가 아니라, `generic MSG callback` 및
  `SPOT callback`의 local `inproc` delivery path가 callback-only single perf
  모델을 완전히 소화하지 못하는 버그 후보로 봐야 한다.
- `/home/hep7/project/kairos/zlink-direct-callback-rewrite`는 같은 비교 기준으로
  쓰기 어렵다. 해당 시점의 `single perf`는 callback이 아니라 `recv` 기반이었고,
  이번 버그는 callback 전환 후 새로 드러난 경로다.

## Scope

### Confirmed

- `single perf` callback 전환 이후 `inproc`에서 receiver callback delivery가
  진행되지 않는다.
- 실패 리포트:
  - `/home/hep7/project/kairos/zlink/core/perf/results/single/report/perf_linux_20260317_113238.txt`
- 대표 패턴:
  - `PAIR/inproc`
  - `DEALER_DEALER/inproc`
  - `DEALER_ROUTER/inproc`
  - `ROUTER_ROUTER/inproc`
  - `PUBSUB/inproc`

### Not Confirmed

- `tcp`, `ipc`, `tls`, `ws`, `wss` 전체가 같은 버그인지
- `PUBSUB/inproc`가 `generic MSG callback`과 같은 root cause인지

## Why This Is Not "inproc Is Special"

- network transport는
  [`session_base_t::push_msg()`](/home/hep7/project/kairos/zlink/core/src/core/session_base.cpp#L136)
  에서 engine/session이 socket callback dispatch를 직접 밀어 넣는다.
- 반면 `inproc`는 engine/session을 타지 않고
  [`pipe_t::process_activate_read()`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp#L393)
  -> [`socket_base_t::read_activated()`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.cpp#L2495)
  -> 각 소켓의 `xread_activated()`에 의존한다.
- 따라서 같은 callback contract라도 현재 구현은
  - network/session path
  - local inproc pipe activation path
  로 나뉘어 있다.
- 이 차이는 사용자 contract 차이가 아니라 구현 결손이다.

## Observed Symptoms

### 1. Generic MSG callback patterns

- `PAIR/inproc`, `DEALER_DEALER/inproc`, `DEALER_ROUTER/inproc`,
  `ROUTER_ROUTER/inproc`가 모두 `FAIL`이다.
- 공통 증상:
  - throughput/latency 결과가 비지 않음
  - send/recv queue pending이 끝까지 `2000`에 머묾
- 이건 sender가 HWM까지 밀어 넣고 receiver callback이 소비를 못 했다는 뜻이다.

### 2. PUBSUB callback pattern

- `PUBSUB`는 별도 `SPOT` callback 인터페이스를 사용한다.
- callback 전환 초기에 `SUB`를 `MSG` handler처럼 붙인 perf 쪽 오류가 있었고,
  이것은 benchmark-side bug였다.
- 이후 `SPOT` handler와 `topic + payload` framing으로 수정한 뒤
  `tcp`는 복구됐다.
- 하지만 `PUBSUB/inproc`는 여전히 queue가 `2000`까지 차며 실패한다.
- 따라서 `PUBSUB/inproc`는 benchmark framing bug와 별개로,
  local callback delivery 쪽 추가 버그 가능성이 남아 있다.

## Comparison With zlink-direct-callback-rewrite

- `/home/hep7/project/kairos/zlink-direct-callback-rewrite`의 `single perf`는
  당시 `recv` 기반이었다.
- 예를 들어
  [`perf_pubsub.cpp`](../../../../zlink-direct-callback-rewrite/core/perf/single/src/perf_pubsub.cpp)
  는 `receiver_thread + zlink_msg_recv()` 모델이다.
- 따라서 그 브랜치에서 `single perf`가 통과했다는 사실은
  - `recv` 경로가 동작했다
  를 의미할 뿐,
  - callback + inproc 경로가 이미 검증돼 있었다
  는 뜻은 아니다.
- 이번 이슈는 `single perf`를 callback-only로 바꾸며 처음 강하게 드러난
  경로 회귀로 분류해야 한다.

## Current Root Cause Hypothesis

### Hypothesis A: generic MSG callback local dispatch gap

- `PAIR`, `DEALER`, `ROUTER`의 callback path는 주로
  session 기반 `socket_msg_dispatch_from_io()` 중심으로 정리돼 있다.
- `inproc`는 session push 대신 local pipe `read_activated()`에 의존하므로,
  `xread_activated()`에서 callback delivery를 끝까지 책임져야 한다.
- 현재 구현은 이 경로가 incomplete일 가능성이 높다.

근거:

- network transport는 정상인데 `inproc`만 실패한다.
- queue가 sender/receiver 양쪽에서 함께 포화된다.
- 이는 callback registration 자체 실패보다, local read activation 이후
  message consumption이 진행되지 않는 쪽에 가깝다.

### Hypothesis B: XSUB/SPOT local dispatch gap

- `XSUB`는 이미 전용 async dispatch loop를 갖고 있지만,
  `single perf` callback 전환 후 `PUBSUB/inproc`도 동일하게 실패한다.
- 따라서
  - `PUBSUB/inproc`가 같은 local activation 문제를 공유하거나
  - `SPOT` callback loop와 single perf one-way callback model 사이에
    별도 gap이 있을 수 있다.

현재로서는 `PUBSUB/inproc`를 generic MSG callback 버그와 동일 root cause로
단정하면 안 된다.

## Reproduction

### Failing report

- `/home/hep7/project/kairos/zlink/core/perf/results/single/report/perf_linux_20260317_113238.txt`

### Representative commands

```bash
./core/build/bin/perf_pair current inproc 64
./core/build/bin/perf_dealer_dealer current inproc 64
./core/build/bin/perf_dealer_router current inproc 64
./core/build/bin/perf_router_router current inproc 64
./core/build/bin/perf_pubsub current inproc 64
```

### Expected

- callback receiver가 정상 delivery를 소비하고
  throughput/latency 결과를 출력해야 한다.
- queue pending은 phase 종료 시 0 또는 낮은 값으로 내려와야 한다.

### Actual

- 결과는 `FAIL`
- queue pending은 `2000/2000/2000`

## Non-Fixes

- perf harness에서 sleep, retry, pump thread, poll helper 같은 우회로 숨기지 않는다.
- `inproc`만 별도 benchmark contract를 두지 않는다.
- transport별 동작 차이를 정상으로 문서화하지 않는다.

## Required Fix Direction

- `core`에서 callback delivery contract를 transport-independent 하게 맞춘다.
- 즉 같은 소켓 타입이면
  - network/session path
  - local inproc pipe activation path
  모두 동일하게 callback delivery가 진행돼야 한다.
- 우선순위:
  1. `PAIR/DEALER/ROUTER` generic MSG callback의 local inproc dispatch 고정
  2. `PUBSUB/inproc`를 별도 최소 재현으로 분리해 `XSUB/SPOT` local dispatch도 고정

## Open Questions

- `PUBSUB/inproc`는 `XSUB` core bug인가, 아니면 single perf callback phase model의
  별도 mismatch인가
- generic MSG callback의 local dispatch gap을 고치면 `PAIR/DEALER/ROUTER`가
  한 번에 같이 복구되는가
- 회귀 테스트는 perf binary가 아니라 `core/tests`에 어떤 최소 contract로
  고정하는 것이 가장 적절한가

## Status

- 열림
- perf 우회는 보류
- 다음 작업은 `core` 최소 재현 테스트 추가 후 local callback dispatch root cause
  확정이다
