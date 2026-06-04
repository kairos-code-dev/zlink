# Deterministic Lifecycle Root Cause Review

## 요약
현재 `spot/gateway/discovery/registry` 계열에서 반복적으로 나타나는
timeout, hang, split/full-suite-only failure는 개별 테스트나 개별 transport의
우연한 문제가 아니다. service lifecycle contract 부재와 socket core의
termination thread model 위반이 겹친 구조적 문제다.

핵심은 다음과 같다.

- `destroy()` 성공의 의미가 서비스마다 다르다.
- `who stops / who closes / who waits / who decides success`가 서비스마다 다르다.
- `ctx_term()`이 사실상 마지막 cleanup owner처럼 동작한다.
- direct callback 도입 이후 socket core가 더 이상 "한 시점에 한 스레드만
  소켓 내부 상태를 만진다"는 가정을 지키지 못한다.
- 그래서 한 곳을 보강하면 다른 서비스나 다른 transport에서 같은 종류의
  종료 문제가 다시 드러난다.

## 현재 증상
대표적으로 다음 패턴이 반복된다.

1. 단독 실행은 통과하지만 split/full 순차 실행에서 timeout이 난다.
2. `shutdown=abortive` 로그가 찍혀도 테스트 프로세스가 끝까지 수렴하지 않을
   때가 있다.
3. `spot` 계열에서는 `tls_lock`, `topology_summary`, `peer_ws`,
   `peer_tls` 등에서 teardown이 비결정적으로 길어진다.
4. `registry/discovery` 계열에서는 startup/rollback 이후 secondary 증상
   (`NULL`, `EPROTONOSUPPORT`, `EADDRINUSE`)으로 나타나기도 한다.

최종 증상은 다양하지만 공통점은 하나다. `destroy -> ctx_term` 경계에서 내부
자원 수렴이 deterministic하지 않다.

## 현재까지 분리된 문제
bug 폴더의 다른 문서를 기준으로 보면 이 이슈는 이미 두 층으로 분리돼 있다.

### BUG-01: service lifecycle tracking loss
- `service_runtime_base_t::wait_drained()`가 timeout 시 `_closing_sockets`
  추적을 잃어버리던 문제
- service 계층 helper 결함이었고, 별도 수정으로 해결된 상태다.

### BUG-02: async mailbox vs reaper data race
- direct callback의 async mailbox가 I/O thread에서 socket command를 처리하는 동안
  reaper thread가 같은 socket의 termination을 진행
- `_pipes`, `_term_acks`, pipe termination graph에 동시 접근
- service helper가 아니라 socket core thread model 위반 문제다.

따라서 현재 남은 본질은 "service helper가 약하다"만으로는 설명이 부족하다.
"service-level 종료 계약 부재"와 "core-level socket termination race"를
같이 봐야 한다.

## 구조적 원인
### 1. 종료 책임이 여러 계층에 분산되어 있다
현재 lifecycle 책임은 다음 계층에 나뉘어 있다.

- public handle
- service runtime/helper
- worker thread
- `ctx/reaper`

이 구조에서는 같은 서비스 안에서도 다음 질문의 답이 흩어진다.

- 누가 새 work를 막는가
- 누가 socket을 닫는가
- 누가 drain을 기다리는가
- 누가 성공/실패를 판정하는가

이 책임이 한 곳에 고정되지 않으니 종료 순서가 조금만 흔들려도 다음과 같은
증상이 반복된다.

- child handle destroy는 끝났다고 보이는데 runtime socket은 남아 있음
- runtime은 tracked socket이 0이라고 보는데 `ctx`는 아직 socket을 들고 있음
- graceful 실패를 서비스가 숨기고 `ctx_term()`에서 마지막 timeout으로만 드러남

### 2. `service_runtime_base_t`는 lifecycle kernel이 아니라 socket helper 수준이다
현재 `service_runtime_base_t`는 다음 정도만 담당한다.

- owned socket registry
- closing socket registry
- `close_socket()`
- `wait_drained()`
- `force_wait_remaining()`

하지만 deterministic lifecycle에 필요한 핵심 요소는 아직 각 서비스가 따로
관리한다.

- worker thread join
- periodic/control task 제거
- observer detach 완료
- startup/rollback state
- graceful 실패 시 반환 계약

이름은 runtime base이지만 실제로는 공통 lifecycle state machine이 아니다.
다만 bug 문서를 기준으로 보면 이것이 "현재 남아 있는 모든 문제의 단일 root cause"는
아니다. BUG-01의 직접 원인은 맞았지만 BUG-02까지 설명하지는 못한다.

### 3. `ctx_term()`이 무기한 최종 cleanup owner로 남아 있다
현재 `ctx_t::terminate()`는 서비스가 내부 자원을 모두 수렴시켰다고 가정하고
reaper `done`을 기다린다.

이 모델의 문제는 명확하다.

- 서비스 graceful shutdown이 완전히 수렴하지 않으면
- 최종 증상은 전부 `ctx_term()` hang/timeout으로 수렴한다.

즉 `ctx`가 cleanup 진단의 마지막 쓰레기통처럼 동작한다.

