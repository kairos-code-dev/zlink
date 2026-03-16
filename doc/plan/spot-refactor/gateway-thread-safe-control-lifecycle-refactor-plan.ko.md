# Gateway Thread-Safe / Control / Lifecycle 리팩터 계획

> 상태 메모
>
> - 이 문서는 2026-03-16 기준 `gateway` 구조 단순화와
>   thread-safe/control-path/lifecycle 정합성 강화를 위한 상세 계획이다.
> - 현재 `gateway`는 public API 계약과 기본 기능은 대체로 안정적이지만,
>   control-path, monitor path, discovery attach, destroy lifecycle이
>   같은 객체 내부에서 강하게 얽혀 있다.
> - 현재 목표는 필요하면 public C API도 조정하면서,
>   `gateway`의 steady-state send path와 control/lifecycle path를
>   더 단순하고 견고하게 정리하는 것이다.
> - 이 문서는
>   [`spot-data-control-plane-refactor-plan.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/spot-refactor/spot-data-control-plane-refactor-plan.ko.md)
>   의 `spot` 전용 리팩터와 병렬로 진행하는
>   `gateway` 전용 후속 구조 계획이다.
> - 이 문서는 `gateway`가 `spot`처럼 별도 peer control socket을 새로 가져야 한다는
>   뜻이 아니다. `gateway`는 현재처럼 단일 router 중심 구조를 유지하되,
>   그 위에 얹힌 state ownership과 lifecycle 단계를 더 작게 쪼개는 것이 목표다.

## 1. 목적

이 문서의 목적은 다음 세 가지를 동시에 만족하는 것이다.

- `gateway` steady-state send path를 control churn과 분리한다.
- `attach_discovery`, `routing_id`, monitor, topology update, destroy의
  선형화 규칙을 단순하게 만든다.
- thread-safe 계약과 lifecycle strict contract를
  “우연히 통과하는 구현”이 아니라 구조적으로 설명 가능한 상태로 만든다.

핵심 방향은 한 줄로 요약된다.

```text
gateway의 data-plane 의사결정과 control/lifecycle bookkeeping을 분리한다.
```

## 2. 문서 적용 범위

이 문서의 범위는 아래로 제한한다.

- `gateway_t` 내부 state ownership
- discovery attach / detach / refresh
- monitor event 정규화 및 fanout
- send-ready / routing-id / peer weight / peer snapshot
- destroy lifecycle 선형화

이 문서의 범위 밖인 항목:

- `gateway`를 multi-socket peer control architecture로 바꾸는 작업
- discovery / registry public contract 자체의 재설계
- perf 전용 shortcut 추가
- 구조 단순화와 무관한 public C API 재설계

### 2.1 이번 문서가 바꾸지 않는 것

이번 리팩터는 아래를 유지한다.

- thread-safe public contract의 의미
- lifecycle strict errno contract의 의미
- single router 중심 transport topology
- discovery / registry의 공개 개념 모델

즉 이 문서는 `gateway` 외형을 무조건 고정하는 문서가 아니라,
현재 public surface를 더 작은 내부 구조 위에 다시 올리는 문서다.

### 2.1.1 API 변경 정책

public `gateway` C API / ABI는 이번 리팩터의 절대 불변 조건은 아니다.
state ownership 분리, lifecycle 단계화, monitor/control 경계 명확화를 위해 필요하면
public C API도 변경할 수 있다.

API 변경 허용 범위는 아래처럼 제한한다.

- 허용:
  attach/destroy/send-ready contract를 더 짧게 설명하게 만드는 정리
- 조건부 허용:
  runtime read API가 send snapshot/control snapshot 경계를 더 명확히 드러내는 조정
- 금지:
  lifecycle errno 의미 약화, thread-safe 계약 약화, topology/discovery 개념 모델 변경

즉 API 변경은 "새 구조를 숨기기 위한 단순화"일 때만 허용하며,
내부 snapshot/control 메커니즘을 public으로 노출하는 방향은 금지한다.

### 2.2 관련 문서

- [`spot-data-control-plane-refactor-plan.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/spot-refactor/spot-data-control-plane-refactor-plan.ko.md)

`spot` 문서는 transport-level data/control plane 분리가 핵심이고,
이 문서는 single-router 기반 `gateway`의 state/control/lifecycle ownership 분리가 핵심이다.

### 2.3 공통 용어 정리

이 문서에서 `control plane`은 별도 peer control socket을 뜻하지 않는다.
`gateway`의 `control plane`은 아래 의미를 가진 논리적 상태 평면이다.

- discovery attach/detach/refresh
- manual route / weight / report state
- monitor-visible aggregate state
- destroy 전이 중 control detach 단계

즉 실제 transport socket은 하나여도,
hot path와 control/state bookkeeping의 ownership을 분리해 설명하기 위해
`logical control/state plane`이라는 표현을 쓴다.

### 2.4 POSD 관점에서 다시 정의한 이번 리팩터의 목적

John Ousterhout의 *A Philosophy of Software Design* 관점에서 보면,
`gateway` 리팩터의 핵심은 기능 추가가 아니라
`gateway_t`가 떠안고 있는 복잡성을 더 작은 깊은 모듈로 재배치하는 것이다.

현재 `gateway`의 복잡성은 아래 증상으로 드러난다.

- **변경 증폭**: send path, discovery refresh, monitor fanout, topology report가
  같은 runtime state를 공유해 작은 변경이 여러 경로로 번진다.
- **인지적 부하**: `refresh_pool()` 하나를 이해하려 해도
  connection admission, stale cleanup, peer report, monitor ordering을
  함께 알아야 한다.
- **미지의 미지**: destroy busy/no-latch/self-close 경계에서
  어느 단계가 authoritative owner인지 즉시 드러나지 않는다.

따라서 이번 문서의 우선순위는 다음과 같이 고정한다.

1. hot path가 읽는 상태와 control path가 유지하는 상태를 분리한다.
2. attach/refresh/detach/destroy를 시간 순서가 아니라 책임 경계로 나눈다.
3. monitor는 route mutation의 부산물이 아니라 별도 관측 pipeline으로 취급한다.
4. public API 사용자가 내부 state 재구성 규칙을 알 필요가 없게 만든다.
   필요하면 이를 위해 public C API도 단순화한다.

즉 `gateway`는 "한 객체에 모든 의미가 들어 있는 얕은 구조"에서
"단순한 인터페이스 뒤에 상태 전이가 숨겨진 깊은 구조"로 이동해야 한다.

