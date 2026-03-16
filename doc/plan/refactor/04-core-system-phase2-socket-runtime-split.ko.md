# `[04]` `core` 시스템 리팩토링 Phase 2 Socket Runtime Split

> 상태: draft
> 목적: `socket_base_t`에서 semantic 과 runtime 기계 작업 분리

| nav | link |
| --- | --- |
| 목록 | [README](README.ko.md) |
| 이전 | [03 Phase 1 Resource Inventory](03-core-system-phase1-resource-inventory.ko.md) |
| 다음 | [05 Phase 3 Engine/Transport/Service](05-core-system-phase3-engine-transport-service-plan.ko.md) |
| 관련 | [02 Phase 1 Ownership Map](02-core-system-phase1-ownership-map.ko.md), [01 Baseline](01-core-system-phase0-baseline.ko.md) |
| thread-safe 규약 | [thread-safe-socket-plan](../plan/thread-safe/thread-safe-socket-plan.ko.md) — hot path + control path 계층. 현재 구현 수준을 유지한다. |

## 1. 목적

Phase 2의 목적은 `socket_base_t`가 들고 있는
과도한 공통 책임을 runtime 경계로 내리는 것이다.

용어 주의:

- Phase 1에서 말하는 "socket runtime"은 현재 코드에서
  socket 내부가 이미 수행하는 close/destroy/quiesce 메커니즘을 가리킨다.
- Phase 2에서 도입하는 "socket runtime"은 그 범위를 확장하여
  endpoint registry, peer state, monitor bridge, dispatch bridge,
  lifecycle hooks까지 포함하는 새로운 구조 계층이다.

즉 Phase 1은 기존 socket 내부 메커니즘의 owner를 명확히 하고,
Phase 2는 그 위에 공통 메커니즘을 통합한 계층을 만드는 것이다.

이번 단계는 "소켓 구현을 잘게 나누는 작업"이 아니다.
이번 단계는 다음 질문에 답하는 구조 재정의다.

```text
"socket family가 알아야 할 것"과
"모든 socket이 공통으로 수행하는 기계 작업"을 어디서 나눌 것인가
```

## 2. 현재 문제

현재 `socket_base_t`는 대략 아래를 같이 가진다.

- API 진입점
- endpoint lifecycle
- pipe/mailbox 연동
- monitor emission
- peer bookkeeping
- send-ready / callback dispatch glue
- reaper / destroy 연동

이 구조는 재사용성은 높지만,
POSD 기준으로는 semantic(각 socket family가 가진 고유한 메시지 의미와 라우팅 정책)과
mechanism(endpoint 등록, peer 추적, monitor 이벤트 등 모든 socket이 공통으로 수행하는 기계 작업)이
한 타입에 과도하게 몰려 있다.

AS-IS/TO-BE 비교:

```text
AS-IS: socket_base_t가 모든 것을 가짐

  socket_base_t
  ┌──────────────────────────────────────────────┐
  │  semantic (family별 차이 — 메시지 의미, 라우팅)  │
  │  ├── xsend / xrecv / xhas_in / xhas_out      │
  │  ├── routing / subscription / load-balancing  │
  │  └── pipe event semantics                     │
  │                                                │
  │  mechanism (모든 socket 공통 — 기계적 기반 작업) │
  │  ├── endpoint attach/detach bookkeeping       │
  │  ├── peer connect/disconnect tracking         │
  │  ├── monitor frame encoding/emit              │
  │  ├── dispatch subject / callback bridge       │
  │  ├── pipe/mailbox glue                        │
  │  └── destroy quiesce / reaper 연동             │
  └──────────────────────────────────────────────┘
  문제: family 구현이 mechanism 세부를 알아야 하고,
        mechanism 변경이 모든 family에 영향


TO-BE: semantic과 mechanism 분리

  socket facade / family semantic
  ┌──────────────────────────────────────────────┐
  │  xsend / xrecv / xhas_in / xhas_out          │
  │  routing / subscription / load-balancing      │
  │  pipe event semantics                         │
  │  (mechanism 세부를 모름)                       │
  └──────────────────────┬───────────────────────┘
                         │ 좁은 인터페이스
                         v
  socket runtime
  ┌──────────────────────────────────────────────┐
  │  endpoint registry                            │
  │  peer state                                   │
  │  monitor bridge                               │
  │  dispatch bridge                              │
  │  lifecycle hooks / quiesce helper             │
  │  (family 의미를 모름)                          │
  └──────────────────────┬───────────────────────┘
                         │
                         v
  core primitives (pipe, mailbox, own_t, reaper)
```

