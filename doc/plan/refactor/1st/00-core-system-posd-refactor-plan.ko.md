# `[00]` POSD 기반 `core` 전체 구조 리팩토링 계획

> 상태: draft
> 대상: `core/`
> 기준 원칙: POSD + 성능 비퇴행
> 관련 문서:
> - `doc/plan/posd-core-src-refactor-plan.ko.md`
> - `doc/internals/architecture.ko.md`
> - `doc/perf/PERF_POLICY.md`
> - `doc/perf/PERF_SINGLE_TEST_POLICY.md`
> - `doc/perf/PERF_MULTI_TEST_POLICY.md`
> - `doc/plan/thread-safe/thread-safe-socket-plan.ko.md` — thread-safe 규약 (현재 구현 수준 유지)

| nav | link |
| --- | --- |
| 목록 | [README](README.ko.md) |
| 다음 | [01 Phase 0 Baseline](01-core-system-phase0-baseline.ko.md) |

## 1. 문서 목적

이 문서는 `core` 전체 시스템을 John Ousterhout의
*A Philosophy of Software Design* 관점으로 다시 정리하기 위한
상위 리팩토링 계획이다.

이번 계획의 핵심 목표는 단순한 디렉터리 재배치가 아니다.

- `core/src`의 구조 복잡도를 낮춘다.
- 변경 증폭을 줄인다.
- 수명주기 ownership을 명확히 한다.
- data/control/lifecycle/runtime 책임을 깊은 모듈 안으로 내린다.
- 성능 hot path는 유지하거나 개선한다.

즉 이번 리팩토링의 성공 기준은 아래 한 줄이다.

```text
"더 적은 지식을 알아도 안전하게 바꿀 수 있는 구조"로 만들되,
single/multi perf 기준은 후퇴시키지 않는다.
```

## 2. 우선순위

이 계획에서 우선순위는 고정한다.

1. 성능 비퇴행
2. 복잡도 감소
3. 설명 가능한 ownership / deep module
4. 파일/디렉터리 미관

구조가 더 좋아 보여도 아래 중 하나가 발생하면 실패로 본다.

- steady-state(초기 연결 이후 정상 운영 상태) throughput 하락
- tail latency 상승
- CPU 사용률 상승
- hot path(성능에 가장 큰 영향을 주는 자주 실행되는 코드 경로) alloc/copy/branch 증가
- callback churn(콜백 등록/해제가 과도하게 반복되는 현상) 증가
- transport/protocol/socket별 특수 케이스 확산
- thread-safe 계약 수준 후퇴 — 현재 보장하는 3계층 계약(hot path concurrent,
  control path serialized, lifecycle strict)을 깨뜨리는 구조 변경.
  규약 문서: [`thread-safe-socket-plan.ko.md`](../thread-safe/thread-safe-socket-plan.ko.md)

반대로 local micro-optimization 하나가 빨라져도
전체 구조 설명이 더 어려워지면 채택하지 않는다.

## 3. 현재 구조의 핵심 문제

현재 `core`는 이미 계층이 나뉘어 있지만,
POSD 기준에서는 아직 "깊은 모듈"보다
"큰 허브 객체 + 분산된 세부 정책" 조합이 강하다.

현재 문제를 요약하면 다음과 같다.

### 3.1 허브 타입이 너무 많은 책임을 가진다

- `socket_base_t`가 socket API, endpoint lifecycle, poll 연동,
  pipe 이벤트, monitor, peer bookkeeping까지 함께 가진다.
- `asio_engine_t`가 handshake, timer, buffering, speculative I/O,
  write policy, completion 처리까지 동시에 가진다.
- service runtime이 public 의미와 internal socket topology를 함께 드러낸다.

결과적으로 변경 하나가 여러 허브를 동시에 건드리게 된다.

### 3.2 data/control/lifecycle가 시간 순서 기준으로 섞여 있다

현재 구조는 정보 소유 기준보다 실행 순서 기준으로 나뉜 영역이 많다.

- transport connect/listen/handshake가 여러 타입에 퍼져 있다.
- service readiness/monitoring이 socket 내부 이벤트와 느슨하게 연결된다.
- shutdown/drain/close ownership이 runtime, service, socket, reaper로 분산된다.

이 구조는 POSD가 경계해야 하는 temporal decomposition
(정보 소유 기준이 아니라 실행 순서 기준으로 모듈을 나누는 안티패턴) 냄새가 강하다.

### 3.3 성능 정책이 모듈 경계 대신 구현 세부에 묻혀 있다

