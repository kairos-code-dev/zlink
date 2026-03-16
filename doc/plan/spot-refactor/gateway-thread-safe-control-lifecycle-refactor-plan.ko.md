# Gateway Thread-Safe / Control / Lifecycle 리팩터 계획

> 상태 메모
>
> - 이 문서는 2026-03-16 기준 `gateway` 구조 단순화와
>   thread-safe/control-path/lifecycle 정합성 강화를 위한 상세 계획이다.
> - 현재 `gateway`는 public API 계약과 기본 기능은 대체로 안정적이지만,
>   control-path, monitor path, discovery attach, destroy lifecycle이
>   같은 객체 내부에서 강하게 얽혀 있다.
> - 현재 목표는 public API를 바꾸지 않고,
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

- public `gateway` API 시그니처 변경
- `gateway`를 multi-socket peer control architecture로 바꾸는 작업
- discovery / registry public contract 자체의 재설계
- perf 전용 shortcut 추가

### 2.1 이번 문서가 바꾸지 않는 것

이번 리팩터는 아래를 바꾸지 않는다.

- public `gateway` API / ABI
- thread-safe public contract의 의미
- lifecycle strict errno contract의 의미
- single router 중심 transport topology
- discovery / registry의 공개 개념 모델

즉 이 문서는 `gateway` 외형을 바꾸는 문서가 아니라,
현재 public surface를 더 작은 내부 구조 위에 다시 올리는 문서다.

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

즉 이번 리팩터는 `gateway` 공개 API를 다시 설계하는 작업이 아니라,
현재 API를 더 작은 내부 구조 위에 다시 올리는 작업이다.

### 3.3 현재 코드에 이미 반영된 점과 아직 목표만 있는 점

현재 코드 기준으로 이미 반영된 점:

- single handle public surface와 single router 기반 steady-state send path
- lifecycle strict errno 규칙의 기본 골격
- same-handle runtime read, send-ready self-close, 일부 attach/query ordering 회귀
- discovery attach 이후 refresh 기반 route 반영 모델

아직 목표만 있고 구조적으로 완전히 분리되지 않은 점:

- send snapshot과 control/topology snapshot의 명시적 이원화
- attach / refresh / detach의 독립 상태 전이
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
| 추가 socket | transport-level 추가 socket 설계는 완료, internal lifecycle 분리는 미완료 | 없음 (현재 구조 유지) |
| hidden child 문제 | 있음 — `ensure_default_sub()`가 public sub를 암묵 생성 | 없음 |
| destroy 중복 | data plane / default sub / node의 3-way close | discovery detach / monitor / socket close가 한 메서드에 혼합 |
| readiness source | peer control protocol 기반이지만 bookkeeping 분산 | send-ready callback + connection count 기반 |

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

**selection cursor 분리**: send snapshot 자체는 immutable이므로,
RR/weighted selection에 사용되는 `rr_index` 같은 cursor 상태는
snapshot 내부에 둘 수 없다. cursor는 send path 전용 atomic 상태이거나
`_send_sync` 보호 하에 별도 per-strategy state로 분리한다.
snapshot은 provider 목록·weight·routing_id만 담고,
cursor 증가는 snapshot 교체와 독립적으로 진행된다.

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

이 구조로 바꾸면
route churn과 event fanout 비용을 별도로 다룰 수 있고,
report side-effect가 hot path에 간접 영향을 주는 것을 방지한다.

### 8.4 destroy 단계 정규화

destroy는 아래 순서로 고정한다.

1. close admission 시작
2. busy children / callback inflight 검사
3. discovery/control detach
4. monitor terminal close
5. internal socket close
6. tracked drain wait

각 단계는 실패 의미가 달라야 하며,
같은 리소스를 여러 단계가 중복 close하지 않게 해야 한다.

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

### 9.2 2단계: send snapshot 분리

`gateway_service_pool_t` 또는 동등한 구조를
hot-path snapshot과 control snapshot으로 분리한다.

완료 기준:

- `send()` / `send_rid()`가 읽는 자료구조가 control metadata 없이 설명된다.
- discovery churn과 weight update가 send path와 분리된 refresh 경로를 가진다.

### 9.3 3단계: attach / refresh / detach 전이 분리

attach, refresh, detach를 서로 다른 전이로 나눈다.

현재 `refresh_pool()`은 ~200줄에 달하는 메서드로,
monitor drain, provider source merge, connect admission, readiness 확인,
stale endpoint 정리, snapshot commit, peer report 갱신을 함께 수행한다.
리팩터 후에는 최소한 다음 단계로 분해한다.

1. provider source resolve (discovery + manual merge)
2. connection admission / handover gate 판단
3. send snapshot build (hot path에 publish할 immutable snapshot)
4. control snapshot / report diff 적용

완료 기준:

- `attach_discovery()` ordering을 별도 상태도 없이 문장으로 설명할 수 있다.
- discovery destroyed callback이 send path를 직접 오염시키지 않는다.
- `refresh_pool()`이 위 4단계로 분해되어 각 단계를 독립적으로 설명할 수 있다.

### 9.4 4단계: monitor pipeline 분리

route up/down, send-ready changed, service ready/lost, closed/error를
직접 emit하지 말고
정규화된 internal event를 거쳐 fanout하도록 바꾼다.

완료 기준:

- monitor event emission이 route mutation 코드에서 분리된다.
- `monitor_service_contract`와 gateway monitor 회귀가
  더 단순한 state change 설명으로 통과한다.

### 9.5 5단계: destroy 선형화 정리

destroy를 단계화하고, 각 단계의 errno 의미를 고정한다.

완료 기준:

- callback self-close, monitor child open, destroy busy/no-latch 규칙을
  코드에서 직접 읽을 수 있다.
- `gateway` destroy가 “한 메서드에서 여러 cleanup을 순차 호출하는 코드”가 아니라
  의도된 단계 전이처럼 보인다.

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

위 네 조건을 동시에 만족하지 못하면
구조 변경보다 증상 이동일 가능성이 크다.

## 12. 수용 기준

### 12.1 기능 수용 기준

- `gateway` steady-state send path가 control metadata 없이 설명 가능하다.
- discovery attach/detach/refresh ordering이 문장과 테스트로 일치한다.
- monitor fanout이 route mutation의 직접 부산물이 아니라 별도 pipeline으로 보인다.
- destroy lifecycle이 단계별로 나뉘고,
  busy/no-latch/terminal event 규칙이 선형화돼 있다.
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
