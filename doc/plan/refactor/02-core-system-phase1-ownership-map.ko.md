# `[02]` `core` 시스템 리팩토링 Phase 1 Ownership Map

> 상태: draft
> 목적: lifecycle / destroy / close ownership 단일화

| nav | link |
| --- | --- |
| 목록 | [README](README.ko.md) |
| 이전 | [01 Phase 0 Baseline](01-core-system-phase0-baseline.ko.md) |
| 다음 | [03 Phase 1 Resource Inventory](03-core-system-phase1-resource-inventory.ko.md) |
| 관련 | [00 상위 계획](00-core-system-posd-refactor-plan.ko.md) |
| thread-safe 규약 | [thread-safe-socket-plan](../thread-safe/thread-safe-socket-plan.ko.md) — lifecycle strict 계층 (close/destroy fail-fast). 현재 구현 수준을 유지한다. |

## 1. 목적

Phase 1의 목적은 `core` 전체 리팩토링에서
가장 먼저 lifecycle ownership을 고정하는 것이다.

이 단계에서 해결하려는 문제는 다음과 같다.

- 같은 리소스를 여러 주체가 닫을 수 있다.
- destroy와 close, stop과 detach, node와 child ownership 경계가 섞여 있다.
- service runtime과 socket runtime, reaper 경계가 하나의 설명으로 이어지지 않는다.

POSD 관점에서 이것은 단순한 버그 집합이 아니라
정보 누출(숨겨야 할 내부 구현이 외부에 드러남)과
temporal decomposition(실행 순서 기준으로 모듈을 나누는 안티패턴)의 결합이다.

용어 주의: 이 문서에서 "socket runtime"은 현재 코드에서
socket 내부가 이미 수행하는 close/destroy/quiesce(보류 작업을 완료하고 안전하게 멈추는 절차) 메커니즘을 가리킨다.
Phase 2에서 도입하는 "socket runtime"은 이 범위를 확장하여
공통 mechanism 계층으로 통합하는 새로운 구조물이다.

## 2. 우선 분석 대상 파일

### 2.1 socket / core lifecycle

- [socket_base.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.hpp)
- [socket_base.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp)
- [own.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/own.hpp)
- [own.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/own.cpp)
- [reaper.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/reaper.hpp)
- [reaper.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/reaper.cpp)

### 2.2 service lifecycle

- [service_runtime_base.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/common/service_runtime_base.hpp)
- [service_control_runtime.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/control/service_control_runtime.hpp)
- [gateway_runtime.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/gateway/gateway_runtime.hpp)
- [spot_runtime.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_runtime.hpp)
- [zlink.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/api/zlink.cpp)

## 3. 현재 ownership 냄새

현재 코드와 테스트 진입점을 기준으로 보면
아래 냄새가 이미 드러난다.

AS-IS 전체 ownership 흐름:

```text
AS-IS: destroy 경로에 여러 주체가 관여

  API layer (zlink_*_destroy)
      |
      |  destroy 시작
      v
  service_runtime_base_t
      |  close_socket() — coordinator이면서 closer
      |  close_socket_and_wait() — ctx contract에 직접 결합
      |  wait_drained() — sleep 기반 polling
      v
  socket_base_t
      |  process_destroy() → _destroyed = true
      |  check_destroy() → mailbox quiesce 확인
      |  finalize_destroy() → destroy_socket + send_reaped
      v
  reaper_t
      |  final close + free
      v
  done

  문제: 3개 계층이 각자 close 역할을 가짐
        "누가 authoritative close owner?"에 단일 답이 없음
```

### 3.1 destroy owner가 한 단계에 고정되어 있지 않다

예:

- API entry에서 destroy 호출
- service runtime에서 close / wait 처리
- socket 내부에서 reaper 등록 및 최종 destroy 처리

이 구조는 "누가 authoritative close owner인가"를 흐린다.

### 3.2 service runtime이 lifecycle coordinator이면서 socket closer이기도 하다

[service_runtime_base.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/common/service_runtime_base.hpp)는 현재 다음을 함께 가진다.

- state machine
- owned socket registry
- closing socket registry
- close / close_and_wait
- drain 대기