현재 성능 최적화는 중요한 자산이지만,
그 위치가 deep module 내부 정책으로 정리되어 있지 않은 부분이 있다.

- speculative I/O
- gather write
- buffer growth
- zero-copy / low-copy 경로
- stream/ws/wss transport별 fast path

이 정책이 상위 구조와 함께 섞이면
"왜 빠른지"와 "어디를 바꿔도 되는지"가 함께 흐려진다.

### 3.4 공개 API가 내부 구조를 너무 많이 드러낸다

일부 서비스는 이미 의미 중심 API를 제공하지만,
여전히 내부 socket 역할이나 내부 연결 구조를 상위에서 알아야 하는 경로가 남아 있다.

이것은 information hiding(정보 은닉 — 모듈이 내부 구현을 외부에 숨기는 원칙) 부족이고,
새 기능 추가 시 change amplification(변경 증폭 — 한 기능 변경이 여러 파일을 건드리는 현상)을 만든다.

POSD에서 information leakage(정보 유출 — 숨겨야 할 내부 구현이 외부 인터페이스에 드러나는 것)는
보통 두 가지 원인이 있다.

1. 설계 시점에 추상화 수준을 잘못 잡은 경우
   — 처음부터 내부 구조가 인터페이스에 반영됨
2. 점진적 기능 추가로 인터페이스가 팽창한 경우
   — 원래는 숨겨져 있었으나 새 요구사항마다 내부를 노출

현재 `core`는 주로 2번에 해당한다.
초기 설계는 합리적이었으나, 기능이 추가되면서
service가 internal socket role을 상위에 점진적으로 노출하게 되었다.

이 판단이 중요한 이유:
1번이면 인터페이스부터 재설계해야 하고,
2번이면 내부 재구성이 주된 작업이되 필요 시 API도 함께 재설계한다.
현재 `core`는 주로 2번이므로 내부 구조 정리가 핵심이지만,
API가 이미 internal concept을 노출하는 부분은 호환성 없이 재설계한다.

### 3.5 복잡도는 점진적으로 쌓였다

POSD는 복잡도가 한 번에 오지 않고 점진적으로 쌓인다고 경고한다.
(complexity is incremental — 작은 변경 하나하나는 합리적이지만 누적되면 구조를 무너뜨린다)

현재 `core`의 복잡도는 다음 패턴으로 축적되었다.

- 새 transport 추가 시 engine에 분기가 하나씩 늘었다.
- 새 service 추가 시 service runtime에 특수 경로가 하나씩 늘었다.
- 성능 최적화 시 hot path에 조건부 정책이 하나씩 끼어들었다.

각각은 합리적인 변경이었지만, 누적되면서 허브 타입이 커졌다.

리팩토링 후에도 같은 패턴이 반복되면 복잡도가 다시 쌓인다.
따라서 리팩토링의 목표는 단순히 현재 복잡도를 낮추는 것이 아니라,
**같은 종류의 기능 추가가 허브 타입을 건드리지 않는 구조**를 만드는 것이다.

## 4. AS-IS / TO-BE 구조 비교

### 4.1 AS-IS: 현재 구조

현재 `core`는 허브 타입 중심으로 연결되어 있다.
각 허브가 많은 책임을 직접 가지고 있어서
변경 하나가 여러 허브를 동시에 건드린다.

```text
AS-IS

  zlink.cpp (API hub)
      |
      +---> socket_base_t (hub: API + lifecycle + pipe + monitor
      |         + peer + dispatch + reaper 연동)
      |             |
      |             +---> own_t / reaper_t
      |             +---> asio_engine_t (hub: handshake + timer
      |                       + buffer + speculative I/O
      |                       + gather write + completion)
      |                       |
      |                       +---> tcp/ipc/ws/wss/tls (파일은 분리,
      |                                 상위가 scheme 세부를 알아야 함)
      |
      +---> service_runtime_base_t (hub: state machine + socket registry
      |         + close mechanics + drain polling)
      |         |
      |         +---> gateway_runtime_t (lifecycle를 composition으로 보유)
      |         +---> spot_runtime_t (독자 topology + spot_node_t가 별도 lifecycle 보유)
      |
      문제: 허브 간 경계가 흐리고, 같은 리소스를 여러 곳에서 close 가능
```

### 4.2 TO-BE: 목표 구조

리팩토링 후 `core`는 허브 중심이 아니라
각 계층이 자기 아래 복잡성을 숨기는 deep module
(좁은 인터페이스 뒤에 많은 구현을 숨기는 모듈) 구조로 바뀐다.