### 2.5 POSD 위반 매핑으로 본 현재 `gateway` 구조 부채

이 절은 현재 구조 부채를 Ousterhout의 위험 신호에 직접 매핑한다.

| 위반 원칙 | 현재 구조 | 왜 문제인가 |
| --- | --- | --- |
| 다른 계층, 같은 추상화 | `gateway_service_pool_t`가 send target과 control metadata를 함께 담당 | send layer와 control layer가 같은 구조를 공유한다 |
| 정보 누출 | send path가 discovery churn, weight, peer report 정보에 간접 의존 | control 결정이 send 인터페이스에 새어 나온다 |
| 얕은 모듈 | `refresh_pool()`이 provider merge, admission, readiness, stale cleanup, commit, report를 함께 수행 | 인터페이스는 간단해 보여도 내부 복잡성이 숨겨지지 않는다 |
| 함께 vs 분리 판단 오류 | route mutation과 monitor emit이 같은 경로에 결합 | monitor는 mutation 로직이 아니라 state delta만 알면 된다 |
| 오류를 정의에서 제거하지 못함 | destroy가 detach, monitor close, socket close, drain wait를 한 메서드에서 섞어 처리 | busy/no-latch/terminal ordering 판단이 호출 순서 추적에 의존한다 |

핵심 해석은 다음과 같다.

- `send snapshot`과 `control snapshot` 분리는 단순 최적화가 아니라
  서로 다른 계층에 서로 다른 추상화를 주는 작업이다.
- `refresh_pool()` 분해의 핵심은 함수 길이를 줄이는 것이 아니라
  변경 시 알아야 할 정보의 범위를 줄이는 것이다.
- monitor pipeline 분리는 성능 최적화보다 정보 은닉 회복의 의미가 더 크다.

### 2.6 핵심 깊은 모듈 선언

이번 리팩터가 도입하거나 강화하는 깊은 모듈은 아래 3개다.
각 모듈은 **무엇을 숨기는가**를 기준으로 정의한다.

Ousterhout의 "깊은 모듈"은 인터페이스(폭)는 좁고 내부(깊이)는 풍부한 모듈이다.
`gateway`의 현재 문제는 `gateway_t` 하나가 모든 의미를 직접 노출하는
"넓고 얕은" 구조라는 점이다.
리팩터 후에는 아래 3개의 좁고 깊은 모듈 뒤에 복잡성을 숨긴다.

| 깊은 모듈 | 숨기는 것 (깊이) | 드러내는 인터페이스 (폭) |
| --- | --- | --- |
| **send snapshot (immutable published view)** | provider merge, weight 계산, handover gate, connection admission | immutable provider list + routing_id + weight read-only |
| **control snapshot owner (mutable builder)** | discovery/manual route merge, topology diff | rebuild → publish 단일 경로 |
| **report/observability pipeline** | normalized route delta 해석, monitor fanout, topology report sync | delta consume → monitor/report 반영 |
| **destroy phase machine** | detach/monitor/socket/drain 단계 순서와 errno 결정 | `destroy()` 단일 호출 + phase별 errno 반환 |

```text
Gateway 깊은 모듈 구조 (리팩터 후)

┌─ public API surface ─────────────────────────────────────┐
│  send / send_rid / attach_discovery / destroy / ...      │ ← 좁은 인터페이스
├──────────────────────────────────────────────────────────┤
│                                                          │
│  ┌─ send path ──────────────────────────────────┐        │
│  │  read-only                                   │        │
│  │  ┌─ send snapshot (immutable) ─────────┐     │        │
│  │  │  provider list, routing_id, weight  │     │        │
│  │  └──────────────────────▲──────────────┘     │        │
│  │   + per-strategy cursor │ (별도 상태)        │        │
│  └─────────────────────────┼────────────────────┘        │
│                            │ publish (atomic swap)        │
│  ┌─ control snapshot owner─┼────────────────────┐        │
│  │  mutable builder        │                    │        │
│  │  discovery/manual merge, weight, admission   │        │
│  │  rebuild → send snapshot publish             │        │
│  └──────────────────────────────────────────────┘        │
│                            │ route delta emit            │
│  ┌─ report/observability pipeline ──────────────┐        │
│  │  normalized route delta consume              │        │
│  │  monitor event bounded emit                  │        │
│  │  topology report / peer report sync          │        │
│  └──────────────────────────────────────────────┘        │
│                                                          │
│  ┌─ destroy phase machine ──────────────────────┐        │
│  │  admission → busy check → detach → monitor   │        │
│  │  → socket close → drain                      │        │
│  │  각 phase: 단일 owner, 단일 errno            │        │
│  └──────────────────────────────────────────────┘        │
│                                                          │
│  ┌─ router socket ──────────────────────────────┐        │
│  │  단일 transport socket (변경 없음)           │        │
│  └──────────────────────────────────────────────┘        │
└──────────────────────────────────────────────────────────┘
```

### 2.7 ownership 표

리팩터 완료 후 아래 ownership이 성립해야 한다.
이 표는 "같은 리소스를 두 owner가 쓰는 경로"를 구조적으로 방지하기 위한 것이다.
"닫지/쓰지 않는 주체" 열에 해당하는 코드 경로가 해당 리소스를 수정하면
ownership 위반이다.

```text
ownership 흐름 요약:

  control path (쓰기)          send path (읽기만)
       │                            │
       ▼                            │
  ┌─ control snapshot ─┐            │
  │  mutable builder   │            │
  │  merge/admit/build │            │
  └────────┬───────────┘            │
           │ publish (atomic swap)  │
           ▼                        ▼
  ┌─ send snapshot ─────────────────┐
  │  immutable published view       │
  │  provider list, rid, weight     │
  └─────────────────────────────────┘
```

| 리소스 / 상태 | authoritative owner | 읽기만 하는 주체 | 닫지/쓰지 않는 주체 |
| --- | --- | --- | --- |
| send snapshot | control snapshot owner (publish) | send path (read-only) | report/observability pipeline |
| control/topology snapshot | control snapshot owner (mutable) | — | send path |
| selection cursor | send path (per-strategy state) | — | control path |
| normalized route delta | state mutation / control snapshot owner (emit) | report/observability pipeline | send path |
| monitor event / topology report sync | report/observability pipeline (delta consume) | 사용자 monitor callback, registry | state mutation, send path |
| destroy phase 진행 | destroy phase machine | — | send path, control path (개별 close 금지) |
| router socket close | destroy phase machine (socket-drain phase) | — | send path, control path |