즉 lifecycle state와 concrete socket close mechanics가 같은 타입에 묶여 있다.

### 3.3 `wait_drained` 구현이 POSD와 테스트 정책 양쪽에서 냄새가 있다

현재 `wait_drained` / `force_wait_remaining` 경로는 내부적으로
짧은 sleep 기반 진행을 사용한다.

이것은 다음 두 문제를 만든다.

- 구조 설명이 "누가 상태를 소유하는가"보다 "조금 기다리면 사라진다"에 기대게 된다.
- fail-fast(오류 발생 시 즉시 실패 보고) / deterministic synchronization(명시적 동기화 수단으로 완료를 확인) 정책과 긴장 관계를 만든다.

이 부분은 테스트 코드가 아니라 runtime 코드이지만,
구조 리팩토링에서는 같은 문제로 본다.

### 3.4 service별 ownership 모델이 서로 다르게 읽힌다

- `gateway_runtime_t`는 `service_runtime_base_t lifecycle` 위에 router/control 상태를 얹는다.
- `spot_runtime_t`는 독자적인 attachment/control/data-plane/socket 집합을 직접 가진다.

즉 `gateway`와 `spot`이 같은 서비스 계열인데도
shutdown 설명이 같은 문장으로 이어지지 않는다.

## 4. Phase 1의 목표 상태

TO-BE 전체 ownership 흐름:

```text
TO-BE: 각 계층의 역할이 단일화

  API layer (zlink_*_destroy)
      |
      |  orchestration entry (진입만, close 세부 모름)
      v
  service runtime
      |  lifecycle coordinator (정지 절차 시작, registry detach)
      |  concrete close는 하지 않음
      v
  socket runtime
      |  concrete close owner (실제 close mechanics 실행)
      |  drain 확인
      v
  reaper
      |  finalization executor (final free만 수행)
      |  정책 판단 하지 않음
      v
  done

  핵심: 각 리소스에 대해 close owner가 정확히 하나
```

```text
AS-IS vs TO-BE 역할 비교

  주체                  AS-IS 역할                   TO-BE 역할
  ────────────────────  ─────────────────────────    ─────────────────────
  API layer             destroy 호출 + 일부 fallback → orchestration entry만
  service runtime       coordinator + closer 겸임    → coordinator만
  socket runtime        close + final destroy        → concrete close owner
  reaper                finalization + 정책 혼동      → finalization executor만
```

Phase 1 완료 후에는 아래 질문에 한 문장으로 답할 수 있어야 한다.

1. 이 리소스는 누가 만든다?
2. 누가 정상 종료를 시작한다?
3. 누가 실제 close를 수행한다?
4. 누가 drain 완료를 확인한다?
5. 누가 최종 메모리 파괴를 수행한다?

답이 두 주체 이상이면 아직 구조가 정리되지 않은 것이다.

## 5. ownership 원칙

요약 결정:

- service runtime은 lifecycle coordinator다.
- socket runtime은 concrete close owner다.
- reaper는 finalization executor다.
- API destroy는 orchestration entry다.

### 5.1 same resource, single close owner

같은 리소스에 대해 close를 호출할 수 있는 주체는 정확히 하나여야 한다.

### 5.2 lifecycle coordinator와 concrete closer를 분리한다

service는 "정지 절차를 시작하는 주체"일 수는 있지만,
실제 socket close mechanics의 세부까지 같이 갖지 않는 방향이 맞다.

### 5.3 API destroy는 orchestration entry여야 한다

API layer는 destroy orchestration의 진입점만 가져야 하며,
실제 close 순서의 세부를 다 알면 안 된다.

### 5.4 reaper는 finalization executor여야지 정책 owner가 아니어야 한다

reaper는 final close/final free의 하위 실행자여야 한다.
상위 lifecycle 정책이 reaper의 내부 흐름에 기대어 설명되면 안 된다.

### 5.5 오류 가능성을 구조로 제거한다

POSD의 "define errors out of existence" 원칙:
오류를 런타임에 검사하는 것이 아니라,
API나 타입 설계로 오류 자체가 발생할 수 없게 만든다.

"이 리소스는 A만 닫아야 한다"를 문서에 적는 것과
타입 시스템이나 구조가 다른 주체의 close를 불가능하게 만드는 것은 다르다.