```text
TO-BE

  public API / bindings ─── 얇은 facade(위임 전용 진입점), 내부 연결 구조 모름
        |
        v
  service facade ─────────── service 의미 API만 노출
        |                    (create/attach/destroy/monitor)
        v
  service runtime ────────── lifecycle state machine + readiness
        |                    (internal topology, socket 조합은 숨김)
        +---> socket semantic runtime ─── family별 의미만 담당
        |         |                       (PAIR/PUB/SUB/DEALER/ROUTER/STREAM)
        |         v
        |     socket runtime ──────────── 공통 mechanism 통합
        |         |                       (endpoint registry, peer state,
        |         |                        monitor bridge, dispatch bridge,
        |         |                        lifecycle quiesce)
        |         v
        |     core primitives ─────────── pipe, mailbox, own_t, reaper
        |
        +---> engine facade ──────────── connection start/stop/state
                  |                      (hot path 세부는 숨김)
                  v
              engine pipeline ─────────── speculative I/O, gather write,
                  |                       buffer growth, heartbeat
                  v
              transport adapter ───────── URI → endpoint open
                  |                       (TLS/WS/WSS layering은 내부)
                  v
              protocol codec ──────────── raw/zmp wire encoding
```

### 4.3 핵심 차이

```text
AS-IS                              TO-BE
─────────────────────────────────  ─────────────────────────────────
허브 타입이 많은 책임을 직접 보유  →  각 계층이 아래 복잡성을 숨김
socket_base_t가 7개 책임           →  family는 semantic만, runtime이 mechanism
asio_engine_t가 6개 책임           →  facade는 connection 의미, pipeline이 정책
service_runtime이 closer 겸임      →  coordinator만, close는 socket runtime
같은 리소스에 다중 close owner     →  리소스당 단일 close owner
transport 세부가 상위에 노출       →  adapter 뒤로 숨김
```

### 4.4 계층별 추상화 차이 검증

POSD의 "different layer, different abstraction"
(각 계층은 서로 다른 추상화를 제공해야 한다) 원칙:
각 계층이 단순 pass-through(받은 호출을 그대로 아래에 전달하기만 하는 것)가
아니라 **다른 추상화**를 제공해야 한다.

| 계층 | 상위에 제공하는 추상화 | pass-through가 아닌 이유 |
| --- | --- | --- |
| service facade | service 의미 (create/destroy) | 내부 socket 조합 구조를 숨김 |
| service runtime | lifecycle state + readiness | socket open/close 순서와 drain(보류 중인 작업 완료 대기)을 숨김 |
| socket semantic runtime | send/recv/bind/connect 의미 | family 간 routing/subscription 차이를 숨김 |
| socket runtime | 공통 mechanism contract | endpoint/peer/monitor/dispatch glue를 숨김 |
| engine facade | connection start/stop/state | handshake state machine과 timer를 숨김 |
| engine pipeline | async I/O completion contract | speculative I/O와 buffer 정책을 숨김 |
| transport adapter | endpoint open + channel | URI/address/scheme 선택을 숨김 |
| protocol codec | frame boundary | wire encoding/version을 숨김 |

service facade와 service runtime이 별개 계층인 이유:
facade는 "이 서비스가 무엇을 하는가"를 추상화하고,
runtime은 "이 서비스가 어떤 순서로 살고 죽는가"를 추상화한다.
둘의 관심사가 다르므로 별개 계층이다.

핵심은 "레이어를 더 늘린다"가 아니다.
핵심은 각 레이어가 아래 복잡성을 더 많이 숨기게 만드는 것이다.

## 5. 목표 깊은 모듈

이번 리팩토링에서 강화해야 할 깊은 모듈은 아래 여섯 개다.

### 5.1 socket semantic runtime

숨기는 것:

- endpoint attach/detach
- peer bookkeeping
- pipe/mailbox 연동
- monitor emission
- socket별 dispatch registration

드러내는 것:

- send/recv capability
- bind/connect/term 의미
- readiness / monitor hook 의미

### 5.2 lifecycle runtime

숨기는 것:

- open -> active -> draining -> close -> reap 순서
- reaper 연동
- linger / timeout / abortive fallback
- tracked socket ownership

드러내는 것:

- `start`
- `shutdown`
- `await_drained` 또는 동등 의미의 내부 계약

### 5.3 engine pipeline

숨기는 것:

- async read/write orchestration
- handshake 단계
- heartbeat / timer
- buffer strategy
- speculative read/write
- gather write

드러내는 것:

- ingress frame delivery
- egress frame submission
- connection state transitions

### 5.4 transport adapter