## 3. 목표 구조

Phase 2 후 목표 구조는 아래에 가깝다.

```text
socket facade / family semantic
        |
        v
socket runtime
   |- endpoint registry
   |- peer state
   |- monitor bridge
   |- dispatch bridge
   |- lifecycle hooks
        |
        v
core socket / pipe / mailbox primitives
```

### 3.1 하위 계약별 information hiding

각 하위 계약이 무엇을 숨기고, 누가 호출하는지 1줄로 정리한다.
이 표는 하위 모듈을 지금 독립 deep module로 쪼개는 것이 아니라,
socket runtime 내부의 책임 경계를 명시하기 위한 것이다.

```text
하위 계약            호출자              숨기는 것                      모르는 것
─────────────────  ────────────────  ───────────────────────────  ─────────────────────────────
endpoint registry  socket runtime    endpoint 저장 구조, id 할당   family 의미, message routing
peer state         socket runtime    peer connect/disconnect 추적  family-specific pipe event 해석
monitor bridge     socket runtime    monitor frame wire format     family 의미, message 내용
dispatch bridge    socket runtime    callback subject 저장/매칭    family-specific readiness 의미
lifecycle hooks    socket runtime    quiesce 순서, drain mechanics family-specific state machine
```

핵심은 family 구현이 다음 정도만 알게 만드는 것이다.

- 어떤 메시지 의미를 가지는가
- 어떤 routing/subscription/load-balancing 정책을 쓰는가
- 어떤 readiness 의미를 외부에 노출하는가

반대로 아래는 runtime으로 내린다.

- endpoint attach/detach bookkeeping
- monitor frame emission
- common dispatch registration
- pipe/mailbox glue
- destroy quiesce coordination

## 4. 분리 원칙

요약 결정:

- family는 semantic owner다.
- socket runtime은 공통 mechanism owner다.
- service runtime은 socket close coordinator이지 socket semantic owner가 아니다.

### 4.1 semantic 과 mechanism 을 분리한다

semantic:

- `PAIR`, `PUBSUB`, `DEALER`, `ROUTER`, `STREAM`의 의미 차이

mechanism:

- endpoint registry
- peer lifecycle bookkeeping
- common callback bridging
- monitor event plumbing

### 4.2 공통화는 family 수정 범위를 줄일 때만 허용한다

runtime을 도입하더라도
상위 family가 runtime 내부 세부를 더 많이 알아야 하면 실패다.

### 4.3 성능 민감 glue 는 runtime 안에 모은다

send-ready, dispatch, pipe wakeup, monitor fast path 같은 경로는
family마다 흩어지지 않게 runtime 안에 모은다.

### 4.4 socket runtime을 단일 mega-class로 만들지 않는다

`socket_base_t`의 과도한 책임을 비판하면서
같은 크기의 `socket_runtime_t` 하나를 다시 만들면 실패다.

구현 제약:

- §3.1의 하위 계약(endpoint registry, peer state, monitor bridge,
  dispatch bridge, lifecycle hooks)은 각각 **독립 private component**
  (composition member)로 구현한다.
- 하위 component는 자기 데이터를 가지고, socket runtime은 이들을 조합만 한다.
- 하위 component 간 직접 참조를 금지한다 — 상호 호출이 필요하면
  socket runtime이 매개한다.
- socket runtime 자체의 public method 수는 `socket_base_t`에서
  옮겨온 mechanism method 수보다 적어야 한다 (조합 인터페이스이므로).

이 규칙을 지키지 않으면 hub 타입이 이름만 바뀐 것이다.

판정 기준:

- socket runtime의 `.hpp` 파일을 열었을 때 method가 하위 component에
  위임하는 패턴이 보여야 한다.