이 구조에서는 서비스가 약간만 종료를 잘못해도 마지막에는 다 같은 모양으로
보인다.

그리고 BUG-02 같은 core race가 있으면 service-level graceful shutdown을
아무리 보강해도 마지막 증상은 다시 `ctx_term()` hang/timeout으로 드러날 수 있다.

### 4. `spot`은 worker-owned socket과 node-owned shutdown 판단이 섞여 있다
`spot`의 경우 특히 이 문제가 강하다.

- data-plane thread가 internal socket을 생성하고 종료에 관여한다.
- node destroy는 runtime field와 lifecycle registry를 보고 종료를 판단한다.
- child handle도 attachment/detach 경로에 간접적으로 관여한다.

socket lifetime과 shutdown decision이 한 층에 고정돼 있지 않은 것이다.

이 구조에서는 다음 race가 자연스럽게 생긴다.

- thread는 아직 socket을 쓰고 있는데 main-thread destroy가 종료를 판정
- tracked socket은 비워졌다고 보지만 worker 쪽 lifetime은 아직 남아 있음
- abortive fallback을 써도 `ctx` 단계까지 잔여 lifetime이 밀린다

다만 이것 역시 "spot만의 독립 root cause"로 단정하면 안 된다.
`spot`은 direct callback과 async mailbox를 가장 강하게 쓰는 경로라서
core race를 가장 자주 드러내는 서비스에 가깝다.

### 5. socket core의 termination thread model이 direct callback 이후 깨졌다
원래 socket model은 대략 이런 가정 위에 있었다.

- close 전까지는 application thread 중심
- close 후에는 reaper thread 중심
- 같은 socket 내부 상태는 사실상 한 시점에 한 스레드만 접근

하지만 direct callback의 async mailbox가 들어오면서 다음이 가능해졌다.

- I/O thread: `process_async_mailbox()` / `process_commands()`
- reaper thread: `start_reaping()` / `process_term()`

이 둘이 같은 socket의 `_pipes`, pipe termination, `_term_acks`에
동시에 관여할 수 있다.

bug 문서의 분석대로, 현재 남은 deeper issue는 여기다.

지금 패턴을 정리하면 이렇다.
- 표면에서는 `spot tls_lock timeout`, `ctx_term hang`처럼 보이고
- 한 단계 아래에서는 `wait_drained`/abortive 실패처럼 보이며
- 더 아래 core에서는 `async mailbox vs reaper` race가 termination graph를
  흔든다.

### 6. 서비스별 종료 contract가 일관되지 않다
현재 서비스별 종료 경로가 통일돼 있지 않다.

- `spot`
  - abortive fallback과 tracked socket drain을 일부 가짐
- `gateway`
  - runtime lifecycle을 쓰지만 destroy 성공 조건이 강하게 계약화돼 있지 않음
- `discovery`
  - observer/task/socket 정리는 하지만 final contract가 약함
- `registry`
  - startup rollback과 destroy final drain이 완전히 동일 모델로 묶여 있지 않음

공통 helper를 일부 쓴다고 해서 공통 lifecycle contract가 생긴 것은 아니다.
그리고 이 불일치는 core race를 더 잘 드러내는 증폭기로 작동한다.

## 왜 하나 고치면 다른 게 다시 터지는가
지금 발생하는 현상은 전형적인 구조 문제 패턴이다.

- 개별 케이스를 고치면 그 케이스를 드러내던 race는 줄어든다.
- 하지만 종료 계약 자체가 공통화되어 있지 않아서
- 다른 서비스, 다른 transport, 다른 테스트 sequence가 다음 race를 드러낸다.

그래서 현재 보이는 `tls_lock`, `topology_summary`, `peer_ws`, `peer_tls`는
서로 별개의 버그가 아니다. 같은 클래스의 teardown/termination defect가
드러나는 창구가 바뀌는 것으로 봐야 한다.

정확히는 두 층이 같이 있다.
- service-level deterministic shutdown 부재
- core-level async mailbox/reaper race

## 근본 해결 방향
### 1. 공통 lifecycle contract를 먼저 고정해야 한다
모든 서비스는 아래 순서를 동일하게 따라야 한다.

1. 새 work 유입 차단
2. observer detach
3. periodic/control task 제거
4. worker stop signal
5. worker join 완료
6. owned socket final close
7. drain 확인
8. 성공 시에만 `destroy()` 반환

이 순서와 성공 조건이 공통화되지 않으면 서비스별 땜질은 계속 반복된다.
다만 이 조치만으로 BUG-02가 사라진다고 가정하면 안 된다.

### 2. ownership을 한 층으로 단일화해야 한다
worker가 만든 socket은 다음 둘 중 하나여야 한다.

- worker만 종료 책임을 가진다
- runtime finalizer만 종료 책임을 가진다

혼합 ownership은 금지해야 한다.

지금처럼

- worker도 닫고
- node도 닫고
- runtime helper도 닫고
- `ctx`도 기다리는

구조는 다시 같은 문제를 만든다.

동시에 socket core 쪽에서도 "close 전에 async mailbox quiesce 완료" 같은
단일-thread termination invariant가 복구되어야 한다.