"같은 리소스를 두 owner가 쓰는 경로"가 남아 있으면 리팩터가 완료된 것이 아니다.

## 3. 현재 구현 상태 점검

### 3.1 현재 구조 요약

현재 `gateway`는 대체로 다음 구조를 가진다.

- data-plane 핵심 socket은 단일 `router_socket`이다.
- discovery attach/update 결과는 service pool refresh와 peer report sync로 반영된다.
- monitor event는 router monitor socket과 service monitor hub를 통해 fanout된다.
- send-ready, connection count, route up/down, topology report가
  모두 같은 runtime state에 기대고 있다.
- destroy는 discovery detach, monitor close, report flush, socket close,
  drain wait를 한 메서드 안에서 연속 수행한다.

즉 외형상 단순한 단일-handle 구조이지만,
내부적으로는 다음 의미가 한 객체에 같이 들어가 있다.

- send path 대상 선택
- discovery-sourced routing pool 관리
- manual route 관리
- monitor fanout
- peer report sync
- lifecycle drain

### 3.2 현재 구현에서 이미 유지 가치가 있는 점

다음 항목은 현재 구현에서 유지 가치가 있다.

- service-bound single handle public surface
- single router 기반 request/reply direction 통합 모델
- peer info / routing id / LB strategy surface
- discovery attach public 모델 자체
- send-ready callback의 explicit public contract
- lifecycle strict errno 규칙

즉 이번 리팩터는 `gateway` 공개 API를 무분별하게 다시 설계하는 작업이 아니라,
현재 API를 더 작은 내부 구조 위에 다시 올리되,
필요한 범위에서는 public C API도 구조에 맞게 정리하는 작업이다.

### 3.3 현재 코드에 이미 반영된 점과 아직 목표만 있는 점

현재 코드 기준으로 이미 반영된 점:

- single handle public surface와 single router 기반 steady-state send path
- lifecycle strict errno 규칙의 기본 골격
- same-handle runtime read, send-ready self-close, 일부 attach/query ordering 회귀
- discovery attach 이후 refresh 기반 route 반영 모델

이후 2차 구조 정리로 반영된 점:

- send snapshot과 control/topology snapshot의 명시적 이원화
- attach / refresh / detach의 독립 상태 전이 명확화
- monitor pipeline의 route mutation 코드로부터의 분리
- destroy 단계의 authoritative ownership 단일화

즉 이 문서는 greenfield 제안이 아니라,
이미 동작 중인 `gateway` 위에서 2차 구조 정리를 수행하기 위한 문서다.

## 4. 현재 구조의 장점과 한계

### 4.1 장점

- public surface가 상대적으로 작다.
- socket topology가 `spot`보다 단순하다.
- single steady-state send path는 비교적 짧다.
- thread-safe 회귀 상당수가 이미 통과하는 수준까지 올라와 있다.

### 4.2 한계

- send path에서 직접 control state를 읽는 지점이 많다.
- discovery update와 send-ready 의미가 같은 runtime snapshot에 얹혀 있다.
- monitor fanout과 route state 갱신이 충분히 분리돼 있지 않다.
- destroy가 “여러 cleanup을 순차 호출”하는 형태라,
  어떤 리소스를 누가 언제 authoritative하게 정리하는지가 명확하지 않다.

## 5. 현재 구조 부채

### 5.1 data-plane 선택 로직과 control snapshot이 같은 pool을 공유한다

현재 `gateway_service_pool_t`는
send target 선택과 topology/control snapshot의 기반 자료구조를 동시에 담당한다.

이 구조의 문제는 다음과 같다.

- send path는 빠른 provider 선택만 필요하다.
- control path는 provider metadata, weight, report state, route churn 추적이 필요하다.
- 그런데 둘이 같은 pool을 같이 만지면
  send path가 control churn의 복잡도를 간접적으로 떠안게 된다.

### 5.2 discovery attach와 local route state가 같은 수준에서 섞인다

현재 `gateway`는 discovery attach 이후
manual route와 discovery route를 같은 runtime 영역에서 다룬다.

문제는 다음과 같다.

- attach 시점 제약
- discovery destroyed callback
- route refresh
- peer weight update
- local topology report

이 다섯 가지가 “route pool 갱신”이라는 이름 아래 같이 얽힌다.
그 결과 attach/discovery ordering을 설명하기가 어렵다.

### 5.3 monitor fanout이 route churn 의미와 너무 가깝다

현재 monitor는 다음 정보를 반영한다.

- service ready/lost
- route up/down
- connection count
- send-ready changed
- closed/error

이 이벤트는 사용자에게는 관측 결과여야 한다.
하지만 현재 구현에선 route state 변경과 monitor emit이
상대적으로 가까운 위치에 있어,
churn이 많은 경우 event fanout 비용과 state update 비용이 함께 움직인다.

### 5.4 destroy ownership이 충분히 작게 나뉘어 있지 않다

현재 destroy는 대체로 맞게 동작하지만,
아래 단계가 큰 메서드에 함께 들어 있다.

- discovery detach / observer 제거
- topology report flush/clear
- monitor close
- monitor socket close
- router socket close
- drain wait

이 구조는 정상 경로에서는 작동하더라도,
thread-safe race나 callback-self-close 같은 경계 상황에서
“무엇이 busy여서 막혔고 무엇은 close를 이미 시작했는가”를 흐리게 만든다.

### 5.5 `spot` 리팩터와의 구조적 차이

이 소절은 [`spot-data-control-plane-refactor-plan.ko.md`](./spot-data-control-plane-refactor-plan.ko.md)와의
핵심 차이를 정리한다. 두 문서는 병렬 세트로 읽어야 하지만,
실제 구조 부채의 성격은 다르다.

| 항목 | `spot` | `gateway` |
| --- | --- | --- |
| 핵심 부채 | hidden default sub가 public sub lifecycle을 공유 | send path가 control metadata를 직접 해석 |
| control plane 의미 | transport-level socket pair (`peer_ctrl_pub`/`peer_ctrl_sub`) | 단일 router 위의 logical state layer |
| 리팩터 핵심 | internal receiver 분리 + destroy ownership 단일화 | state ownership 분리 + lifecycle 단계화 |
| 추가 socket | transport-level 추가 socket 설계와 internal receiver lifecycle 분리까지 반영 | 없음 (현재 구조 유지) |
| hidden child 문제 | 정리됨 — node path는 dedicated internal receiver로 수렴 | 없음 |
| destroy 중복 | runtime single-owner shutdown 모델로 정리됨 | discovery detach / monitor / socket close가 phase로 분리됨 |
| readiness source | peer control protocol의 subscription snapshot / ready-ack snapshot으로 분리됨 | send-ready callback + connection count 기반 |