숨기는 것:

- URI parse
- connect/listen 전략
- socket 종류별 async primitive
- TLS/WS/WSS handshake 상세

드러내는 것:

- `client_endpoint`
- `server_endpoint`
- `async_transport_channel`

### 5.5 service runtime

숨기는 것:

- internal socket topology
- control socket 조합
- monitor bridge
- topology snapshot / registry interaction

드러내는 것:

- service 의미 API
- service 상태 / monitor 의미

### 5.6 perf contract module

숨기는 것:

- benchmark harness 세부
- 측정 phase 제어
- transport별 예외 실행 규칙

드러내는 것:

- 구조 변경 승인 기준
- hot path observability
- regression gate

이 모듈은 런타임 코드라기보다 리팩토링 governance 모듈이다.
하지만 이번 작업에서 반드시 필요하다.

### 5.7 인터페이스 축소 검증 기준

deep module이 실제로 "깊어졌는지" 판정하려면
인터페이스(상위가 알아야 할 것)가 줄어들었는지 확인해야 한다.

아래 표는 각 deep module 후보의 현재 public surface 추정과
목표 public surface를 비교한 것이다.

```text
모듈                      현재 상위 노출 항목 (추정)      목표 상위 노출 항목
────────────────────────  ────────────────────────────  ────────────────────────
socket semantic runtime   xsend/xrecv + endpoint ops     xsend/xrecv
                          + monitor glue + peer hooks     + bind/connect/term
                          + dispatch registration         + readiness hook
                          (~15개 이상)                    (~5~7개)

lifecycle runtime         state + close + wait            start + shutdown
                          + drain + force_wait            + await_drained
                          + socket registry ops           (~3개)
                          (~8개 이상)

engine pipeline           handshake + timer + buffer      ingress + egress
                          + speculative + gather          + state transition
                          + completion + state            (~3~4개)
                          (~10개 이상)

transport adapter         URI + address + connecter       client_endpoint
                          + listener + transport          + server_endpoint
                          + scheme-specific open          + async_channel
                          (~6개 이상)                     (~3개)

service runtime           state + socket ops + close      service 의미 API
                          + topology + monitor bridge     + state / monitor hook
                          (~10개 이상)                    (~4~5개)
```

이 표의 목표 수치는 설계 방향이지 정확한 사양이 아니다.
구현 시점에 실제 인터페이스를 정의하면서 이 추정과 비교한다.
목표보다 항목이 늘어나면 분리가 충분하지 않은 것이다.

## 6. 구조 개편 원칙

### 6.1 디렉터리보다 책임 경계를 먼저 바꾼다

파일 이동은 마지막에 한다.
먼저 ownership, invariant, state transition, perf policy를 정리한다.

### 6.2 얕은 helper 분해를 금지한다

큰 파일을 작은 파일 여러 개로 쪼개는 것만으로는 POSD 개선이 아니다.
새 타입은 아래 둘 중 하나일 때만 만든다.

- 상위가 알아야 할 개념 수를 줄일 때
- hot path 정책을 한곳에 가둘 때

### 6.3 특수 케이스를 상위로 올리지 않는다

- transport 예외
- socket family 예외
- monitor 예외
- thread-safe 예외
- perf 예외

이들은 가능하면 해당 deep module 내부에서 흡수한다.

### 6.4 죽은 코드와 불필요한 파일을 같이 정리한다

리팩토링 과정에서 아래에 해당하는 코드/파일은 발견 즉시 제거한다.

- **죽은 코드** — 어디서도 호출되지 않는 함수, 도달 불가능한 분기,
  `#if 0` / 주석 처리된 구현
- **사용되지 않는 파일** — include되지 않는 헤더, 빌드에 포함되지 않는 소스
- **중복 코드** — 같은 로직이 복사된 것. deep module로 통합하거나 하나만 남김
- **레거시 호환 코드** — 더 이상 지원하지 않는 경로를 위한 shim, wrapper, fallback
- **빈 추상화** — 내용 없이 위임만 하는 pass-through 클래스/함수

제거 기준:

- `rg`(ripgrep) / 빌드 시스템(`CMakeLists.txt` include 확인) / IDE reference 검색으로
  호출처가 없음이 확인된 것만 제거한다.
- 테스트 전용 헬퍼는 테스트에서만 사용되더라도 제거 대상이 아니다.
- 제거 시 해당 phase의 기능 게이트를 통과해야 한다.
- 제거된 항목은 commit 메시지에 명시하고, 큰 단위는 별도 commit으로 분리한다.