- 하위 component 하나를 교체하거나 변경할 때 다른 component의
  코드 수정이 필요하면 분리가 불충분한 것이다.

## 5. 제안 경계

## 5.1 `socket_runtime` 계열이 가져갈 책임

- socket id / registration bookkeeping
- endpoint attach/detach tracking
- common monitor event encoding/emit
- peer connect/disconnect state tracking
- dispatch subject / callback bridge
- lifecycle quiesce helper

## 5.2 `socket_base_t`가 유지할 책임

- `xsend`, `xrecv`, `xhas_in`, `xhas_out`
- family-specific routing/distribution/subscription semantics
- family-specific poll/read/write reactions
- family-specific pipe event semantics

## 5.3 family 구현이 알면 안 되는 것

- monitor frame wire detail
- mailbox quiesce sequencing
- generic endpoint registry structure
- callback subject storage detail

## 6. 우선 적용 대상

1. `socket_base_t`
2. `stream`
3. `router`
4. `xpub/xsub`
5. `dealer/pair`

이 순서를 권장하는 이유:

- `stream`과 `router`는 callback/monitor/peer state 경계가 두드러진다.
- `xpub/xsub`는 subscription bookkeeping 분리 효과를 보기 좋다.

## 7. 구현 작업 패키지

### 7.1 Package A. endpoint / peer registry 분리

목표:

- endpoint attach/detach bookkeeping을 family 밖으로 이동

완료 조건:

- endpoint lifecycle 수정이 family 코드 변경을 덜 요구

### 7.2 Package B. monitor bridge 분리

목표:

- monitor frame 조립과 emit 경로를 runtime으로 이동

완료 조건:

- monitor semantics 변경이 family 로직을 덜 침범

### 7.3 Package C. callback / dispatch bridge 분리

목표:

- socket msg dispatch / send-ready dispatch 공통 경로 정리

완료 조건:

- callback 관련 공통 분기가 family에 남지 않음

### 7.4 Package D. destroy quiesce helper 정리

목표:

- mailbox quiesce / destroy finalize helper를 runtime contract로 노출

완료 조건:

- family 구현이 destroy sequencing 세부를 몰라도 됨

### 7.5 죽은 코드 및 불필요한 파일 정리

각 Package 작업 중 발견되는 아래 항목을 같이 제거한다.

- 호출처가 없는 함수/메서드
- include되지 않는 헤더
- 빌드에 포함되지 않는 소스 파일
- `socket_base_t`에서 runtime으로 이동한 뒤 원본에 남은 중복 코드
- 레거시 호환 shim, `#if 0` 블록, 주석 처리된 구현

제거 시 해당 phase의 기능 게이트를 통과해야 하며,
큰 단위 제거는 별도 commit으로 분리한다.

## 8. 성능 게이트

특히 아래를 본다.

- callback dispatch depth 증가 여부
- monitor 경로 branch 증가 여부
- `PAIR`, `PUBSUB`, `ROUTER`, `STREAM_CALLBACK` 회귀 여부
- endpoint attach/detach steady-state 비용 증가 여부

## 9. Phase 2 완료 조건

정성 기준:

- `socket_base_t`의 공통 기계 책임이 줄어든다.
- family 구현이 semantic 중심으로 읽힌다.
- monitor/dispatch/endpoint 변경의 수정 범위가 감소한다.
- single/multi perf가 baseline 대비 비퇴행이다.

정량 기준 (Phase 2 시작 시 baseline 측정, 완료 시 비교):

- 새 socket family 추가 시 수정되는 하위 모듈 수 (목표: ≤ 2)
- `socket_base_t` public/protected method 수 감소
- endpoint/monitor/dispatch 변경 시 family 코드 수정 파일 수 감소
- 3.1 information hiding 표의 "모르는 것"이 실제로 family에서 접근 불가

## 10. 다음 단계

Phase 2 다음은 engine/transport/service 경계 재구성이다.
socket runtime 계약이 정리되기 전에는
engine facade 재구성을 먼저 하지 않는다.

참조 문서:

- `doc/perf/refactor/05-core-system-phase3-engine-transport-service-plan.ko.md`