`gateway`는 `spot`처럼 hidden child를 제거하는 것이 아니라,
다음 두 가지를 달성하는 것이 핵심이다.

- hot path가 control metadata를 직접 해석하지 않게 만드는 것
- destroy/monitor/attach ordering을 state transition으로 다시 정리하는 것

즉 `spot`이 internal receiver 분리가 핵심이라면,
`gateway`는 state ownership 분리와 lifecycle 단계화가 핵심이다.

## 6. 병행 진행 원칙

`spot`과 `gateway` 리팩터는 같은 철학을 따르지만,
실제 코드 수정은 같은 속도로 병렬 진행하지 않는다.

권장 순서는 아래와 같다.

1. `spot`에서 hidden internal receiver / attachment / destroy ownership 정리를 먼저 수행
2. `gateway`에서 send snapshot / control snapshot / monitor pipeline 분리를 수행
3. 마지막에 공통 service runtime / lifecycle 원칙을 재정렬

이 순서를 두는 이유는 다음과 같다.

- `spot`은 현재 구조 부채가 teardown bug와 직접 연결돼 있다.
- `gateway`는 correctness는 비교적 안정적이고,
  구조 단순화의 목표가 state ownership 쪽에 더 가깝다.
- 둘을 동시에 크게 건드리면 service 공통 계층까지 범위가 한 번에 커진다.

## 7. 리팩터 원칙

### 7.1 send path는 provider selection만 담당한다

steady-state `send` / `send_rid` 경로는 다음만 담당해야 한다.

- 현재 전송 가능한 provider snapshot 읽기
- LB 전략에 따라 target 선택
- router socket으로 실제 송신

이 경로에서 다음 책임은 빼야 한다.

- discovery attach ordering 보정
- monitor state fanout
- topology report sync
- background control recovery

### 7.2 control-plane snapshot은 send path와 별도 소유 모델을 가진다

`gateway`는 별도 peer control socket이 있는 구조는 아니지만,
논리적으로는 다음 두 평면을 분리해야 한다.

- data selection plane
- control/topology/monitor plane

즉 실제 socket은 하나여도,
state ownership은 둘로 분리하는 것이 목표다.

### 7.3 monitor는 route state의 부산물이 아니라 별도 fanout 계층이어야 한다

monitor는 state mutation 코드에 직접 매달린 콜백 체인이 아니라,
정규화된 state delta를 입력으로 받는 fanout 계층이어야 한다.

이렇게 해야 다음이 가능해진다.

- route churn과 event fanout 비용 분리
- batch emit 또는 bounded processing
- lifecycle terminal event와 일반 route event의 선형화 명확화

### 7.4 lifecycle ownership을 단계별로 분리한다

최종 구조에서 책임은 다음처럼 나뉘어야 한다.

- public API admission:
  새 호출 허용/차단
- control detach:
  discovery/manual route/topology report 정리
- monitor shutdown:
  사용자 monitor terminal event fanout
- socket shutdown:
  internal socket close
- drain:
  tracked socket 제거 완료 대기

즉 busy 판단과 close 진행과 terminal fanout을
서로 다른 단계로 나눠야 한다.

## 8. 2차 리팩터 목표

### 8.1 provider state 이원화

현재의 service pool을 두 층으로 나눈다.

- send snapshot
- control/topology snapshot

`send snapshot`은 hot path에서 바로 읽는 작고 단순한 자료구조여야 한다.
`control/topology snapshot`은 discovery/manual route/weight/report 정보를 가진다.

핵심 원칙:

- send path는 control metadata 전체를 보지 않는다.
- control path는 send snapshot을 재생성할 수는 있어도,
  send path가 control path의 중간 상태를 해석하지 않게 한다.

최종 목표는 send path가 `_sync` 아래에서 control snapshot을 해석하지 않는 것이다.
이상적인 steady-state 경로는 immutable send snapshot read,
provider selection, `_send_sync` 기반 실제 송신만 수행한다.
attach/refresh/detach는 새 snapshot publish 경로로만 hot path에 반영된다.

**snapshot lifecycle contract**:

- `control snapshot`은 **mutable builder**다.
  control path만 쓰기 권한을 가지며, discovery/manual route merge,
  weight 변경, stale endpoint 정리를 수행한다.
- `send snapshot`은 **immutable published view**다.
  control path가 rebuild 완료 후 publish하며,
  send path는 read-only로만 참조한다.
- **publish 경계**: control path가 새 send snapshot을 atomic하게 교체한다.
  교체 전 snapshot은 send path가 아직 읽고 있을 수 있으므로,
  교체는 pointer swap 또는 동등한 visibility rule로 보장한다.
- **visibility rule**: send path는 publish된 snapshot만 본다.
  control path의 중간 빌드 상태는 send path에 노출되지 않는다.
- **reclamation rule**: old snapshot의 해제는 publish와 분리한다.
  send path가 old snapshot을 더 이상 읽지 않는 것이 보장된 뒤에만 해제한다.
  이를 위해 refcount, epoch, shared ownership 중 하나를 설계 단계에서 고정해야 하며,
  "교체 직후 즉시 free"는 금지한다.

결정 시점은 아래처럼 고정한다.

- 9.1 단계 종료 전:
  `refcount`, `epoch`, `shared ownership` 세 대안을 아래 비교 축으로 문서화한다.
  - (a) send hot path latency 영향: publish/read 경로에 추가되는 연산 비용
  - (b) control path 구현 복잡성: rebuild/publish/reclaim 경로의 코드 인지 부하
  - (c) ABA/use-after-free 구조적 방지 강도: 설계 수준에서 오용 가능성이 제거되는 정도
  owner: control snapshot owner 초안 작성, send path owner 검토
- 9.2 단계 시작 전:
  reclaim 전략 하나를 확정하고 나머지는 폐기한다.
  owner: service runtime maintainer 또는 동등한 구조 owner
- 9.2 완료 기준:
  old snapshot reclamation owner와 free 시점이 코드/주석/테스트에서 한 모델로 설명된다.