이 원칙의 목적은 구조 단순화와 죽은 코드 정리를 별도 작업으로 미루지 않고,
리팩토링의 자연스러운 일부로 처리하는 것이다.

### 6.5 perf-only 우회 경로를 만들지 않는다

성능 복구를 위해 public contract를 약하게 만들거나
benchmark 전용 shortcut을 넣는 것은 금지한다.

### 6.6 ownership 표가 코드보다 먼저 존재해야 한다

shutdown, close, detach, destroy, callback stop, handler revoke 같은 이벤트는
누가 authoritative owner인지 문서와 코드에서 동시에 명확해야 한다.

### 6.7 오류 가능성을 구조로 제거한다

POSD의 "define errors out of existence" 원칙:
오류를 런타임에 검사해서 잡는 것이 아니라,
타입 시스템이나 API 설계로 오류 자체가 발생할 수 없게 만든다.

ownership을 "정책으로 금지"하는 것과 "구조로 불가능하게 만드는 것"은 다르다.

```text
정책 기반:  "이 리소스는 A만 닫아야 한다" (문서에 적음, 코드는 위반 가능)
구조 기반:  close 권한이 타입에 묶여서 다른 주체가 호출 자체를 못 함
```

C++ 구현 방향 비교:

```text
방식              장점                           단점                           적합 대상
───────────────  ─────────────────────────────  ─────────────────────────────  ──────────────────
unique_ptr       소유권 이전이 명확             raw pointer 인터페이스와 혼재   attachment socket
+ move only      이중 free가 컴파일 에러          시 마이그레이션 비용 큼

close guard      close 후 핸들 무효화            guard 자체가 새 타입/상태 추가  monitor child
(sentinel)       이중 close가 no-op              null check 비용 (미미)

RAII wrapper     scope 벗어나면 자동 close       close 시점을 호출자가           단순 lifecycle
                 수동 close 호출 불가능           제어할 수 없음                  리소스
```

이 프로젝트에서의 적용 우선순위:

1. spot attachment socket → unique_ptr + move only 방향 검토
   - 현재 child/node/runtime 3자가 close 가능한 것이 핵심 문제
   - 소유권을 하나의 unique_ptr로 표현하면 구조적으로 단일 owner 보장
2. monitor child handle → close guard 방향 검토
   - parent/child 양쪽 close 가능성을 sentinel 패턴으로 차단
3. 나머지 리소스 → Phase 1에서 정책 기반으로 정리하되,
   Phase 2 socket runtime 도입 시 구조 기반으로 전환 가능성 열어둠

### 6.8 공통 base는 충분히 범용적이어야 한다

POSD의 "somewhat general-purpose" 원칙:
공통 base 타입은 특정 사용처에 맞춘 것이 아니라,
여러 사용처에 자연스럽게 적용될 만큼 범용적이어야 한다.

`service_runtime_base_t`가 공통 base로 유효하려면
이 기준을 충족해야 한다.

판단 질문:

- base가 모든 서비스에 실제로 적용되는 개념만 포함하는가?
  아니면 특정 서비스(gateway 또는 spot)의 패턴을 억지로 일반화한 것인가?
- gateway와 spot의 shutdown이 같은 문장으로 설명되지 않는 이유가
  base의 추상화 수준이 잘못된 것인가?
  각 서비스가 base를 잘못 사용하는 것인가?

전자라면 base를 재설계해야 한다.
후자라면 base는 유지하면서 사용 방식을 정리하면 된다.

### 6.9 복잡도 재축적을 방어한다 (complexity is incremental 방어)

리팩토링으로 복잡도를 낮춰도,
이후 같은 패턴의 기능 추가가 반복되면 복잡도가 다시 쌓인다.

리팩토링 후 구조는 아래 변경에 강해야 한다.

- 새 transport 추가 시 engine/socket에 분기가 늘지 않는다.
- 새 service 추가 시 service runtime base에 특수 경로가 늘지 않는다.
- 새 socket family 추가 시 socket_base_t를 직접 수정하지 않는다.

반대로 아래 변경에서는 여전히 주의가 필요하다.

- 기존 hot path 정책 변경 (pipeline 내부 수정 필요)
- 기존 service의 lifecycle 단계 추가 (runtime 내부 수정 필요)

이 구분을 명시해야 리팩토링 결과의 수명이 길어진다.

## 7. 세부 리팩토링 축

각 축이 어떤 복잡도를 아래로 내리는지 한 문장 요약:

```text
축                        내리는 복잡도
────────────────────────  ──────────────────────────────────────────
7.1 API / Binding         → 내부 연결 구조 지식을 service facade 아래로
7.2 socket                → 공통 mechanism을 socket runtime 아래로
7.3 engine / transport    → hot path 정책과 scheme 세부를 pipeline/adapter 아래로
7.4 service               → 내부 socket 조합 구조를 service runtime 아래로
7.5 lifecycle / teardown  → close/destroy 실행 절차를 socket runtime 아래로
7.6 perf / benchmark      → 측정 세부를 perf contract module 아래로
```

## 7.1 API / Binding 경계

목표:

- `core/src/api/zlink.cpp`를 조립 허브가 아니라 얇은 facade entry로 축소
- API가 service/socket 내부 연결 구조를 직접 알지 않게 정리
- 기존 API 호환성에 제한받지 않고, semantic purity 기준으로 API를 재설계

실행 항목:

- API entry별로 생성, 옵션, lifecycle, utility 경로를 분리
- opaque handle별 internal owner를 명시
- 공통 option mapping을 facade 아래로 이동

### 7.2 socket 계층

목표:

- `socket_base_t` 축소
- socket 의미와 runtime 기계 작업 분리
- monitor / peer state / endpoint bookkeeping 분리

실행 항목:

- socket 공통 기계 작업을 `socket_runtime` 계열 내부 모듈로 수렴
- socket family별 차이는 semantic policy 객체로 축소
- poll/pipe/monitor glue를 단일 runtime 경계에 모음

### 7.3 engine / protocol / transport 경계

목표:

- `asio_engine_t`를 facade + pipeline 조합으로 재구성
- transport connect/listen/open과 protocol codec 경계를 명확화
- ws/wss/tls 특수 처리의 누수 축소

실행 항목:

- handshake pipeline 분리
- read/write buffer policy 분리
- transport adapter factory 도입
- raw/zmp codec 경계 표준화

### 7.4 service 계층

목표:

- service가 internal socket topology를 덜 알게 함
- `common/`, `control/`, `discovery/`, `gateway/`, `spot/`의 공통 lifecycle 정리
- monitor/readiness/registry 의미를 service runtime에서 흡수

실행 항목:

- service 공통 runtime base 강화
- service별 internal role 노출 제거
- public API를 "socket role"이 아니라 "service 의미" 기준으로 재설계

### 7.5 lifecycle / teardown / reaper 경계

목표:

- destroy와 close의 owner를 단일화
- tracked socket, internal attachment, callback revoke의 경계 명확화
- thread-safe 종료와 normal 종료가 같은 설명으로 이어지게 정리

실행 항목:

- resource ownership 표 작성 및 코드 반영
- shutdown 단계별 invariant 추가
- reaper와 runtime close contract 재정의

### 7.6 perf / benchmark / observability 경계

목표:

- 리팩토링 단계마다 성능 판단이 감이 아니라 계약으로 되게 정리
- perf 정책과 실제 구조 변경을 연결
- hot path regression 원인을 바로 찾을 수 있게 계측 포인트 정리

실행 항목:

- phase별 baseline 저장
- alloc/copy/branch 관찰 포인트 문서화
- single/multi 주요 패턴별 guardrail 명시

## 8. 단계별 실행 계획

각 phase는 "코드 정리"가 아니라 "구조 경계 확정" 단위로 나눈다.
각 phase는 독립 완료 조건을 가져야 한다.

### Phase 0. 기준선 고정

목표:

- 구조 개편 전에 기능/성능 기준선을 고정

작업:

- `core/tests/run_test_lanes.sh`
- 필요 시 `--include-e2e`
- `core/perf/run_benchmarks.sh --pattern ALL`
- `core/perf/run_benchmarks_multi.sh --pattern ALL`
- 대표 보고서와 commit hash 기록
- hotspot별 alloc/copy/CPU 샘플링 포인트 기록

산출물:

- baseline 문서
- 주요 regression watch list

완료 조건:

- baseline 없는 리팩토링 금지

### Phase 1. ownership map 정리

목표:

- lifecycle와 resource close owner를 먼저 확정

작업:

- socket/service/runtime/reaper별 resource ownership 표 작성
- destroy path를 단일 authoritative owner 기준으로 정리
- 중복 close 가능성 제거

대상 우선순위:

- `services/spot`
- `services/gateway`
- `sockets/socket_base.*`
- `core/reaper.*`
- `core/own.*`

완료 조건:

- 같은 리소스를 닫는 주체가 둘 이상 남지 않음
- thread-safe 종료와 일반 종료의 설명이 분리되지 않음

### Phase 2. socket runtime 분리