### 3. `destroy()` 성공의 의미를 강하게 만들어야 한다
현재 가장 큰 설계 약점은 `destroy()` 성공과 실제 drain 완료가 완전히
동일하지 않다는 점이다.

근본 해결을 위해서는 다음 계약이 필요하다.

- `destroy()` 성공 = 내부 자원 drain 완료
- graceful 실패 = 실패 반환
- abortive는 기본 성공 경로가 아니라 진단/보조 경로
- `ctx_term()`은 drained context 종료만 담당

단, graceful 실패를 제대로 드러내도록 바꿔도 core termination race가 남아 있으면
실패 빈도는 줄어도 완전히 끝나지는 않을 수 있다.

### 4. `ctx`는 cleanup owner가 아니라 final shutdown 단계여야 한다
`ctx`가 서비스 lifecycle 실수를 떠안는 구조는 장기적으로 유지하면 안 된다.

`ctx`가 해야 할 일:

- 이미 drain된 socket set 종료
- 남은 socket dump 제공
- debug/test에서 invariant 위반 드러내기

`ctx`가 하면 안 되는 일:

- 서비스 graceful 실패를 무기한 숨겨주기
- service-level contract 부재를 정상 동작처럼 덮기

추가로 `ctx` fallback은 이 단계의 1순위 해법이 아니다.
core race가 남아 있는 상태에서 `ctx` fallback만 강화하면 원인을 더 깊게 숨길 수 있다.

## 권장 재설계
### 1. service 계층: `service_runtime_base_t`를 실제 lifecycle kernel로 승격
다음 상태를 공통 kernel이 직접 소유해야 한다.

- lifecycle state
  - `starting`, `running`, `stopping`, `stopped`, `faulted`
- owned socket registry
- owned task registry
- owned worker registry
- observer detach state

다음 동작도 공통화해야 한다.

- `begin_start()`
- `commit_start()`
- `rollback_start()`
- `begin_stop()`
- `join_workers()`
- `close_owned_sockets()`
- `wait_drained()`
- `assert_drained()`

지금의 socket helper를 서비스 공통 state machine으로 올려야 한다.

### 2. core 계층: async mailbox와 reaper의 종료 모델을 다시 고정
bug 문서 관점에서 보면 장기적으로는 다음이 필요하다.

- `close()` 전에 async mailbox가 완전히 quiesce되도록 보장
- `start_reaping()` 시점에 이전 I/O handler가 socket 내부 상태를 더 이상
  만지지 않도록 보장
- pipe termination/accounting과 owned-object termination/accounting을
  동일 카운터/동일 완료 모델에 섞지 않도록 분리

service 재설계만이 아니라 socket core termination invariant 복구가 필요하다.

### 3. 서비스별 이관 방향
#### spot
- data-plane/internal socket/attachment socket ownership을 runtime 하나로 고정
- node destroy는 worker join 이전/이후 순서를 공통 contract에 맞춤
- child handle은 detach-only 유지
- `tls/ws/tcp/ipc` teardown을 같은 state machine에 수렴

#### discovery / registry
- transient dealer 포함 모든 socket을 runtime ownership에 등록
- `start/ensure/rollback/destroy`가 동일 kernel 경로를 타도록 정리
- partial startup 실패 후 잔여 state가 남지 않게 보장

#### gateway
- router/monitor/refresh/state machine을 runtime state로 일원화
- destroy 성공은 실제 drain 성공과 동일 의미가 되게 수정

### 4. `ctx` 역할 축소
- `ctx_term()`은 cleanup 주체가 아니라 마지막 shutdown 단계
- 서비스가 drain을 못 끝낸 상태면 debug/test에서 즉시 실패로 보이게 함
- context-level fallback은 최후의 안전장치로만 검토

## 테스트 관점 결론
현재 포트 seed, split sequence, transport 조합이 문제를 더 잘 드러내는 것은
맞다. 하지만 그건 본질이 아니라 증폭기다.

테스트를 아무리 조정해도 다음 조건이 먼저 맞아야 한다.

- `destroy()` 성공 후 즉시 `ctx_term()` 가능
- split/full sequence 모두 동일 결과
- retry 없이 green

테스트 안정화는 lifecycle contract와 core termination invariant가 고정된 뒤
따라오는 결과여야 한다.

## 결론
현재 반복되는 문제는 개별 transport bug나 단일 테스트의 flake가 아니라,
다음 두 축이 겹친 구조적 문제다.

1. service lifecycle contract가 코드 전체에서 공통적으로 강제되지 않음
2. direct callback 이후 socket core의 single-thread termination model이 깨짐

따라서 다음 단계는 개별 timeout 대응이 아니라 다음 순서여야 한다.

1. BUG-02 관점의 core termination invariant 복구
2. 공통 lifecycle kernel 계약 확정
3. ownership 단일화
4. 서비스별 이관
5. 마지막에 `ctx` 역할 축소 및 진단 강화

이 방향으로 가야 “하나 고치면 다른 게 다시 터지는” 패턴을 끝낼 수 있다.