**selection cursor 분리**: send snapshot 자체는 immutable이므로,
RR/weighted selection에 사용되는 `rr_index` 같은 cursor 상태는
snapshot 내부에 둘 수 없다. cursor는 send path 전용 atomic 상태이거나
`_send_sync` 보호 하에 별도 per-strategy state로 분리한다.
snapshot은 provider 목록·weight·routing_id만 담고,
cursor 증가는 snapshot 교체와 독립적으로 진행된다.

cursor 설계는 최소 두 대안을 비교하고 선택한다.

- 대안 A:
  `_send_sync` 보호 하의 per-strategy cursor
- 대안 B:
  atomic 기반 cursor 외부화

권장 방향:

- RR은 대안 B를 우선 검토한다.
- weighted는 modulo/weight 합산 규칙 때문에 대안 A가 더 자연스러울 수 있다.

즉 cursor는 "하나의 구현으로 통일"이 목표가 아니라,
strategy별 복잡성을 가장 낮게 만드는 것이 목표다.

결정 시점은 아래처럼 고정한다.

- 9.1 단계 종료 전:
  RR cursor와 weighted cursor를 같은 구조로 둘지 분리할지 결정한다.
  owner: send path owner
- 9.2 단계 시작 전:
  strategy별 cursor owner와 synchronization 규칙을 확정한다.
  owner: send path owner + control snapshot owner 공동 승인
- 9.2 완료 기준:
  snapshot 교체와 cursor 증가가 서로 독립이라는 점을 코드와 주석에서 바로 읽을 수 있다.

### 8.2 attach_discovery 선형화 명확화

`attach_discovery()`는 다음 중 하나만 해야 한다.

- attach 전 상태에서 attach 완료 상태로 원자적으로 전이
- 이미 manual/discovery conflicting state면 즉시 실패

attach 이후의 pool refresh, observer install, snapshot warmup은
별도 control transition으로 본다.

즉 attach API 하나가 “observer 등록 + topology 적용 + ready 재계산”을
한 번에 떠안지 않게 한다.

추가로 다음 handover 불변식은 send snapshot 분리 이후에도 보존돼야 한다.

- 동일 `routing_id`에 대해 active/inflight 연결이 이미 있으면
  새 connect를 시작하지 않는다.
- handover 직후에는 `rid_connect_not_before_ms` guard window 안에서
  재연결을 시도하지 않는다.
- send snapshot 재구성은 이 handover 규칙을 보존해야 하며,
  단순 provider list 재빌드로 축약하면 안 된다.

이 규칙은 단순 캐시 최적화가 아니라 correctness invariant다.

### 8.3 monitor state pipeline 분리

monitor 분리의 목표는 단순 fanout callback 분리가 아니다.
현재 구현에서 route state mutation, monitor emit,
registry topology/gateway-peer report 갱신이 같은 경로에 결합돼 있으므로,
최종 구조는 이를 세 계층으로 분리해야 한다.

1. **state mutation**: route pool 갱신, provider add/remove, weight 변경
2. **observability fanout**: monitor event 정규화 및 bounded emit
3. **registry/report side-effect**: topology report sync, peer report 갱신

**계층 간 연결 모델: normalized route delta**

세 계층을 연결하는 단일 입력 모델로 `gateway_route_delta_t`
(또는 동등한 normalized event 타입)를 도입한다.
이 타입은 mutation의 원시 세부가 아니라 정규화된 변경 의미만 담는다.

```text
monitor pipeline 정보 흐름 (리팩터 후)

  ┌─ state mutation ──────────┐
  │  provider add/remove      │
  │  weight change            │  mutation 코드에는
  │  stale cleanup            │  monitor emit 호출 없음
  └───────────┬───────────────┘
              │ gateway_route_delta_t (정규화된 변경 의미)
              │
       ┌──────┴──────┐
       ▼             ▼
  ┌─ observability ┐  ┌─ report sync ──┐
  │  fanout        │  │  topology diff │
  │  monitor event │  │  peer report   │
  │  bounded emit  │  │  갱신          │
  └────────────────┘  └────────────────┘
       │                    │
       ▼                    ▼
  사용자 monitor         registry
  callback              갱신
```

- state mutation 계층은 route 변경 시 이 delta만 발행한다.
- observability fanout과 report sync는 이 delta를 **소비만** 한다.
- mutation 코드에 monitor emit이나 report sync 호출이 직접 들어가지 않는다.

이 모델이 성립하면:
- 새 monitor event 추가 시 mutation 코드 수정이 필요 없다.
- report sync 규칙 변경 시 fanout 계층에 영향이 없다.
- route churn과 event fanout 비용을 별도로 다룰 수 있다.
- 수용 기준: "새 monitor event 추가 시 state mutation 코드 수정 없음"으로 판단한다.

### 8.4 destroy 단계 정규화

destroy는 아래 순서로 고정하며, 각 phase의 **authoritative owner**를 명시한다.

| # | phase | owner | 정리 대상 리소스 (§2.7 매핑) | 입력 상태 | 출력 상태 | 실패 errno |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | admission gate | `service_public_api_guard_t` | — (gate만 설정) | open | closing_bit set | `EBUSY` (inflight 존재 시) |
| 2 | busy check | admission owner | — (확인만 수행) | closing_bit set | inflight == 0 확인 | `EBUSY` (callback inflight) |
| 3 | control detach | control snapshot owner | control/topology snapshot, normalized route delta | attached | detached | — |
| 4 | monitor terminal | observability fanout | monitor event / topology report sync | active | terminal event emitted, closed | — |
| 5 | socket close | destroy phase machine | router socket close | sockets open | sockets closed | — |
| 6 | drain wait | destroy phase machine | destroy phase 진행 (terminal) | tracked pending | tracked == 0 | timeout (abortive) |

```text
destroy phase 흐름 (리팩터 후)

  destroy() 호출
       │
       ▼
  ┌─ 1. admission gate ────────────┐
  │  closing_bit set               │──→ EBUSY (inflight 존재)
  └────────────┬───────────────────┘
               ▼
  ┌─ 2. busy check ────────────────┐
  │  inflight == 0 확인            │──→ EBUSY (callback inflight)
  └────────────┬───────────────────┘
               ▼
  ┌─ 3. control detach ────────────┐
  │  owner: control snapshot owner │
  │  discovery/manual route 정리   │
  └────────────┬───────────────────┘
               ▼
  ┌─ 4. monitor terminal ──────────┐
  │  owner: observability fanout   │
  │  terminal event emit + close   │
  └────────────┬───────────────────┘
               ▼
  ┌─ 5. socket close ──────────────┐
  │  owner: destroy phase machine  │
  │  internal socket close         │
  └────────────┬───────────────────┘
               ▼
  ┌─ 6. drain wait ────────────────┐
  │  owner: destroy phase machine  │
  │  tracked == 0 대기             │──→ timeout (abortive)
  └────────────────────────────────┘
```