목표:

- socket 의미와 기계적 runtime 분리

작업:

- endpoint/peer/monitor/poll glue를 socket runtime 경계로 수렴
- socket family는 semantic policy에 집중
- `socket_base_t`에서 공통 기계 작업 제거

완료 조건:

- 새 socket 기능 추가 시 `socket_base_t` 직접 수정 빈도 감소
- monitor 및 endpoint 변경이 family 구현을 덜 침범

### Phase 3. engine pipeline 재구성

목표:

- `asio_engine_t` 복잡도를 pipeline 내부로 내림

작업:

- handshake/timer/buffer/write policy 분리
- completion callback 흐름을 ingress/egress/state transition 기준으로 재편
- speculative I/O와 gather write를 전용 정책 모듈 내부로 이동

완료 조건:

- `asio_engine_t`가 facade처럼 읽힘
- hot path 정책 변경이 connection lifecycle 코드 변경을 요구하지 않음

### Phase 4. transport adapter 통합

목표:

- URI -> endpoint open 전략을 상위에서 단순화

작업:

- tcp/ipc/ws/wss/tls transport 생성 규칙 표준화
- address/connecter/listener/transport 분산 책임 정리
- transport별 예외를 adapter 내부에서 흡수

완료 조건:

- 새 transport 추가 시 수정 범위가 국소화
- 상위는 transport 종류보다 capability만 알면 됨

### Phase 5. service facade 수렴

목표:

- service public 의미와 internal topology 분리

작업:

- `service_runtime_base` 중심 공통 lifecycle 재정렬
- discovery/gateway/spot의 control/data/runtime 경계 통일
- internal socket role 노출 제거

완료 조건:

- service 문서를 socket wiring 없이 설명 가능
- service별 monitor/readiness가 같은 구조 설명으로 연결됨

### Phase 6. perf recovery + structural cleanup

목표:

- 구조 변경 후 성능을 회복 또는 개선
- 성능을 위해 다시 구조를 망치지 않음

작업:

- regression 구간별 root cause 추적
- alloc/copy 제거
- branch pruning
- buffer sizing / batching / zero-copy 정책 정리

완료 조건:

- 성능 회복이 perf-only hack에 의존하지 않음
- 구조 설명과 성능 설명이 같은 모듈 경계에서 가능

### Phase 7. 최종 구조 고정

목표:

- 남은 파일 이동, 이름 정리, 문서/테스트 정합성 반영

작업:

- 책임 중심 rename
- 디렉터리 재배치가 정말 필요할 때만 수행
- internals/perf/guide 문서 갱신

완료 조건:

- 구조 문서와 코드가 같은 단어를 사용
- 신규 기여자가 ownership과 hot path를 문서만으로 추적 가능

## 9. 성능 게이트

각 phase는 아래 네 가지를 동시에 통과해야 한다.

### 9.1 기능 게이트

- `./core/tests/run_test_lanes.sh`
- 필요 시 `./core/tests/run_test_lanes.sh --include-e2e`

### 9.2 single perf 게이트

- `core/perf/run_benchmarks.sh --pattern ALL`
- 최소 기준:
  리팩토링 전 실측 baseline 대비 의미 있는 regression 없음
- 우선 감시 패턴:
  `PAIR`, `PUBSUB`, `DEALER`, `ROUTER`, `STREAM`, `SPOT`, `GATEWAY`

### 9.3 multi perf 게이트

- `core/perf/run_benchmarks_multi.sh --pattern ALL`
- 우선 감시 패턴:
  `MULTI_STREAM*`, `MULTI_SPOT*`, `MULTI_GATEWAY*`

### 9.4 hot path 게이트

아래 항목은 측정/샘플링 포인트로 관리한다.

- send/recv loop 내 heap alloc 증가 여부
- frame copy 횟수 증가 여부
- callback dispatch depth 증가 여부
- handshake 이후 steady-state branch 수 증가 여부
- CPU 사용률 및 context switching 악화 여부

정량 기준은 phase 시작 시 baseline과 함께 기록하고,
"이전보다 느려졌지만 구조가 좋아졌다"는 이유만으로는 통과시키지 않는다.

이 절의 baseline은 `Phase 0 baseline` 문서에 기록된
리팩토링 전 실측 데이터를 기준으로 한다.

baseline commit 규칙 (Phase 0 문서와 동일):

- single과 multi는 **반드시 같은 baseline commit**에서 측정한다.
  commit이 다르면 baseline으로 승인하지 않는다.