```text
정책 기반:  "close owner는 socket runtime" — 문서/주석에 명시
            → 다른 코드가 직접 close를 호출해도 컴파일됨

구조 기반:  close 가능 핸들을 socket runtime만 보유
            → 다른 코드에서 close 호출 자체가 불가능
```

Phase 1에서 모든 리소스에 구조적 보장을 강제할 수는 없지만,
ownership 중복이 가장 문제되는 리소스에는
정책이 아니라 구조로 단일 owner를 보장하는 방향을 우선 검토한다.

우선 검토 대상:

- spot attachment socket — 현재 child/node/runtime 3자가 close 가능
- monitor child handle — parent와 child 양쪽에서 close 가능

## 6. 1차 ownership 표

아래 표는 현재 구조를 바로 고치기 위한 1차 목표 ownership 표다.
이 표는 implementation detail이 아니라 구조 목표다.

**규칙: 각 칸에 단일 주체만 적는다. 복수 주체(`또는`, `/`, `+`) 금지.**

| 리소스 | authoritative close owner | shutdown initiator | executor | 근거 |
| --- | --- | --- | --- | --- |
| 일반 socket | socket runtime | service runtime | reaper (finalization) | close mechanics는 socket runtime이 가짐 |
| service owned socket registry | service runtime | service runtime | service runtime | registry detach만 수행, concrete close는 socket runtime |
| socket 내부 fd/transport binding | socket runtime | socket runtime | socket runtime | mechanism owner가 직접 close |
| gateway router socket | socket runtime | gateway runtime | reaper (finalization) | service runtime은 coordinator, close는 socket runtime |
| spot attachment socket | socket runtime | spot runtime | reaper (finalization) | attachment runtime이 아니라 socket runtime이 close owner |
| monitor child handle | child handle | parent runtime (detach) | child handle | parent는 detach만, child가 자기 자원 close |
| ctx tracked socket entry | ctx | ctx | ctx | ctx shutdown 경로에서 단일 owner |

**역할 정의:**

- **authoritative close owner** — 이 리소스의 concrete close를 실행할 유일한 주체
- **shutdown initiator** — close를 시작하도록 trigger하는 주체 (owner와 다를 수 있음)
- **executor** — 최종 물리적 해제를 수행하는 주체

**finalization 체인 해석:**

socket의 경우 finalization은 단일 행위가 아니라 2단계 체인이다.

1. `socket_base_t::finalize_destroy()` — socket-level cleanup 수행
   (`destroy_socket` + `send_reaped`). 이 단계의 owner는 socket runtime.
2. `reaper_t` — reaped 통지를 받아 최종 memory free 수행.
   이 단계의 owner는 reaper (finalization executor).

따라서 위 표에서 executor = reaper는 **최종 memory free** 기준이고,
[03] inventory에서 `socket_base_t`를 final destroy owner라고 적은 것은
**socket-level cleanup** 기준이다. 두 문서는 같은 체인의 다른 단계를 가리킨다.

핵심은 다음이다.

- service runtime은 shutdown initiator(coordinator)이지,
  authoritative close owner가 되어서는 안 된다.
- socket runtime이 모든 socket의 authoritative close owner다.
- reaper는 정책 owner가 아니라 finalization executor로 제한한다.
- 표에 복수 주체가 적히면 ownership 결정이 미완이므로 구현에 들어가지 않는다.

## 7. 서비스별 적용 방향

### 7.1 `gateway`

목표:

- `gateway_runtime_t`의 lifecycle 설명이 공통 service lifecycle 위에서 읽히게 정리
- router socket, monitor socket, pool state를 분리

우선 작업:

- `lifecycle`이 실제로 소유하는 리소스와 단순 추적만 하는 리소스 구분
- monitor child/observer 경계 명확화
- refresh task와 socket close 순서 명시

### 7.2 `spot`

목표:

- attachment, control socket, data plane socket의 close owner를 나눠서 명시
- node destroy와 child destroy의 경계를 단일 문장으로 설명 가능하게 정리

우선 작업:

- `attachments`
- `data_ctrl_front/back`
- `mesh_pub`, `mesh_xsub`
- `peer_ctrl_pub`, `peer_ctrl_sub`
- `local_pub_ingress_sub`, `local_fanout_xpub`