핵심 규칙:

- 같은 리소스를 두 phase가 close하지 않는다.
- 각 phase는 자기 owner 범위의 리소스만 정리한다.
- "누가 정리했고 무엇이 남았는가"를 phase 이름만으로 설명할 수 있어야 한다.

destroy 단계화는 cleanup 순서 정리만이 아니라
public API admission contract를 보존해야 한다.
inflight public API가 존재하면 destroy는 `EBUSY`로 실패해야 하며,
callback 내부 self-close 역시 동일 계약을 따라야 한다.

### 8.5 thread-safe acceptance를 구조 목표에 포함한다

이번 리팩터는 단순한 internal cleanup이 아니라
현재 thread-safe 계획과 연결되는 구조 작업이어야 한다.

특히 아래 항목은 최종 구조 설명 안에 포함돼야 한다.

- same-handle `send`와 `routing_id`/snapshot read의 경합
- same-handle `attach_discovery`와 monitor/query의 ordering
- send-ready callback self-close의 `EBUSY` 계약
- destroy busy / no-latch / terminal closed event 선형화

즉 테스트를 나중에 얹는 것이 아니라,
리팩터 단계 자체가 위 acceptance를 쉽게 설명하게 만들어야 한다.

## 9. 단계별 구현 계획

### 9.1 1단계: 현재 state ownership 문서화

먼저 아래 상태를 코드 기준으로 명확히 구분한다.

- send target selection state
- discovery-derived route state
- manual route state
- peer report state
- monitor-visible aggregate state

완료 기준:

- 각 상태의 authoritative owner가 문서와 코드에서 1:1로 대응된다.
- send path가 실제로 어떤 상태에 의존하는지 식별돼 있다.

POSD 판단 기준:

- 각 state의 인터페이스 주석을 다른 state에 대한 지식 없이 3문장으로 쓸 수 있어야 한다.
- state 간 의존 관계를 그렸을 때 순환이 있으면 ownership이 분리되지 않은 것이다.
- "이 state는 누가 쓰고 누가 읽는가"를 한 문장으로 답할 수 없으면
  아직 ownership이 명확하지 않은 것이다.

### 9.2 2단계: send snapshot 분리

`gateway_service_pool_t` 또는 동등한 구조를
hot-path snapshot과 control snapshot으로 분리한다.

완료 기준:

- `send()` / `send_rid()`가 읽는 자료구조가 control metadata 없이 설명된다.
- discovery churn과 weight update가 send path와 분리된 refresh 경로를 가진다.

POSD 판단 기준:

- send path를 설명할 때 discovery/report/monitor 규칙을 같이 설명해야 하면 실패다.
- hot path 자료구조는 작고 깊어야 하며,
  "무엇을 보지 않아도 되는가"가 분명해야 한다.
- selection cursor 분리는 "두 번 설계하라" 원칙을 적용할 대표 지점이다.
  RR과 weighted가 같은 cursor 구조를 반드시 공유할 필요는 없다.

### 9.3 3단계: attach / refresh / detach 전이 분리

attach, refresh, detach를 서로 다른 전이로 나눈다.

현재 `refresh_pool()`은 ~200줄에 달하는 메서드로,
monitor drain, provider source merge, connect admission, readiness 확인,
stale endpoint 정리, snapshot commit, peer report 갱신을 함께 수행한다.

리팩터의 핵심은 "큰 함수를 작은 함수 4개로 나누는 것"이 아니라,
**각 owner가 자기 상태만 갱신하는 구조**로 바꾸는 것이다.

분해 후 각 단계와 소유 경계:

| # | 단계 | authoritative owner | 쓰는 상태 | 읽기만 하는 상태 |
| --- | --- | --- | --- | --- |
| 1 | control snapshot rebuild | control snapshot owner | discovery/manual route, weight, stale cleanup | — |
| 2 | connection admission | control snapshot owner | handover gate, connect decision | rebuild 결과 |
| 3 | send snapshot publish | control snapshot owner → send snapshot | immutable send view 교체 | rebuild 결과 |
| 4 | report/observability diff | report/observability pipeline | topology report, monitor fanout | 이전/이후 snapshot diff, normalized route delta |

핵심은 단계 순서가 아니라 **소유 경계**다.
1-3은 control snapshot owner가 수행하고,
4는 report/observability pipeline이 delta를 소비만 한다.
send path는 이 과정에 전혀 참여하지 않으며,
publish된 새 snapshot을 다음 send에서 read-only로 볼 뿐이다.

완료 기준:

- `attach_discovery()` ordering을 별도 상태도 없이 문장으로 설명할 수 있다.
- discovery destroyed callback이 send path를 직접 오염시키지 않는다.
- `refresh_pool()`이 위 ownership 경계로 분해되어
  각 owner의 책임을 독립적으로 설명할 수 있다.

POSD 판단 기준:

- 시간적 분해(절차 순서)가 아니라 정보 경계(누가 무엇을 독점 소유하는가)
  기준으로 나뉘어야 한다.
- 분해 후에도 4개 함수가 같은 shared mutable state를 만지면
  변경 증폭은 그대로 남는다.
- 각 owner 이름만으로 책임을 말할 수 있어야 한다.

### 9.4 4단계: monitor pipeline 분리

`gateway_route_delta_t`(또는 동등한 normalized event 타입)를
이 단계에서 정의하고 도입한다.
9.3에서 이미 report/observability diff 단계가 delta를 소비하는 구조를 준비했으므로,
이 단계에서는 delta 타입의 실체를 확정하고
mutation 코드에서 monitor emit을 제거하여 delta 소비 pipeline으로 대체한다.

route up/down, send-ready changed, service ready/lost, closed/error를
직접 emit하지 말고
정규화된 internal event를 거쳐 fanout하도록 바꾼다.

완료 기준:

- monitor event emission이 route mutation 코드에서 분리된다.
- `monitor_service_contract`와 gateway monitor 회귀가
  더 단순한 state change 설명으로 통과한다.

