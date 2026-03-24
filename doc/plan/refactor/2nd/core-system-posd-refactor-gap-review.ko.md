# POSD 2차 리팩토링 갭 리뷰

> 상태: active
> 기준 문서:
> - `doc/plan/refactor/2nd/core-system-posd-refactor-master-plan.ko.md`
> - `doc/plan/refactor/2nd/core-system-posd-refactor-remaining-execution-guide.ko.md`
> 기준 시점: 2026-03-24 현재 워크트리 재리뷰
> 대상 범위: `core/`, `core/tests/`
> 목적: 이미 반영된 2차 리팩토링의 성과와 잔여 구조 갭을 현재 코드 기준으로 다시 고정

## 1. 문서 목적

이 문서는 2차 POSD 리팩토링이 실제로 어디까지 반영되었는지,
그리고 어떤 핵심 허브가 아직 남아 있는지를 현재 코드 기준으로 재판정한다.

이 문서는 마스터 플랜을 대체하지 않는다.
또한 기존 실행 문서의 과거 `완료` 판정을 무효화하기 위한 문서도 아니다.

이 문서의 역할은 아래 두 가지다.

- 이미 먹힌 구조 변화와 아직 미완료인 경계를 분리해서 다시 고정한다.
- 이후 실행 문서가 perf 최종 마감 전에 어떤 구조 갭을 먼저 닫아야 하는지 우선순위를 제공한다.

핵심 해석은 아래 한 줄로 고정한다.

```text
이번 리팩토링은 의미 있는 진전을 만들었지만,
socket/context 허브를 deep module로 바꾸는 마지막 핵심 단계는 아직 남아 있다.
```

## 2. 재평가 요약

현재 코드는 "리팩토링 전 상태"가 아니다.
실제로 아래 항목은 분명히 진전됐다.

- `api/` 관심사 분리
- `logical multipart send` 공통 모듈 도입
- service-local access seam 도입
- `service_runtime_base_t` / `socket_close_ops_t` / `ctx_t` close-wait 분업
- `gateway`, `discovery`, `spot` 일부 deep module 분해

반면 아래 항목은 마스터 플랜 목표 대비 아직 미완료다.

- `socket_base_t` semantic/runtime 분리의 마지막 단계
- `ctx_t`의 startup/shutdown/resource orchestration 축소
- 일부 대형 서비스 파일의 ownership 재정의 완결
- 구조 갭을 닫은 뒤의 perf 최종 baseline 회복 확정

즉 현재 평결은 아래처럼 고정한다.

```text
Phase 1, 3, 5는 상당 부분 반영되었다.
Phase 2와 ctx 중심 runtime deep-module화는 부분 달성에 머물러 있다.
perf 최종 마감은 이 잔여 구조 갭을 닫은 뒤 수행해야 한다.
```

## 3. 달성된 항목

### 3.1 API facade 분해는 실제로 진행됐다

- `context_api.cpp`, `socket_api.cpp`, `socket_message_api.cpp`, `service_*_api.cpp`가 존재한다.
- `core/src/api/zlink.cpp`가 예전 단일 허브였던 상태에서 상당 부분 분산됐다.
- `logical multipart send`는 `core/src/core/multipart_send_txn.*`로 추출됐다.
- `gateway`와 `spot` high-level send/publish caller가 해당 공통 모듈을 사용한다.

해석:

- 마스터 플랜의 Phase 1 방향 자체는 맞았고, 코드에도 상당 부분 반영됐다.
- 따라서 현재 문제를 "리팩토링이 없었다"로 해석하면 안 된다.

### 3.2 service access / close-drain 분업도 실제로 생겼다

- `service_public_api_guard_t`와 `service_public_api_scope_t`가 common guard로 존재한다.
- `gateway_access.*`, `registry_query_access.*`, `spot_subject_access.*` 같은 service-local seam이 존재한다.
- `service_runtime_base_t`는 lifecycle / owned socket tracking을 맡고,
  `socket_close_ops_t`는 actual close/wait helper contract를 맡는다.
- `ctx_t`는 global socket removal wait owner 역할을 유지한다.

해석:

- 마스터 플랜의 Phase 3, 5는 구조적으로 먹혔다.
- service-owned ownership과 global removal ownership을 억지로 합치지 않은 방향도 맞다.

## 4. 부분 달성 항목

### 4.1 `api/zlink.cpp`는 얇아졌지만 완전히 thin facade는 아니다

현재 `zlink.cpp`는 socket creation policy, close sequencing, monitor handler state,
poller fallback 같은 오케스트레이션을 여전히 꽤 많이 가진다.

평가:

- `완료`보다는 `부분 달성`
- 다시 예전 수준의 mega-hub는 아니지만,
  마스터 플랜이 말한 "public entry aggregation file" 수준까지도 아직 남아 있다.

### 4.2 서비스 대형 파일은 분해됐지만 ownership 완결은 아니다

특히 아래 파일군은 아직 크기 자체보다 책임 응집 관점에서 추가 정리가 필요하다.

- `core/src/services/discovery/registry.cpp`
- `core/src/services/spot/spot_subject_access.cpp`
- `core/src/services/spot/spot_data_plane.cpp`

평가:

- helper 분리는 진행됐지만,
  lifecycle / topology wiring / protocol/data path owner가 문장 하나로 바로 설명되는 수준까지는 덜 갔다.

## 5. 미완료 핵심 갭

### 5.1 `socket_base_t`는 아직 semantic facade로 충분히 줄지 않았다

현재 `socket_base_t`는 여전히 아래를 같이 가진다.

- public API admission
- callback depth / deferred close
- send-ready sequencing
- monitor queue/thread
- endpoint bookkeeping
- async mailbox quiesce

또한 `socket_runtime_t`로 묶인 상태를 다시 `socket_base_t`가 다수의 참조 멤버로 풀어
직접 제어한다.

평가:

- 마스터 플랜의 Phase 2는 `완료`가 아니라 `부분 달성`
- 문서가 경고한 "`socket_runtime_t` mega-class 재생산" 위험이 아직 남아 있다

### 5.2 `ctx_t`는 아직 too-much-orchestrator다

현재 `ctx_t`는 아래를 함께 안다.

- lazy start
- reaper/io thread 생성
- service control runtime 부팅
- terminate/shutdown sequencing
- socket slot bookkeeping
- pending inproc 연결 정리

평가:

- `ctx_t`는 runtime deep module 후보이긴 하지만,
  지금은 resource registry와 startup/shutdown coordinator가 아직 과도하게 응집돼 있다.
- perf 최종 마감 전에 이 축을 줄이지 않으면 이후 구조 변경이 다시 `ctx_t`로 재집중될 가능성이 높다.

### 5.3 perf의 남은 owner는 구조 갭과 연결돼 있다

현재 perf 잔여 이슈는 단순 수치 미달이 아니라,
`spot` multi secure transport와 연계된 runtime/budget/backlog/phase-sequencing owner를
계속 건드리고 있다.

평가:

- perf는 별도 phase가 맞다.
- 다만 `socket_base_t`, `ctx_t`, `spot` runtime의 잔여 구조 갭이 남은 상태에서
  perf만 단독으로 끝내려 하면 hot path owner가 다시 흔들릴 가능성이 높다.

## 6. 실행 우선순위 재고정

실행 문서는 아래 순서로 다시 고정한다.

1. 이 갭 리뷰 문서를 authority로 추가한다.
2. `socket_base_t` residual split을 다시 연다.
3. `ctx_t` startup/shutdown/resource orchestration residual split을 연다.
4. `registry.cpp`, `spot_subject_access.cpp`, `spot_data_plane.cpp`의 ownership 완결을 진행한다.
5. 위 구조 갭을 닫은 뒤 perf smoke / targeted recheck / full baseline 비교를 다시 수행한다.
6. baseline 미달 tuple이 0개가 될 때까지 perf 회복 루프를 반복한다.

## 7. 실행 문서 반영 규칙

이 문서를 실행 문서에 반영할 때는 아래처럼 해석한다.

- 기존 `완료` 항목을 전부 뒤집지 않는다.
- 대신 `현재 코드 기준 재검토 필요` 항목을 별도 행으로 추가한다.
- 특히 `Phase 2`, `ctx/runtime residual`, `service residual`, `perf finalization`은
  새 행으로 다시 추적한다.
- perf는 마지막 행이 아니라,
  구조 residual 정리 뒤에 수행되는 최종 회복 단계로 고정한다.
- residual 항목의 구현 판단은 별도 execution spec 문서로 더 고정한다.

## 8. 최종 평결

현재 `core`는 POSD 2차 리팩토링이 실패한 상태가 아니다.
오히려 많은 기반이 이미 생겼다.

하지만 아래 두 문장이 동시에 참이다.

- "리팩토링은 이미 크게 진행됐다."
- "지금도 추가 리팩토링이 필요하다."

그 이유는 남은 복잡도가 대체로 `socket_base_t`, `ctx_t`, 일부 `spot/discovery` 허브,
그리고 그 위에 얹힌 perf 잔여 owner로 집중되기 때문이다.

따라서 이후 실행의 목표는 새 방향을 찾는 것이 아니라,
이미 맞게 잡힌 방향을 마지막 구조 갭까지 닫고 perf 최종 확정까지 끝내는 것이다.