각 리소스마다 create/start/stop/close/finalize owner를 명시한다.

## 8. 코드 리팩토링 체크리스트

Phase 1 구현에 들어가면 아래 순서로 본다.

1. resource inventory 작성
2. 각 리소스의 authoritative owner 확정
3. close 중복 가능 경로 제거
4. wait/drain 경로를 deterministic contract 기준으로 재설계
5. API destroy가 orchestration entry만 하도록 축소
6. 죽은 코드 및 불필요한 파일 정리 (호출처 없는 함수, 미사용 헤더, 레거시 shim 등)
7. 관련 integration/e2e/thread-safe 케이스로 회귀 검증

## 9. 테스트 및 검증 포인트

기본:

- `./core/tests/run_test_lanes.sh`

필수 추가 확인 대상:

- service destroy busy/monitor child 관련 integration 케이스
- `spot` child destroy order 관련 케이스
- thread-safe contract lane
- `gateway` / `spot` perf cleanup 경로

관련 근거:

- `core/tests/CMakeLists.txt`의 service destroy / monitor child / thread-safe 항목
- `core/perf/single/src/perf_spot.cpp`
- `core/perf/multi/src/perf_multi_spot_client.cpp`
- `core/perf/multi/src/perf_multi_gateway_client.cpp`

## 10. Phase 1 완료 조건

아래가 모두 충족되면 Phase 1 완료로 본다.

- 핵심 리소스마다 single close owner가 명시된다.
- ownership 표에 복수 주체(`또는`, `/`, `+`)가 없다.
- service runtime과 socket runtime의 경계가 이전보다 명확해진다.
- `gateway`와 `spot`의 destroy 설명이 공통 구조 언어로 이어진다.
- drain/wait 경로에서 sleep 기반 polling이 deterministic synchronization으로 교체되었거나,
  sleep이 남아 있다면 해당 경로와 사유가 문서화되어 있다.
- destroy 관련 integration/e2e/thread-safe 회귀가 없다.
- perf cleanup 경로가 baseline 대비 의미 있게 후퇴하지 않는다.

## 11. 다음 단계

Phase 1이 정리되면 다음은 Phase 2 socket runtime 분리다.

참조 문서:

- `doc/perf/refactor/04-core-system-phase2-socket-runtime-split.ko.md`

Phase 2 forward reference:

Phase 1에서 ownership을 정리할 때,
Phase 2에서 도입할 socket runtime 계층의 최종 위치를 고려해야 한다.
Phase 1에서 close owner를 임시 위치에 놓고
Phase 2에서 다시 이동하면 throwaway work가 된다.

리소스별 구체 배치 지침:

```text
리소스                       Phase 1에서 배치할 위치            이유
─────────────────────────  ──────────────────────────────    ────────────────────
endpoint attach/detach     socket_base_t 내부에 유지          Phase 2에서 socket runtime으로 이동 예정.
                                                              Phase 1에서 service로 올리면 다시 내려야 함.

close/quiesce mechanics    socket_base_t 내부에 유지          Phase 2 socket runtime의 핵심 후보.
                                                              service_runtime에서 빼되, 아래로 내리기만 함.

service socket registry    service_runtime에서 유지            Phase 2에서도 service가 registry를 가짐.
                           (단, close 실행은 분리)             저장 위치는 변하지 않으므로 이동 불필요.

spot attachment close      spot_runtime 내부로 이동            Phase 2에서 attachment runtime 후보.
                                                              service_runtime_base에 놓으면 다시 분리해야 함.

monitor child close        child handle 내부로 이동            Phase 2에서도 child가 자기 자원을 닫는 구조.
                                                              parent에서 빼는 것이 최종 목적지와 일치.
```

이 표를 참조하면 Phase 1의 배치가 Phase 2에서 재이동 없이 연결된다.

그 전에는 아래를 하지 않는다.

- 대규모 디렉터리 이동
- facade 이름만 바꾸는 rename 위주 작업
- engine pipeline 대수술

ownership이 정리되지 않은 상태에서 이 작업들을 먼저 하면
change amplification만 더 커진다.