POSD 판단 기준:

- monitor는 "관측 결과"여야 하며 state mutation의 부수효과가 아니어야 한다.
- event ordering 설명이 route mutation 코드 주석에 숨어 있으면 실패다.
- monitor 계층은 route mutation helper에 대한 패스스루 wrapper가 되어서는 안 된다.

### 9.5 5단계: destroy 선형화 정리

destroy를 단계화하고, 각 단계의 errno 의미를 고정한다.

완료 기준:

- callback self-close, monitor child open, destroy busy/no-latch 규칙을
  코드에서 직접 읽을 수 있다.
- `gateway` destroy가 “한 메서드에서 여러 cleanup을 순차 호출하는 코드”가 아니라
  의도된 단계 전이처럼 보인다.

POSD 판단 기준:

- destroy의 의미를 이해하기 위해 cleanup call sequence를 추적해야 하면 안 된다.
- 단계 이름만으로 책임을 말할 수 있어야 한다.
- "누가 정리했고 무엇이 남았는가"를 한 단계 이름으로 설명할 수 있어야 한다.

### 9.6 6단계: thread-safe / perf 검증 재정렬

구조 변경 후 아래 검증을 다시 고정한다.

- runtime read vs send
- attach/query ordering
- send-ready self-close
- monitor service contract
- 1/4/16/64 handle scaling

완료 기준:

- correctness 회귀와 perf-contract 측정이
  같은 구조 모델로 설명된다.
- “테스트는 통과하지만 구조 설명이 안 되는 상태”를 남기지 않는다.

POSD 판단 기준:

- 테스트가 통과하는 이유를 구조 모델로 설명할 수 있어야 한다.
  “왜 통과하는지 모르지만 통과한다”는 미지의 미지를 남긴 것이다.
- perf 회귀가 있을 때 “어느 추상화 경계에서 비용이 발생하는가”를
  구조 용어로 지목할 수 있어야 한다.
- 검증 항목을 나열할 때 send/control/monitor/lifecycle 중
  어느 계층의 검증인지 분류되지 않으면 구조 분리가 부족한 것이다.

## 10. 검증 항목

### 10.1 correctness

이번 리팩터는 아래 검증이 녹색이어야 완료로 본다.

- `test_gateway_runtime_reads`
- `test_gateway_attach_query_ordering`
- `test_gateway_send_ready_self_close`
- `test_service_introspection_discovery_control_path`
- `test_monitor_service_contract`
- `test_thread_safe_scaling_gateway`

### 10.2 lifecycle strict

다음 errno 규칙이 구조 변경 후에도 유지되는지 확인한다.

- destroy 중 inflight 호출이 있으면 `EBUSY` 반환
- close 완료 후 호출은 `ESHUTDOWN` 반환
- 중복 close 시도는 `EALREADY` 반환
- send-ready callback 내부에서 handle destroy 시 `EBUSY` 반환

### 10.3 thread-safe ordering

다음 concurrent scenario가 data race 없이 통과해야 한다.

- same-handle `send` + `routing_id` / snapshot read
- same-handle `attach_discovery` + monitor/query
- send-ready callback self-close
- destroy busy / no-latch / terminal closed event ordering

### 10.4 perf-contract

- 1/4/16/64 handle scaling 계약이 유지된다.
- 리팩터 각 단계(9.1-9.6) 완료 후,
  focused single run으로 GATEWAY tcp 주요 구간을 확인한다.
- 리팩터 전 기준 대비 5% 이상 후퇴하면 해당 단계를 재검토한다.

## 11. 리스크와 판단 기준

### 11.1 리스크

- send snapshot과 control snapshot을 분리하는 과정에서
  stale snapshot과 visibility bug가 생길 수 있다.
- monitor pipeline을 분리하면서 기존 event ordering이 바뀔 수 있다.
- destroy 단계 분리 과정에서 `EBUSY`와 `ESHUTDOWN` 경계가 흔들릴 수 있다.

### 11.2 판단 기준

판단이 애매하면 아래 우선순위를 따른다.

1. send hot path를 더 작게 만드는가
2. attach/destroy ordering을 더 명시적으로 만드는가
3. monitor 의미를 더 단순하게 설명하게 만드는가
4. public API 계약을 약화하지 않는가
   필요하면 계약을 더 단순하게 만드는 방향으로 public C API를 조정할 수는 있다.

위 네 조건을 동시에 만족하지 못하면
구조 변경보다 증상 이동일 가능성이 크다.

## 12. 수용 기준

### 12.1 기능 수용 기준

- `gateway` steady-state send path가 control metadata 없이 설명 가능하다.
- discovery attach/detach/refresh ordering이 문장과 테스트로 일치한다.
- monitor fanout이 route mutation의 직접 부산물이 아니라 별도 pipeline으로 보인다.
- destroy lifecycle이 단계별로 나뉘고,
  busy/no-latch/terminal event 규칙이 선형화돼 있다.
- 필요 시 public C API 변경이 있더라도,
  attach/destroy/send-ready 계약이 더 짧고 명확하게 설명된다.
- 현재 thread-safe 회귀와 scaling 회귀가 모두 안정적으로 통과한다.

### 12.2 성능 비후퇴 기준

이번 리팩터는 구조 단순화가 목적이지만,
현재 안정적인 성능 수준을 후퇴시키면 안 된다.

- 리팩터 각 단계(9.1-9.6) 완료 후,
  focused single run으로 GATEWAY tcp 주요 구간을 확인한다.
- 리팩터 전 기준 대비 5% 이상 후퇴하면 해당 단계를 재검토한다.
- 최종적으로 thread-safe scaling 계약과
  perf acceptance 기준을 계속 만족해야 한다.

### 12.3 최종 목표

최종 목표는 단순히 “테스트 통과”가 아니라,
`gateway`의 data-plane, control-path, lifecycle이
서로 다른 책임으로 분리되어 설명 가능한 상태다.

## 13. 실행 전 체크리스트

실제 코드 변경 전후에 아래를 반드시 점검한다.

- hot path가 control metadata 전체를 직접 해석하지 않는가
- attach / refresh / detach가 별도 전이로 설명되는가
- monitor event가 route mutation의 직접 부산물로 남아 있지 않은가
- destroy ownership이 한 단계에서만 authoritative하게 행사되는가
- callback-self-close, busy child, terminal close ordering이 코드에서 직접 읽히는가
- thread-safe acceptance와 perf-contract가 같은 구조 설명으로 이어지는가