- tree 상태는 **clean**(uncommitted change 없음)이어야 한다.
- 실행 옵션 요약(build type, compiler, OS, CPU)을 필수로 남긴다.

## 10. 리팩토링 판단 질문

각 변경은 merge 전에 아래 질문에 답할 수 있어야 한다.

1. 이 변경으로 상위가 알아야 할 개념 수가 실제로 줄었는가?
2. 이 변경은 deep module을 만들었는가, 아니면 helper를 늘렸는가?
3. ownership이 더 명확해졌는가?
4. hot path alloc/copy/branch가 유지되거나 줄었는가?
5. service/socket/transport 예외 케이스가 상위로 새지 않는가?
6. perf harness 없이도 구조 자체가 설명 가능한가?
7. 같은 기능 변경 시 수정 파일 수가 줄어드는가?
8. 오류 가능한 경로가 구조적으로 제거되었는가,
   아니면 정책/문서로만 금지하고 있는가?
9. 이후 같은 종류의 기능 추가가 허브 타입을 다시 건드리는가?

하나라도 명확히 "아니오"면 구조 개선으로 보지 않는다.

## 11. 금지 사항

- perf 숫자를 위해 public contract를 약하게 만드는 변경
- teardown race를 테스트 완화로 덮는 변경
- sleep/retry 기반 동기화 추가
- 리팩토링 명목의 helper 남발
- 책임 중심이 아닌 구현 수단 중심 naming 확산
- phase gate 없이 대규모 파일 이동부터 하는 작업
- bench 전용 분기나 환경 변수에 의존한 성능 복구

## 12. 우선 적용 권장 순서

실행 우선순위는 아래가 적절하다.

1. `spot`, `gateway`, thread-safe 종료 경로
2. `socket_base_t` ownership / runtime 분리
3. `asio_engine_t` pipeline 분리
4. transport adapter 통합
5. API facade 정리
6. 최종 rename / 문서 정리

이 순서를 권장하는 이유는 다음과 같다.

- 현재 가장 큰 복잡도와 성능 리스크가 service lifecycle과 engine hot path에 걸쳐 있다.
- ownership이 정리되지 않은 상태에서 API나 디렉터리만 바꾸면 change amplification이 더 커진다.
- transport와 engine 경계는 socket/runtime 계약이 정리된 뒤에 바꿔야 안전하다.

## 13. 완료 판정

이번 리팩토링은 아래 상태가 동시에 만족될 때 완료로 본다.

- `core`의 주요 변경 경로가 ownership 관점으로 설명 가능하다.
- service/socket/engine/transport가 책임 중심 단어로 정리된다.
- `socket_base_t`, `asio_engine_t` 같은 허브 타입의 복잡도가 유의미하게 낮아진다.
- single/multi perf가 baseline 대비 비퇴행 또는 개선 상태다.
- 성능 설명이 특정 hack이 아니라 구조 설명과 같은 경계에서 가능하다.
- 신규 기능 추가 시 수정 범위가 눈에 띄게 줄어든다.

## 14. 최종 해석

이번 리팩토링은 "큰 구조를 예쁘게 다시 그린다"가 목표가 아니다.

목표는 다음 두 가지를 동시에 달성하는 것이다.

- `core`의 핵심 복잡성을 더 깊은 모듈 아래로 내린다.
- 그 과정에서도 zlink의 성능 자산을 잃지 않는다.

즉 POSD 관점에서 좋은 구조와 성능 관점에서 좋은 구조를
서로 양보시키는 것이 아니라,
"성능을 보존하는 깊은 모듈"을 만들도록 전체 시스템을 재편하는 것이
이번 계획의 본질이다.

## 15. 문서 사용 순서

실제 작업 시작 시 아래 순서로 문서를 본다.

| # | 파일 | 비고 |
| --- | --- | --- |
| 00 | [상위 계획 (본 문서)](00-core-system-posd-refactor-plan.ko.md) | |
| 01 | [Phase 0 Baseline](01-core-system-phase0-baseline.ko.md) | |
| 02 | [Phase 1 Ownership Map](02-core-system-phase1-ownership-map.ko.md) | |
| 03 | [Phase 1 Resource Inventory](03-core-system-phase1-resource-inventory.ko.md) | |
| 04 | [Phase 2 Socket Runtime Split](04-core-system-phase2-socket-runtime-split.ko.md) | |
| 05 | [Phase 3 Engine/Transport/Service](05-core-system-phase3-engine-transport-service-plan.ko.md) | 상위 Phase 3+4+5 통합 |
| 06 | [Review Log](06-core-system-review-log.ko.md) | |