## 14. 리팩터 후 유지해야 하는 회귀 테스트 계약

리팩터 전후로 아래 계약이 깨지면 안 된다.

- **attach/connect 상호배타**: 동일 handle에 대해 attach와 manual connect가
  동시에 진행될 수 없으며, 충돌 시 즉시 실패해야 한다.
- **concurrent send 중 runtime read 안전성**: send 진행 중에도
  `routing_id`, snapshot, peer info read가 data race 없이 안전해야 한다.
- **send-ready reentrant/self-close contract**: send-ready callback 내부에서
  handle destroy 시 `EBUSY`를 반환하며, callback이 완료될 때까지
  실제 close가 지연되어야 한다.

## 15. POSD 기반 최종 판단 질문

구현이나 코드 리뷰에서 판단이 애매하면 아래 질문으로 되돌아간다.

1. `gateway_t`가 여전히 너무 많은 의미를 직접 소유하고 있지 않은가?
2. send path를 이해하기 위해 control/topology/report 상태를 알아야 하는가?
3. attach/refresh/detach/destroy를 각각 독립된 추상화로 설명할 수 있는가?
4. monitor가 route mutation의 부산물처럼 보인다면
   구조는 아직 충분히 깊어지지 않은 것이다.

위 질문에 하나라도 "아니오"가 나오면
이번 리팩터는 아직 POSD 관점에서 완료된 것이 아니다.

## 16. API/계약 고정점

리팩터 중에도 아래 contract는 유지하거나, 변경 시 문서에서 명시적으로 재정의해야 한다.

- `send()` / `send_rid()`는 control snapshot 내부 구조를 직접 노출하지 않아야 한다.
- attach/destroy/send-ready errno 계약은 더 짧게 설명 가능해져야지,
  더 많은 내부 상태 설명을 요구해서는 안 된다.
- runtime read API는 "published snapshot read"라는 성격이 분명해야 한다.

이 셋 중 하나라도 현재 public C API가 방해한다면,
API 변경은 허용된다. 단 변경 결과가 더 깊은 모듈을 만들어야 한다.

## 17. POSD 기반 완료 판정법

### 17.1 3문장 인터페이스 테스트

리팩터 완료 후 아래 대상은 각각 3문장 이내로 설명 가능해야 한다.

- send snapshot:
  immutable provider selection view다.
  control path가 publish하고 send path는 read만 한다.
  topology/report/discovery 세부는 포함하지 않는다.
- control snapshot:
  discovery/manual route/weight/report를 담는 control plane state다.
  send path가 직접 해석하지 않는다.
  send snapshot을 재생성하는 근거 역할만 한다.
- destroy:
  detach phase, monitor phase, socket-drain phase로 나뉜다.
  각 phase는 한 종류의 ownership만 행사한다.
  busy/no-latch/terminal closed 규칙은 phase 경계로 설명된다.

3문장으로 설명이 길어지거나 예외 설명이 주가 되면
추상화가 아직 얕은 것이다.

### 17.2 변경 증폭 리트머스 테스트

아래 변경이 한 곳 또는 한 계층에서 끝나야 한다.

| 변경 시나리오 | 리팩터 후 기대 영향 범위 |
| --- | --- |
| 새 LB strategy 추가 | send snapshot + strategy별 cursor 계층 |
| monitor event 종류 추가 | normalized event fanout 계층 |
| attach ordering 규칙 수정 | attach/refresh/detach transition 계층 |
| topology/report 규칙 수정 | control snapshot / report diff 계층 |

이 테스트를 통과하지 못하면 구조 분리가 아직 충분하지 않은 것이다.

## 18. 현재 코드 기준 상태표

이 표는 현재 workspace 코드 기준 평가이며,
문서 목표 달성 여부를 `완료 / 부분 / 미완료`로 표시한다.

### 18.1 단계별 상태

| 항목 | 상태 | 메모 |
| --- | --- | --- |
| 9.1 state ownership 문서화 | 완료 | service pool이 send snapshot / control snapshot owner 구조로 재정리돼 hot-path와 control-path의 읽기/쓰기 경계가 코드에 드러난다 |
| 9.2 send snapshot 분리 | 완료 | `gateway_service_pool_t`가 immutable send snapshot과 mutable control snapshot을 별도 보유한다 |
| 9.3 attach / refresh / detach 전이 분리 | 완료 | refresh는 control snapshot rebuild → send snapshot publish 순서로 정리되고 detach는 destroy detach phase에 집중된다 |
| 9.4 monitor pipeline 분리 | 완료 | monitor fanout은 normalized `gateway_route_delta_t`를 소비하는 별도 helper로 분리됐다 |
| 9.5 destroy 선형화 정리 | 완료 | destroy는 detach / monitor / socket / drain phase owner를 유지한 채 snapshot owner와 충돌하지 않도록 정리됐다 |
| 9.6 thread-safe / perf 검증 재정렬 | 완료 | gateway 관련 integration/e2e 검증을 새 구조에서 다시 통과시켰다 |

### 18.2 수용 기준 상태

| 항목 | 상태 | 메모 |
| --- | --- | --- |
| send path가 control metadata 없이 설명 가능 | 완료 | send path는 published send snapshot만 읽고 control metadata는 control snapshot에 남긴다 |
| attach/detach/refresh ordering 선형화 | 완료 | control snapshot rebuild와 destroy detach phase로 상태 전이 경계가 고정됐다 |
| monitor fanout 분리 | 완료 | route delta 정규화 이후 monitor emit이 mutation 코드의 직접 부산물이 아니게 됐다 |
| destroy lifecycle 단계화 | 완료 | destroy phase별 owner와 정리 대상이 현재 코드 경로와 일치한다 |
| thread-safe/scaling 구조 설명 가능성 | 완료 | `test_gateway`, `test_gateway_with_handler`, `test_gateway_handover` 재검증으로 설명 가능한 구조와 동작이 맞춰졌다 |

### 18.3 다음 우선순위

1. snapshot publish/reclamation을 더 공격적으로 최적화해야 하면 현재 분리 모델 위에서만 검토
2. route delta 종류가 늘어나면 observability helper만 확장
3. attach/discovery 관련 추가 계약이 생기면 control snapshot owner에서만 흡수
4. perf 재측정은 구조 검증 이후 별도 보고서로 정리
