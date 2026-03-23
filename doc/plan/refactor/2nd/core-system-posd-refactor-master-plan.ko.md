# POSD 기반 `core` 2차 리팩토링 마스터 플랜

> 상태: draft
> 대상: `core/`
> 기준 원칙: POSD + 공개 API 호환 유지 + 성능 비퇴행
> 관련 문서:
> - `doc/plan/refactor/1st/README.ko.md`
> - `doc/plan/refactor/1st/00-core-system-posd-refactor-plan.ko.md`
> - `doc/plan/refactor/1st/04-core-system-phase2-socket-runtime-split.ko.md`
> - `doc/plan/refactor/1st/05-core-system-phase3-engine-transport-service-plan.ko.md`
> - `doc/plan/thread-safe/thread-safe-socket-plan.ko.md`

## 1. 문서 목적

이 문서는 `core` 1차 POSD 리팩토링 문서 세트를 바탕으로,
**이미 1차 핵심 경계가 코드에 반영된 현재 코드베이스**를 기준으로 작성한
2차 상세 실행 계획 마스터 플랜이다.

1차 문서 세트가 ownership, runtime, 최종 source layout 관점에서
`core`를 다시 읽는 기준을 세웠고,
현재 코드는 그 이후 여러 기능 추가와 변경을 거치면서 일부 복잡도가 다시 허브로
재집중된 상태라고 본다.

즉 2차 문서의 목적은 1차 설계를 다시 처음부터 수행하는 것이 아니다.
2차 문서는 **1차가 이미 반영된 baseline 위에서,
후속 기능 추가로 다시 커진 경계와 지식 누수를 정리**하는 데 집중한다.

따라서 이 문서에서 말하는 "호환 유지"와 "보존 대상"은
과거 1차 이전 인터페이스가 아니라 **현재 코드가 제공하는 현행 동작과 현행 공개 표면**이다.

1차 문서 세트가 ownership, runtime, 최종 source layout 관점에서
`core`를 다시 읽는 기준을 세웠다면,
2차 문서는 아래 네 경계에 집중한다.

- `api` facade
- `socket` semantic/runtime 경계
- `options` ownership 경계
- `service` runtime / access 경계

이번 문서의 핵심 목표는 단순한 파일 분해가 아니다.

- 공개 API는 유지한다.
- `core/include/zlink.h`가 제공하는 C API/ABI 계약은 유지한다.
- bindings가 기대하는 native library 계약을 깨지 않는다.
- 내부 지식 누수를 줄인다.
- 허브 객체를 deep module로 대체한다.
- 변경 하나가 넓게 번지는 구조를 줄인다.
- 리팩토링 후에도 테스트와 thread-safe 계약을 유지한다.

성공 기준은 아래 한 줄이다.

```text
공개 표면은 유지하면서, 내부 구조는 더 적은 개념과 더 적은 수정 범위로
안전하게 바꿀 수 있게 만든다.
```

그리고 여기서 "공개 표면"은 단순히 `core` C 호출자만 뜻하지 않는다.
이 저장소에서는 `core`가 **C API 형태로 제공되고, bindings가 그 위에 올라가는 구조**이므로
실제 외부 계약은 아래 두 층을 함께 뜻한다.

- `core/include/zlink.h`의 C API/ABI
- 각 language binding이 기대하는 `libzlink` native surface

## 2. 현재 상태 평가

### 2.1 현재 baseline

이번 2차 계획의 baseline은 아래처럼 고정한다.

- 1차 POSD 리팩토링의 핵심 방향은 이미 코드에 반영돼 있다.
- 2차는 그 이후 누적된 기능 추가와 경로 확장 때문에 다시 커진 허브를 정리하는 단계다.
- 판단 기준은 "1차 문서가 무엇을 말했는가"가 아니라
  "현재 코드가 어떤 동작과 경계를 실제로 제공하는가"다.
- 현재 `core`는 직접 사용되는 C API일 뿐 아니라,
  `cpp`, `dotnet`, `java`, `node`, `python` bindings가 의존하는 native library다.

즉 2차 계획은 설계 초안의 연장이 아니라
**현재 코드 기준의 구조 복구 및 재정렬 계획**으로 읽혀야 한다.

### 2.2 좋은 점

현재 `core`는 완전히 무질서한 상태는 아니다.
리팩토링의 출발점으로 삼을 수 있는 기반이 이미 있다.

- `core/src`가 `api`, `core`, `sockets`, `protocol`, `services`,
  `transports`, `utils`로 나뉘어 있다.
- `core/tests`가 `unittest`, `integration`, `e2e` lane으로 분리되어 있어
  구조 변경 후 회귀 검증 경로가 명확하다.
- 서비스 계층이 `gateway`, `discovery`, `spot`로 이미 디렉터리 분리돼 있다.
- 1차 POSD 문서 세트가 ownership, socket runtime split, target source layout에 대한
  상위 원칙을 고정했고, 현재 코드는 그 결과를 일부 이미 반영하고 있다.

즉 지금 필요한 것은 "처음부터 새 구조를 만드는 것"이 아니라,
이미 존재하는 분리 축을 실제 지식 경계와 ownership 경계로 끌어올리는 작업이다.

### 2.3 핵심 문제

현재 구조는 디렉터리 분리는 되어 있지만,
POSD 기준에서 **정보 은닉과 변경 증폭 억제** 측면은 아직 부족하다.

### 2.3.1 `api/zlink.cpp`가 과도한 허브다

`core/src/api/zlink.cpp`는 공개 C API의 구현 파일이지만,
실제로는 다음 내부 세부까지 함께 알고 있다.

- socket 생성과 종류별 분기
- service 생성/attach/start/stop
- message / callback / poller / monitor 연계
- protocol / metadata 일부 처리
- service concrete type include

이 구조의 문제는 단순히 파일이 크다는 점이 아니다.
문제는 공개 API 계층이 **내부 구현 세부를 너무 많이 안다**는 점이다.

POSD 관점에서 이 파일은
"얇은 진입점"이 아니라 "광범위한 내부 지식 허브"에 가깝다.
특히 1차 이후 서비스 기능, handle 정책, callback 정책, option 정책이 추가되면서
한곳으로 계속 누적되기 쉽다.

### 2.3.2 `socket_base_t`가 semantic과 mechanism을 함께 가진다

`socket_base_t`는 socket abstraction의 중심이지만,
동시에 너무 많은 공통 기계 작업을 들고 있다.

- send / recv 의미
- pipe 이벤트
- callback dispatch
- send-ready handler
- monitor emission
- endpoint bookkeeping
- lifecycle / close / stop 연동

이 구조는 공통화에는 유리해 보여도,
family별 semantic 변경과 공통 runtime 변경이 서로 영향을 주기 쉽다.

POSD 기준에서 이는 "깊은 모듈"이라기보다
"표면적이 넓은 공통 허브"에 가깝다.

### 2.3.3 `options_t`가 중앙 옵션 가방 역할을 한다

`options_t`는 socket, transport, protocol, service 근처 정책이 한 struct에 같이 들어간다.

이것이 만드는 문제는 아래와 같다.

- 새 옵션이 추가될 때 소유권이 불분명하다.
- 옵션 validation/apply logic이 중앙에서 비대해진다.
- transport 변경이 socket/service 계층까지 넓게 퍼진다.
- 내부 모듈이 자기와 무관한 옵션 필드를 알게 된다.

즉 `options_t`는 compatibility carrier 이상의 역할을 맡고 있으며,
그 결과 option ownership이 흐려져 있다.

### 2.3.4 `service_runtime_base_t` 경계는 더 선명해질 필요가 있다

이 타입은 service lifecycle coordinator이면서,
service가 소유한 socket 집합을 추적하는 runtime helper이기도 하다.

현재 코드 기준으로 actual close/wait mechanics 자체는
`socket_close_ops_t`와 `ctx_t`에 위임되어 있다.
하지만 아래 계약은 문서와 구조에서 더 선명해질 필요가 있다.

- service-owned socket registry
- lifecycle state machine
- `socket_close_ops_t` 호출 경계
- `ctx_t`의 global removal tracking과의 협력 지점
- shutdown / drain timeout 의미

즉 현재 문제가 "이미 잘못된 중복 구현"이라기보다,
서비스 소유권과 global removal 책임의 경계가 약하게 설명되어
향후 기능 추가 시 coordinator가 다시 비대해질 위험이 있다는 점이다.

### 2.3.5 현재 구조 평결

현재 `core`는 다음처럼 요약할 수 있다.

```text
디렉터리 분리는 존재하지만,
실제 설계 경계는 아직 허브 파일과 허브 타입 중심으로 읽힌다.
```

이 상태를 바꾸지 않으면,
새 기능이 추가될수록 `api` 허브, `socket_base_t`, `options_t`,
service runtime 공통부로 복잡도가 재집중될 가능성이 높다.

### 2.4 현행 기능 보존 목록

이번 리팩토링에서 보존 기준으로 삼을 현행 기능 축을 먼저 고정한다.
이 목록은 과거 설계안이 아니라 현재 코드의 실제 제공 기능을 기준으로 한다.

- `core/include/zlink.h`의 공개 함수 시그니처, 상수, enum 값, callback signature
- exported `zlink_*` symbol 집합과 `ZLINK_EXPORT` surface
- `zlink_msg_t`, `zlink_routing_id_t` 같은 공개 C struct layout/size 계약
- `gateway`, `discovery`, `spot` 서비스 경로와 관련 service access surface
- socket message dispatch, spot handler, send-ready handler, stream dispatch 경로
- monitor / poller / routing-id / subscription 관련 공개 동작
- TLS 관련 옵션과 secure transport 경로
- `monitor_event_version`, `hello_msg`, `disconnect_msg`, `hiccup_msg`,
  `busy_poll` 등 후속 추가 옵션
- 현재 테스트 lane이 이미 덮고 있는 service lifecycle / teardown / callback 경로
- `cpp`, `dotnet`, `java`, `node`, `python` bindings가 현재 기대하는 native 호출 규약
- release 이후 `bindings/update_zlink_libs.sh`로 배포되는 native artifact 소비 방식

즉 2차는 "구조는 바꾸되, 현재 기능은 줄이지 않는다"가 기본 전제다.
특히 bindings 관점에서는 "내부 구조는 바꾸되, C 계약은 깨지 않는다"가 더 강한 제약이다.

### 2.5 적용 경계와 no-touch 규칙

이번 계획은 실행 가능한 문서여야 하므로,
"무엇을 바꾸는가"와 "무엇은 건드리지 않는가"를 먼저 고정한다.

직접 수정 대상:

- `core/src/api/`
- `core/src/sockets/`
- `core/src/core/`
- `core/src/services/`
- `core/tests/`의 회귀/검증 추가분

원칙적으로 직접 수정하지 않는 대상:

- `core/include/zlink.h`
- `core/src/libzlink.vers`
- `bindings/cpp/include/`
- `bindings/dotnet/src/`
- `bindings/java/src/main/java/`
- `bindings/node/src/`, `bindings/node/native/src/`
- `bindings/python/src/`

예외:

- bindings 테스트나 문서에서 리팩토링 검증을 보강하는 정도의 수정은 허용한다.
- bindings 소스 수정이 필요해지면 우선 `core` 리팩토링이 C 계약을 침범한 것인지부터 재검토한다.
- `core/include/zlink.h` 또는 `core/src/libzlink.vers` 수정이 필요해지면
  이번 계획 범위를 벗어난 것으로 간주하고 별도 API/ABI 변경 계획으로 분리한다.

## 3. 전체 목표 구조

2차 리팩토링 이후 `core`는 아래 네 층으로 읽혀야 한다.

```text
public API facade
    ->
runtime core
    ->
domain modules
    ->
supporting utilities
```

보다 구체적으로는 아래 흐름을 목표로 한다.

```text
public API facade
  -> context / socket / message / service use-case adapters
  -> runtime core
  -> socket semantic/runtime
  -> service facade/runtime
  -> transport/protocol modules
  -> supporting utilities
```

### 3.1 계층별 추상화

| 계층 | 상위에 제공하는 추상화 | 내부로 숨겨야 하는 것 |
| --- | --- | --- |
| public API facade | 공개 C API 진입점 | concrete service/socket implementation detail |
| runtime core | context, shutdown, close/drain orchestration | 내부 socket/resource finalization mechanics |
| engine / io backend | poller, io_context, mailbox-driven execution backbone | backend-specific event loop and execution context coupling |
| socket semantic/runtime | socket family 의미와 공통 기계 작업 | callback/monitor/endpoint bookkeeping 세부 |
| service facade/runtime | 서비스 의미와 lifecycle | internal socket topology, owned socket drain |
| transport/protocol | wire/channel capability | address/scheme/TLS layering 세부 |
| supporting utilities | 재사용 가능한 기반 | 특정 도메인 정책 |

핵심은 각 계층이 단순 pass-through가 아니라
아래 복잡성을 실제로 숨기는 deep module로 읽혀야 한다는 점이다.

각 계층이 숨겨야 할 "비밀"은 아래 한 줄로 고정한다.

- public API facade: handle/tag 검증 뒤의 concrete service/socket branching과 wire decode를 숨긴다.
- runtime core: shutdown/finalization/removal sequencing과 timeout 협력 세부를 숨긴다.
- engine / io backend: ASIO poller, io_context, mailbox scheduling이 어떻게 결합되는지 숨긴다.
- socket semantic/runtime: family 의미와 무관한 dispatch/monitor/endpoint bookkeeping을 숨긴다.
- service facade/runtime: service-owned socket topology와 internal control/data plane wiring을 숨긴다.
- transport/protocol: address scheme, TLS handshake, wire framing 세부를 숨긴다.
- supporting utilities: 어떤 도메인 정책에도 종속되지 않는 공용 기반만 남긴다.

### 3.2 목표 의존 방향

```text
api facade -> use-case adapter -> runtime/domain contract

services -> runtime contract + socket/protocol contract

sockets -> runtime core + engine/io backend + protocol/transport capability

runtime core -> engine/io backend

transport/protocol -> engine/io backend + lower-level primitives

utils -> generic support only
```

금지 방향:

- public API가 service concrete type detail을 직접 많이 아는 것
- service가 socket close/wait mechanics를 재구현하는 것
- transport/protocol detail이 API 계층까지 새는 것
- domain-specific policy가 `utils`로 밀려 들어가는 것

### 3.3 before/after 폴더 구조

이 절의 목적은 "리팩토링 후 어떤 책임 경계가 source tree에도 드러나야 하는가"를
보여주는 것이다.

중요:

- 아래 `after` 트리는 **목표 책임 구조 예시**다.
- 실제 파일 이동은 필요한 경우에만 수행한다.
- 핵심은 경로명이 아니라 ownership과 정보 은닉 경계다.

### before: 현재 `core/src` 구조

```text
core/src/
  api/
  core/
  engine/
    asio/
  protocol/
  services/
    common/
    control/
    discovery/
    gateway/
    spot/
  sockets/
  transports/
    ipc/
    pgm/
    tcp/
    tls/
    ws/
  utils/
```

현재 문제는 디렉터리 수가 적어서가 아니라,
이 구조 위에서 `api/zlink.cpp`, `socket_base_t`, `options_t`,
`service_runtime_base_t` 쪽으로 지식이 다시 재집중된다는 점이다.

### after: 목표 책임 구조

```text
core/src/
  api/
    zlink.cpp
    zlink_option.cpp
    context_api.*
    socket_api.*
    message_api.*
    service_api.*
    poller_api.*

  core/
    ctx.*
    own.*
    reaper.*
    mailbox.*
    pipe.*
    poller.*
    close_drain/*
    finalization/*

  engine/
    asio/*

  sockets/
    base/
      socket_base.*
      endpoint_registry.*
      monitor_bridge.*
      dispatch_bridge.*
      lifecycle_hooks.*
      peer_state.*
    families/
      pair/*
      pubsub/*
      dealer/*
      router/*
      stream/*

  protocol/
    raw/*
    zmp/*
    metadata/*

  services/
    common/
      service_runtime_base.*
      service_public_api.*
    discovery/
      discovery_access.*
      facade/*
      runtime/*
      registry/*
      protocol/*
    gateway/
      gateway_access.*
      facade/*
      runtime/*
      routing/*
    spot/
      spot_node_access.*
      facade/*
      runtime/*
      node/*
      data_plane/*

  transports/
    adapter/*
    tcp/*
    ipc/*
    tls/*
    ws/*
    pgm/*

  utils/
```

해석 규칙:

- `api/`는 validate + delegate를 기본으로 하되,
  공개 handle admission/lifetime 확인 같은 per-handle orchestration만 제한적으로 가진다.
- `core/`는 close/drain/finalization 포함 runtime primitive를 가진다.
- `engine/`는 poller/io_context/mailbox execution backbone을 가진다.
- `sockets/base/`는 공통 mechanism, `sockets/families/`는 family semantic을 가진다.
- `services/*/facade`와 `services/*/runtime`은 분리된다.
- `services/common/service_public_api.*`는 semantic service API가 아니라 public admission/close guard다.
- service access seam은 common hub가 아니라 service-local seam으로 둔다.
- `transports/adapter/`는 상위가 scheme 세부를 몰라도 되게 만드는 경계다.

### 3.4 before/after 모듈 구조

### before: 현재 모듈 관계

```text
bindings / C callers
        |
        v
core/include/zlink.h
        |
        v
api/zlink.cpp  <--------------------------------------------+
  |   |   |   |   \                                          |
  |   |   |   |    +-> services/discovery/*                  |
  |   |   |   +------> services/gateway/*                    |
  |   |   +----------> services/spot/*                       |
  |   +--------------> core/ctx.*, core/socket_poller.*      |
  +------------------> sockets/socket_base.*                 |
                       |                                      |
                       +-> dispatch / monitor / endpoint /    |
                           lifecycle / pipe / callback glue   |
                                                              |
service_runtime_base.* ---------------------------------------+
  |   |   |
  |   |   +-> socket_close_ops.*
  |   +------> core/ctx.*
  +----------> owned socket registry + drain polling

options_t
  +-> socket / transport / protocol / service option이 한곳에 혼재
```

현재 병목은 `api/zlink.cpp`, `socket_base_t`, `service_runtime_base_t`,
`options_t`가 각각 허브처럼 읽힌다는 점이다.

### after: 목표 모듈 관계

```text
bindings / C callers
        |
        v
core/include/zlink.h
        |
        v
api facade
  |- context_api
  |- socket_api
  |- message_api
  |- service_api
  \- poller_api
        |
        v
runtime contracts
  |- ctx / shutdown / close_drain / finalization
  |- socket runtime components
  |- service runtime contracts
  \- engine binding
       \- poller / io_thread / asio backend
         ^
         |
protocol / transport capability
  |- raw / zmp / metadata
  \- tcp / ipc / tls / ws / pgm
         ^
         |
         +------------------+
         |                  |
         v                  v
socket families        service facades/runtimes
  |- pair              |- discovery
  |- pubsub            |- gateway
  |- dealer            \- spot
  |- router
  \- stream
```

핵심 차이:

- `api/zlink.cpp`는 더 이상 concrete service 조립 허브가 아니다.
- `socket_base_t`는 family semantic entrypoint로 축소된다.
- close/drain 의미는 runtime contract에 있고, option owner 재배치는 별도 경계에서 다룬다.
- bindings는 계속 C 계약만 보고, 내부 구조는 교체 가능해진다.

## 4. 우선순위와 원칙

이번 리팩토링의 우선순위는 아래 순서로 고정한다.

1. C API/ABI 및 bindings 계약 유지
2. 공개 API 동작 유지
3. 복잡도 감소
4. 설명 가능한 ownership / lifecycle
5. 테스트 가능성 증가
6. 파일/디렉터리 미관

구조가 더 예뻐 보여도 아래 중 하나가 발생하면 실패다.

- public API semantic 회귀
- `core/include/zlink.h` 시그니처/상수/struct layout 변화
- bindings가 기대하는 native symbol/호출 규약 변화
- close/drain/shutdown 계약 약화
- callback/monitor 동작 회귀
- thread-safe 규약 후퇴
- 새 helper 또는 새 base class가 기존 허브를 이름만 바꿔 재생산

핵심 원칙:

- 파일 쪼개기보다 지식 경계 재정의가 먼저다.
- helper 증식보다 deep module 형성이 우선이다.
- 공통화는 family/service 수정 범위를 줄일 때만 허용한다.
- 리팩토링 중 기능 추가는 하지 않는다.
- test를 약하게 만들어 통과시키는 방식은 금지한다.

## 5. 우선 정리 대상

### 5.1 1순위: `core/src/api/zlink.cpp`

1차에서도 주요 개선 대상이었고,
현재도 **후속 기능 추가로 다시 복잡도가 재집중된 잔여 허브**이므로
가장 먼저 다시 손봐야 한다.

즉 이 항목의 의미는
"1차가 실패했다"가 아니라,
"1차 이후 현재 코드 기준으로도 여전히 가장 큰 재집중 지점이다"에 가깝다.

현재도 공개 API 구현 파일이 service/socket/protocol/runtime 내부를 직접 많이 알고 있기 때문이다.

이번 단계의 목표는 이 파일을 제거하는 것이 아니라,
**validate + delegate + per-handle admission 수준의 orchestration만 남기는 facade**로 낮추는 것이다.
단, facade 축소 과정에서도 `core/include/zlink.h`와 exported symbol surface는
bindings 계약이므로 변경하지 않는다.

여기서 API 계층에 남아도 되는 것은 아래 수준으로 제한한다.

- handle validation과 tag/type 확인
- 공개 handle의 admission / lifetime guard
- per-handle 범위에서 닫힘과 API 진입을 조정하는 최소 상태

반대로 아래 로직은 하위 adapter 또는 runtime/domain으로 내려간다.

- monitor event wire decode / protocol parsing
- service-wide mode registry / poller registration table / handler registry
- concrete service/socket branching 세부
- service 내부 topology나 socket runtime sequencing

### 5.2 2순위: `core/src/sockets/socket_base.hpp/.cpp`

socket 의미와 socket 공통 기계 작업을 분리하는 중심 지점이다.
이 타입이 줄어들지 않으면 service 분해와 option ownership 분리도
결국 여기로 다시 재집중된다.

### 5.3 3순위: `core/src/core/options.hpp/.cpp`

option ownership을 다시 세우지 않으면,
transport/socket/service 재구성 후에도 중앙 bag 구조가 유지된다.

이번 단계는 `options_t`를 즉시 제거하는 것이 아니라,
option parsing/validation/apply 책임을 도메인별로 되돌리는 데 초점을 둔다.

### 5.4 4순위: `core/src/services/common/service_runtime_base.hpp`

이 영역의 목표는 무리한 통합이 아니라,
`service_runtime_base_t`, `socket_close_ops_t`, `ctx_t` 사이 계약을 더 선명하게 만드는 것이다.

service-owned socket tracking과 global socket removal tracking은 다른 책임이므로
이를 억지로 하나로 합치지 않는다.

### 5.5 5순위: `gateway`, `discovery`, `spot` 대형 구현 파일

공통 경계를 먼저 정리한 뒤에야
각 서비스의 topology, lifecycle, control/data plane을 의미 있게 쪼갤 수 있다.

서비스부터 먼저 분해하면 상위 허브가 그대로 남아
얕은 파일 이동만 반복될 가능성이 높다.

### 5.6 단계별 실제 수정 파일 묶음

Phase별 구현 시작점은 아래처럼 고정한다.
이 목록은 "먼저 여기를 본다"는 뜻이며, 실제 세부 분해는 이 범위 안에서 진행한다.

### Phase 1 파일 묶음

- `core/src/api/zlink.cpp`
- `core/src/api/zlink_option.cpp`
- Phase 1c에서 concrete service include 제거에 꼭 필요하면 기존 seam을 우선 활용하는 최소 contract shell:
  - `core/src/services/common/service_public_api.hpp`
  - `core/src/services/*/*_access.*`
- 필요 시 새 private 구현 파일:
  - `core/src/api/context_api.cpp`
  - `core/src/api/socket_api.cpp`
  - `core/src/api/message_api.cpp`
  - `core/src/api/service_api.cpp`
  - `core/src/api/poller_api.cpp`

Phase 1은 아래 sub-phase checkpoint로 나눈다.

- Phase 1a: context/message/errno/version 계열 분리
- Phase 1b: socket/poller/monitor 계열 분리
- Phase 1c: existing service seam 기준 `zlink.cpp` concrete knowledge 제거

Phase 1c 선행 규칙:

- 새 facade 파일을 늘리기 전에,
  현재 존재하는 `service_public_api_guard_t`와 service-local `*_access` seam을 기준으로
  `zlink.cpp`가 concrete service include를 직접 덜 보게 만드는 **최소 service access/factory contract**
  를 먼저 고정한다.
- 이 최소 contract 작성은 Phase 1c 범위에 포함할 수 있다.
- Phase 5는 이 최소 contract를 공통 규약으로 확장하고 정제하는 단계이지,
  Phase 1c가 임시 wrapper 분산으로 끝나는 것을 허용하는 단계가 아니다.

Checkpoint 규칙:

- 각 sub-phase 종료 시 core 기본 게이트와 C 계약 확인을 통과해야 다음 sub-phase로 넘어간다.
- Phase 1a, 1b는 core 기본 게이트 + C 계약 확인을 기본으로 한다.
- Phase 1c는 existing service seam과 service entrypoint를 직접 건드리므로,
  종료 시점에 대표 bindings smoke를 최소 1회 **의무**로 수행한다.

### Phase 2 파일 묶음

- `core/src/sockets/socket_base.hpp`
- `core/src/sockets/socket_base.cpp`
- 필요 시 새 private runtime component 파일:
  - dispatch/monitor/peer/endpoint/lifecycle 계열

주의:

- `socket_base_t`에는 이미 `endpoint_registry_t`, `monitor_bridge_t`,
  `dispatch_bridge_t`, `lifecycle_hooks_t`가 존재하고,
  `socket_runtime_t`는 이들을 묶는 내부 aggregation wrapper로 존재한다.
- Phase 2의 목적은 이 1차 분리 흔적을 무시하고 새 구조를 다시 만드는 것이 아니다.
- 우선 원칙은 **기존 partial split을 owner 기준으로 강화하고,
  alias/reference 남용과 허브 재집중을 줄이는 방향**이다.
- 완전히 새 runtime 계층을 도입하는 것은 기존 구조를 확장/정리해도 해결되지 않을 때만 허용한다.

### Phase 3 파일 묶음

- `core/src/services/common/service_runtime_base.hpp`
- `core/src/services/common/`
- `core/src/sockets/socket_close_ops.*`
- `core/src/core/ctx.*` 중 close/wait 관련 부분

### Phase 4 파일 묶음

- `core/src/core/options.hpp`
- `core/src/core/options.cpp`
- option owner가 있는 각 subsystem 적용 지점

### Phase 5 파일 묶음

- `core/src/api/zlink.cpp`
- `core/src/services/common/service_public_api.hpp`
- `core/src/services/*/*_access.*`
- 서비스 생성/attach/query 진입점

### Phase 6 파일 묶음

- `core/src/services/gateway/`
- `core/src/services/discovery/`
- `core/src/services/spot/`

구현 순서 규칙:

1. 파일 묶음은 "ownership 이동의 시작점"이지 절대 경계가 아니다.
2. 한 phase의 목표 owner를 옮기기 위해 caller/callee 양쪽 수정이 필요하면 허용한다.
3. 다만 수정 범위는 현재 phase의 owner 이동을 설명하는 데 필요한 최소 범위여야 한다.
4. 다음 phase의 목표까지 한 번에 같이 해결하려고 범위를 넓히면 설계 경계가 흐린 것으로 보고 중단 후 재설계한다.
5. `core/include/zlink.h`나 bindings 소스로 수정 범위가 번지면 현재 phase는 실패로 판정한다.
6. 같은 파일이 여러 phase에 등장하는 경우(예: Phase 1과 5의 `zlink.cpp`)는
   이전 phase 결과 위에서 수행하는 후속 정제로 해석하며, 경계 침범으로 보지 않는다.

phase 롤백/병합 규칙:

- 각 phase는 독립 branch 또는 독립 commit series로 진행한다.
- 각 sub-phase/phase는 "빌드 가능 + 해당 게이트 통과" 상태에서만 다음 단계로 진행한다.
- 다음 단계에서 경계가 무너지면 이전 통과 지점으로 되돌아가서 재설계한다.
- 여러 phase를 한 번에 섞어서 구현 후 마지막에 정리하는 방식은 금지한다.

## 6. 단계별 실행 계획

### 6.1 Phase 0: 기준선 고정

### 목적

리팩토링 전후 비교 기준을 고정한다.

### 변경 내용

- `core/build/`에서 기본 회귀 테스트 기준선을 잡는다.
- `unittest`, `integration`을 기본 게이트로 삼는다.
- public API 기본 흐름, close/drain, callback/monitor, service lifecycle을
  회귀 판단 축으로 고정한다.
- 성능 비퇴행 확인을 위해 Phase 0에서 최소 1회 perf baseline을 기록한다.
  이 문서의 직접 수정 범위는 `core/perf/`가 아니지만,
  구조 리팩토링 전후 비교를 위한 읽기 전용 기준선은 확보한다.
- `core/include/zlink.h`와 bindings가 기대하는 native 계약을
  리팩토링의 최상위 고정 제약으로 문서화한다.
- 1차 이후 추가된 현행 기능/옵션 목록을 리팩토링 보존 대상으로 문서에 못 박는다.
  최소 고정 대상:
  - `gateway`, `discovery`, `spot` 서비스 경로
  - message/spot/send-ready/stream dispatch 경로
  - monitor / routing-id / subscription 관련 surface
  - TLS 및 후속 추가 socket option surface
  - 공개 C struct layout / callback signature / exported symbol surface
  - 각 bindings가 호출하는 native surface

### 금지 사항

- 테스트를 느슨하게 바꿔서 리팩토링을 통과시키지 않는다.
- sleep/retry 기반의 불안정한 검증을 추가하지 않는다.
- 1차 이후 추가된 현행 기능을 "구조적 잡음"으로 보고 암묵적으로 제거하지 않는다.
- bindings가 의존하는 C surface를 "내부 구현 세부"로 보고 무심코 정리하지 않는다.

### 완료 판정 기준

- 기준선 테스트 결과와 핵심 검증 축이 문서/작업 로그 수준에서 고정돼 있다.
- no-touch 파일(`core/include/zlink.h`, `core/src/libzlink.vers`, bindings 소스)이
  이번 리팩토링 범위 밖이라는 점이 명시돼 있다.
- 기능 기준선과 함께 최소 1회 perf baseline 기록이 존재한다.

### 6.2 Phase 1: API facade 분해

### 목적

1차 이후 다시 커진 `api/zlink.cpp`를 공개 API 허브가 아니라
얇은 facade entrypoint로 바꾼다.

### 변경 내용

- 내부 구현을 관심사별 private translation unit으로 분리한다.
- 최소 분리 단위:
  - context API
  - socket API
  - message API
  - option API
  - poller/monitor API
  - service API
- public C 함수는 입력 검증 후 내부 use-case adapter로 위임한다.
- service 관련 변경은 **기존 `service_public_api_guard_t` + service-local `*_access` seam**을 우선 사용해
  concrete include와 하위 concrete branching을 줄이는 방향으로 진행한다.
- API 계층에 남겨도 되는 orchestration과 내려야 하는 로직의 기준을 아래처럼 고정한다.
  - API에 남는 것:
    - handle validation
    - per-handle admission/lifetime guard
    - per-handle 범위의 API 진입/닫힘 조정
  - 하위로 내리는 것:
    - monitor event wire decode
    - protocol parsing
    - service-wide mode registry
    - poller registration table
    - handler registry
    - concrete service/socket branching
    - runtime sequencing 세부

### 산출물

- `zlink.cpp`는 public entry aggregation 파일로 축소된다.
- context/socket/message/service/poller 관심사가 private 구현 파일로 분산된다.
- `zlink.cpp` 상단 include 집합에서 service concrete include가 줄어든다.
- service-local access seam이 `zlink.cpp`의 concrete knowledge를 흡수하는 방향으로 정리된다.

### 금지 사항

- 단순 helper 함수만 대량으로 만들고 여전히 `zlink.cpp`가 모든 내부 타입을 아는 구조
- API 계층에 남아야 하는 orchestration까지 무조건 하위로 밀어 넣는 것
- service-wide registry/table을 API 계층 ownership으로 굳혀 버리는 것
- 이미 존재하는 seam을 쓰지 않고 새 wrapper/facade를 먼저 추가하는 것
- public API 계층에 서비스별 특수 정책을 계속 누적하는 것
- facade 분해 과정에서 C API entrypoint 시그니처나 exported symbol을 바꾸는 것

### 완료 판정 기준

- `zlink.cpp`의 direct service include는 `service_public_api.hpp`와 service-local `*_access` seam으로 제한한다.
- `zlink.cpp`에 service-wide registry/table owner가 새로 추가되지 않는다.
- 기존 service-wide registry/table은 Phase 1 종료 시점에
  1) service-local seam 또는 runtime owner로 이동했거나,
  2) 남아 있다면 이동 보류 이유와 다음 owner phase가 문서에 명시돼 있어야 한다.
- service create/attach/query 경로는 `zlink.cpp -> service-local access seam -> concrete service` 수준으로 추적 가능하다.
- `core/include/zlink.h`와 `core/src/libzlink.vers`는 변경되지 않는다.

### 6.3 Phase 2: socket runtime 책임 분리

### 목적

1차에서 세운 semantic/runtime 경계를 현재 코드 기준으로 복구하고,
`socket_base_t`에서 다시 커진 공통 mechanism을 정리한다.

### 변경 내용

- 공통 mechanism을 아래 책임으로 분리한다.
  - lifecycle/mailbox ownership
  - dispatch/callback context
  - monitor/event emission
  - endpoint/peer bookkeeping
- `socket_base_t`는 semantic entrypoint를 제공하는 façade로 남긴다.
- common mechanism은 private collaborator 또는 runtime component로 숨긴다.
- family 구현은 기존 accessor/virtual contract 위에 머물게 하고,
  runtime 내부 필드 직접 의존을 새로 만들지 않는다.

### 산출물

- `socket_base_t`의 public/protected surface는 의미 중심으로 남는다.
- callback/monitor/peer/endpoint/lifecycle glue는 별도 internal component로 분리된다.

### 금지 사항

- 이름만 바뀐 `socket_runtime_t` mega-class 재생산
- family가 runtime 내부 세부를 더 많이 알게 되는 구조

### 완료 판정 기준

- monitor 변경이 send/recv semantic 구현을 건드리지 않는다.
- callback/dispatch 변경이 family별 라우팅 정책에 넓게 퍼지지 않는다.
- family 구현 파일에서 runtime internal field 또는 runtime component concrete type 직접 참조는 0을 유지한다.
- `socket_base_t` 변경이 대표 family 파일군(`dealer/router/xpub/xsub`) 수정으로 번지지 않는 경로가 기본이 된다.
- bindings가 직접 쓰는 `zlink_msg_t`, `zlink_routing_id_t` 사용 계약은 유지된다.
- thread-safe stress 게이트를 통과한다.

### 6.4 Phase 3: close/drain contract 명확화와 경계 강화

### 목적

1차에서 정리된 ownership 원칙을 기준으로,
`service_runtime_base_t`, `socket_close_ops_t`, `ctx_t` 사이의 close/wait/drain 계약을
명확히 하고 경계를 강화한다.

이 단계의 목표는 세 타입을 억지로 하나로 합치는 것이 아니다.

### 변경 내용

- `service_runtime_base_t`는 service-owned socket registry와 lifecycle coordinator 책임을 유지한다.
- `socket_close_ops_t`는 actual close/wait helper contract owner로 유지한다.
- `ctx_t`는 global socket removal/wait owner로 유지한다.
- timeout 의미, shutdown sequencing, error propagation, ownership handoff 지점을 문서와 코드에서 명시한다.
- bypass path나 중복 로직이 있으면 제거하되, owner 자체를 무리하게 합치지는 않는다.

### 산출물

- service-owned ownership과 global removal ownership의 경계가 설명 가능해진다.
- close/wait/drain collaboration contract가 문서와 코드에서 같은 형태로 읽힌다.

### Phase 4와의 경계 규칙

- close/drain의 관찰 가능한 의미를 바꾸는 owner는 Phase 3이다.
- option의 저장/검증/apply owner를 재배치하는 owner는 Phase 4다.
- `linger`, timeout, close 관련 option이 close/drain semantics와 교차하더라도
  "의미 정의"는 Phase 3에서 고정하고, Phase 4는 그 의미를 어느 모듈이 해석하는지 정리한다.
- 즉 Phase 4는 close/drain semantics를 재설계하지 않는다.

### 금지 사항

- 각 service가 close/wait 타이밍 로직을 따로 가지는 것
- 서로 다른 owner 책임을 "중복"으로 오해하고 `ctx_t`와 `service_runtime_base_t`를 억지로 합치는 것
- observable shutdown semantics를 둘 이상의 위치에서 따로 정의하는 것

### 완료 판정 기준

- service shutdown path가 공통 runtime contract로 설명된다.
- self-close, attach/query, teardown 관련 회귀를 같은 규칙으로 다룰 수 있다.
- thread-safe stress 게이트를 통과한다.

### 6.5 Phase 4: option ownership 분리

### 목적

1차 이후 추가된 옵션까지 포함해,
`options_t` 저장 구조를 무리하게 쪼개지 않으면서
option owner와 validation/apply 경계를 도메인 소유 관점으로 다시 정리한다.

### 변경 내용

- `options_t` storage layout과 `own_t` embedding은 이번 단계에서 유지한다.
- 내부 option domain을 아래 수준으로 정리한다.
  - core socket behavior
  - transport/network behavior
  - protocol/metadata behavior
  - service-specific behavior
- 외부 `setsockopt/getsockopt` 표면은 유지한다.
- option parsing/validation/apply dispatch는 각 owning subsystem 가까이 이동한다.
- option owner map을 문서와 코드에 명시한다.
- 새 옵션 추가 정책은 "중앙 bag field 직추가"가 아니라
  "owner 지정 + validation/apply 경로 지정"으로 바꾼다.
- 후속 추가 옵션(`monitor_event_version`, hello/disconnect/hiccup 계열,
  TLS 관련 옵션, `busy_poll`)도 현행 의미를 유지한 채 owner를 재정렬한다.

### 산출물

- option owner map이 문서와 코드에 모두 존재한다.
- 중앙 `options.cpp`는 모든 validation/apply를 혼자 들고 있지 않게 된다.
- `options_t` field는 남아 있어도 "누가 해석하는가"는 더 좁은 owner 경계로 설명된다.

### 금지 사항

- 이번 단계를 `options_t` 대규모 구조체 분해 프로젝트로 바꾸는 것
- 새 옵션을 중앙 struct field에 즉시 추가하는 습관 유지
- validation과 apply를 중앙 파일이 계속 독점하는 구조
- option ownership 재정리를 이유로 공개 option 번호, 의미, ABI surface를 바꾸는 것

### 완료 판정 기준

- 옵션 하나를 수정할 때 어떤 모듈이 owner인지 바로 답할 수 있다.
- transport 옵션 변경이 unrelated service/socket 코드를 덜 건드린다.
- `options_t` 저장 구조를 유지한 채 validation/apply owner가 더 좁은 경계로 이동한다.

### 6.6 Phase 5: service access / factory 경계 재정의

### 목적

1차 이후 확장된 service 경로가 public API 허브를 통해 concrete detail을
다시 노출하지 않게 만든다.

### 변경 내용

- Phase 1c에서 정리한 최소 service access/factory contract를
  **service-local access seam 중심**으로 정제하고 확장한다.
- API 계층은 service contract만 보고 concrete implementation detail은 보지 않는다.
- create/attach/start/stop/handler registration/query를 서비스 facade로 모은다.
- `service_public_api_guard_t`는 semantic service API가 아니라
  **public admission/close guard**로 두되,
  access seam 자체는 common hub로 끌어올리지 않는다.

### 산출물

- API 계층은 service contract만 알고 concrete service 조립 로직은 줄어든다.
- 서비스별 access/factory 경계가 코드 탐색 시 바로 드러난다.
- cross-service common access hub 없이 service-local seam으로 ownership이 설명된다.

### 금지 사항

- public API 계층이 서비스 내부 socket topology를 다시 직접 다루는 것
- 서비스별 특수 케이스가 `api/zlink.cpp`로 되돌아오는 것
- `services/common/service_access/*` 같은 새 cross-service hub를 만드는 것

### 완료 판정 기준

- service 추가 시 `api/zlink.cpp`, 해당 service-local access file, 해당 service 구현 파일을 넘는 추가 수정이 기본 경로가 되지 않는다.
- access seam 위치와 owner가 service-local seam + common guard 방향으로 일관된다.
- service concrete type 변경이 public API 계층의 direct include 변경으로 이어지지 않는다.

### 6.7 Phase 6: 서비스별 세부 분해

### 목적

공통 경계가 정리된 뒤 각 서비스 내부를
**현행 기능을 유지한 채** 의미 중심으로 다시 정리한다.

### 변경 내용

- `gateway`
  - routing 정책
  - runtime lifecycle
  - socket wiring
  - TLS/routing-id attach/query 경로 분리
- `discovery`
  - registry state/rules
  - discovery protocol encode/decode
  - runtime orchestration 분리
- `spot`
  - node orchestration
  - pub/sub data plane
  - internal receiver/dispatch
  - discovery-aware orchestration 분리

### 산출물

- 각 서비스 디렉터리에서 lifecycle, topology wiring, protocol/data-path가 구분된다.
- 대형 파일 분해가 helper 나열이 아니라 ownership 재정의로 읽힌다.

### 금지 사항

- 서비스별 대형 파일을 helper만 추가해 나누는 것
- control plane / data plane / lifecycle를 시간 순서대로만 쪼개는 것

### 완료 판정 기준

- `gateway/discovery/spot` 대형 파일 분해 후에도 공개 C API/ABI와 bindings 계약은 변하지 않는다.
- 한 서비스의 control-path 수정이 다른 서비스 디렉터리 수정으로 기본적으로 번지지 않는다.

## 7. 세부 설계 규칙

### 7.1 API 계층 규칙

- public C API는 validate + delegate를 기본으로 하고,
  공개 handle admission/lifetime 확인 같은 per-handle orchestration만 남긴다.
- `core/include/zlink.h`는 내부 convenience header가 아니라
  bindings를 포함한 외부 계약 헤더로 취급한다.
- concrete service 헤더 의존은 adapter 내부로 숨긴다.
- service/socket/protocol 내부 정책 분기는 API 계층에 남기지 않는다.

추가 규칙:

- monitor event wire decode, protocol parsing, concrete service branching은 API 계층에 두지 않는다.
- service-wide mode registry, poller registration table, handler registry는 API 계층 owner가 아니다.
- C function signature, callback typedef, 공개 enum/define 값은
  명시적 릴리스 설계 없이 바꾸지 않는다.
- 공개 C struct의 필드 배치와 크기에 영향 주는 변경은 금지한다.
- internal refactor 때문에 exported symbol 이름/존재 유무가 바뀌면 안 된다.

판정 질문:

```text
이 함수는 공개 입력을 검사한 뒤 내부 유스케이스를 호출하는가,
아니면 내부 조립 로직을 직접 수행하는가?
```

후자라면 API 계층에 남기면 안 된다.

### 7.2 Context / runtime 계층 규칙

- `ctx_t`는 socket registry, thread/poller ownership, endpoint registry,
  termination orchestration의 deep module이어야 한다.
- `io_thread_t`와 poller backend는 runtime core 바깥의 별도 execution backbone으로 드러나야 한다.
- 현재 코드 기준 ASIO backend 결합은 문서에서 숨기지 말고,
  runtime core가 어떤 engine/io backend 위에서 동작하는지 설명 가능해야 한다.
- service는 `ctx`의 세부 종료 메커니즘을 직접 몰라야 한다.
- runtime 계층은 close/drain/finalization contract를 제공하고,
  호출자는 그 내부 시퀀스를 몰라도 된다.

### 7.3 Socket 계층 규칙

- family 타입은 semantic owner다.
- runtime component는 common mechanism owner다.
- monitor, dispatch, endpoint bookkeeping은 common mechanism에 속한다.
- family는 routing/subscription/load-balancing/readiness 의미에 집중한다.

판정 질문:

```text
이 변경은 특정 socket family 의미 변화인가,
아니면 모든 socket에 공통인 기계 작업 변화인가?
```

답이 후자면 family 구현이 아니라 runtime 쪽에 있어야 한다.

### 7.4 Option 계층 규칙

- option은 "어디에 저장되는가"보다 "누가 해석하고 적용하는가"가 중요하다.
- transport option은 transport 근처에서,
  protocol option은 protocol 근처에서,
  service 전용 option은 service 근처에서 validate/apply한다.
- `options_t`는 무제한 확장 지점이 아니다.
- 단, 공개 option 번호와 관찰 가능한 의미는 bindings 계약이므로 유지한다.

새 option 추가 시 반드시 답해야 하는 질문:

1. owner는 누구인가
2. validate는 어디서 하는가
3. apply는 어디서 하는가
4. 어떤 계층은 이 option을 몰라도 되는가

### 7.5 Service 계층 규칙

- service runtime은 lifecycle coordinator다.
- service facade는 service 의미 API owner다.
- service access seam은 service-local seam으로 두고,
  공통화가 필요해도 guard/contract만 common에 둔다.
- `service_public_api_guard_t`는 service 의미 surface가 아니라
  public admission/close guard다.
- close/wait/drain mechanics는 runtime contract 아래로 숨긴다.
- internal socket topology는 public API 설명에서 제거한다.

즉 service는 아래처럼 읽혀야 한다.

```text
무슨 서비스를 제공하는가
어떤 lifecycle을 가지는가
무슨 상태를 외부에 노출하는가
```

반대로 아래는 내부로 숨긴다.

- 어떤 socket들이 내부에서 조합되는가
- 어떤 순서로 close/wait 되는가
- monitor/dispatch bridge가 어떻게 연결되는가

### 7.6 Utility 계층 규칙

- `utils`는 generic support만 둔다.
- socket/service/transport 특수 정책을 `utils`로 밀어 넣지 않는다.
- generic처럼 보이지만 실제 owner가 분명한 기능은 원래 도메인 근처에 둔다.

## 8. 테스트 및 검증 계획

### 8.1 기본 게이트

기본 검증은 아래 두 lane을 사용한다.

```bash
ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L integration -j1
```

Phase별 추가 게이트는 아래처럼 고정한다.

bindings 계약을 고려한 추가 검증 원칙:

- C API/ABI에 닿는 리팩토링 단계에서는 최소한 header/struct/symbol 영향 여부를 점검한다.
- 공개 C surface를 간접 소비하는 bindings smoke를 phase별 규칙에 따라 확인한다.
- release 준비 성격의 변경이면 `bindings/update_zlink_libs.sh` 흐름과 충돌이 없는지 확인한다.
- thread-safe teardown/close/drain 리스크가 큰 phase는 thread-safe stress를 의무 게이트로 둔다.

실행 명령은 아래 수준으로 고정한다.
아래 예시는 현재 저장소의 기본 개발 환경인 Linux 기준이다.

### core 기본 빌드/테스트

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L integration -j1
```

### perf baseline 기록

```bash
# 수정 없이 기준선만 기록한다.
mkdir -p core/perf/results
python3 core/perf/single/run_comparison.py \
  --build-dir core/build \
  --runs 1 \
  --duration 3 \
  --result-file core/perf/results/phase0-single-baseline.json
PERF_ALLOW_MULTI=1 python3 core/perf/run_comparison.py ALL \
  --build-dir core/build \
  --runs 1 \
  --duration 3 \
  --result-file core/perf/results/phase0-multi-baseline.json
```

기록 규칙:

- Phase 0에서는 대표 single baseline과 multi baseline을 실제로 1회씩 실행해 결과 파일을 남긴다.
- `phase0-single-baseline.json`은 single pattern baseline이다.
- `phase0-multi-baseline.json`은 `PERF_ALLOW_MULTI=1`을 명시한 multi pattern baseline이다.
- multi 실행이 불가능한 환경이면 `phase0-multi-baseline.json`을 생략하고, 실행 불가 사유를 작업 로그에 남긴다.
- perf 자체를 수정하지는 않지만, 리팩토링 전후 비교 가능한 결과 파일 또는 실행 로그를 남긴다.
- 최종 단계에서는 동일 조건으로 같은 명령을 다시 실행해 전후 결과를 비교한다.

### C 계약 변경 금지 확인

```bash
git diff -- core/include/zlink.h core/src/libzlink.vers
nm -D core/build/lib/libzlink.so | rg " zlink_"
```

해석 규칙:

- `core/include/zlink.h`, `core/src/libzlink.vers` diff가 생기면 이번 계획 범위를 벗어난다.
- `nm` 결과에서 기존 `zlink_*` export 누락이 보이면 리팩토링 실패다.

### thread-safe contract 검증

```bash
./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 100
./core/tests/run_thread_safe_contract_perf.sh --build-dir core/build --min-ratio 0.85
```

적용 규칙:

- Phase 2와 Phase 3 종료 시에는 `run_thread_safe_contract_stress.sh`를 **의무**로 수행한다.
- Phase 0과 최종 단계에서는 `run_thread_safe_contract_perf.sh` 결과도 함께 보관한다.
- TSan은 환경 비용이 크므로 상시 의무 게이트는 아니지만, close/drain 회귀가 의심되면 우선 추가한다.

### bindings smoke 검증

```bash
ROOT_DIR="$(pwd)"

bash bindings/cpp/build.sh ON
ctest --test-dir bindings/cpp/build --output-on-failure -R test_cpp_

ZLINK_LIBRARY_PATH="$ROOT_DIR/core/build/lib/libzlink.so" \
  dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -v minimal

(
  cd "$ROOT_DIR/bindings/java"
  chmod +x ./gradlew
  ZLINK_LIBRARY_PATH="$ROOT_DIR/core/build/lib/libzlink.so" \
    ./gradlew --no-daemon test integrationTest
)

(
  cd "$ROOT_DIR/bindings/node"
  ZLINK_LIB_PATH="$ROOT_DIR/core/build/lib/libzlink.so" \
    npx --yes node-gyp rebuild
  LD_LIBRARY_PATH="$ROOT_DIR/core/build/lib:${LD_LIBRARY_PATH:-}" \
    npm test
)

(
  cd "$ROOT_DIR/bindings/python"
  ZLINK_LIBRARY_PATH="$ROOT_DIR/core/build/lib/libzlink.so" \
    PYTHONPATH=src \
    python -m pytest -q tests
)
```

적용 규칙:

- 대표 bindings smoke는 `C++ + Node + Python` 조합으로 고정한다.
- full bindings smoke는 `C++ + .NET + Java + Node + Python` 전체 조합으로 고정한다.
- Phase 1a, 1b는 core 기본 게이트 + C 계약 확인을 기본으로 삼는다.
- Phase 1c는 core 기본 게이트 + C 계약 확인 + 대표 bindings smoke를 **의무**로 수행한다.
- Phase 2~4는 core 기본 게이트 + C 계약 확인을 기본으로 하되,
  Phase 2~3은 thread-safe stress를 **의무**로 수행한다.
- Phase 5~6 또는 release 전 검증에서는 full bindings smoke까지 포함한다.
- bindings smoke 실패 시 bindings 코드를 먼저 고치지 말고 `core` 리팩토링의 계약 침범 여부를 먼저 확인한다.
- Java/.NET/Python은 `ZLINK_LIBRARY_PATH`로 현재 `core/build` 산출물을 직접 가리킨다.
- Node는 addon 재빌드가 필요하므로 `ZLINK_LIB_PATH`로 현재 `core/build` 산출물을 링크한 뒤 검증한다.

### 8.2 단계별 검증 포인트

### Phase 1 이후

- handle validation 회귀 없음
- context/socket/message/service 공개 함수 동작 유지
- option routing, poller/monitor API 회귀 없음
- 1차 이후 추가된 service handle 및 callback surface 회귀 없음
- bindings가 직접 호출하는 대표 `zlink_*` 진입점 계약 회귀 없음
- Phase 1c 종료 시 대표 bindings smoke 통과

### Phase 2 이후

- send/recv 기본 동작 유지
- callback dispatch / send-ready handler 유지
- monitor event emission 유지
- close/stop/poller interaction 유지
- stream dispatch / spot dispatch / monitor snapshot 관련 현행 동작 유지
- `zlink_msg_t` / `zlink_routing_id_t` 관련 공개 사용 패턴 회귀 없음
- thread-safe stress 게이트 통과

### Phase 3 이후

- service shutdown timeout 정책 유지
- owned socket drain 계약 유지
- self-close / attach-query / teardown 관련 회귀 없음
- thread-safe stress 게이트 통과

### Phase 4 이후

- getsockopt/setsockopt round-trip 유지
- transport/TLS/routing/subscription 관련 옵션 적용 회귀 없음
- `monitor_event_version`, hello/disconnect/hiccup, `busy_poll` 계열 의미 유지
- option owner map과 validation/apply owner 재배치가 코드와 문서에서 함께 설명 가능

### Phase 5~6 이후

- `gateway`, `discovery`, `spot` 대표 integration 유지
- 서비스 create/attach/start/stop 경로 회귀 없음
- monitor / discovery / routing-id 관련 대표 시나리오 유지
- thread-safe teardown 및 service lifecycle 관련 대표 회귀 축 유지

### 8.3 테스트 추가 원칙

- 새 seam이 생기면 그 seam에 대한 unit test를 추가한다.
- 암묵 계약이 드러나면 `core/tests/`에 regression을 추가한다.
- retry/sleep 기반 완화는 금지한다.
- product bug를 test 약화로 숨기지 않는다.
- C API/ABI에 닿는 변경은 `core/tests/`만으로 끝내지 말고
  bindings 영향까지 함께 판단한다.

## 9. 완료 판정 기준

이번 2차 리팩토링이 성공했다고 볼 기준은 아래와 같다.

- `api/zlink.cpp`가 더 이상 가장 큰 내부 지식 허브가 아니다.
- `socket_base_t`는 semantic entrypoint로 읽히고,
  common mechanism은 분리된 runtime/component로 설명 가능하다.
- option owner가 분명해지고,
  option 변경이 unrelated 계층으로 덜 번진다.
- service runtime은 lifecycle coordinator로 설명되고,
  close/wait/drain 세부는 내부 contract로 숨겨진다.
- 서비스 변경이 public API 허브와 socket base 대형 파일을 전역 수정하게 만들지 않는다.
- 그 과정에서도 `core/include/zlink.h`와 bindings native 계약은 유지된다.
- `unittest`, `integration` 기본 게이트가 유지된다.
- 최종 병합 전 최소 1회는 bindings smoke 검증까지 통과한다.

## 10. 범위와 가정

- 범위는 `core/`다.
- `core/perf`, `core/bench`는 이번 리팩토링 직접 수정 범위가 아니다.
- 공개 API 호환성은 **현재 코드가 제공하는 현행 표면 기준으로** 유지한다.
- 그 공개 표면의 기준은 `core/include/zlink.h`와 exported `libzlink` surface이며,
  bindings가 이를 소비한다는 점을 전제로 한다.
- 기능 추가는 하지 않는다.
- 이번 문서는 1차 문서를 대체하지 않고,
  1차가 이미 반영된 현재 코드의 구조를 실행 관점으로 재정렬하는 2차 마스터 플랜이다.

## 11. 핵심 결정 문장

이번 문서의 결정을 짧게 요약하면 아래와 같다.

- public API는 얇은 facade여야 한다.
- C API/ABI는 bindings 계약이므로 내부 리팩토링보다 우선 보호한다.
- `socket_base_t`는 semantic entrypoint여야 한다.
- common socket mechanism은 runtime/component 아래로 내려가야 한다.
- option은 중앙 bag이 아니라 도메인 owner가 해석해야 한다.
- service runtime은 lifecycle coordinator여야 한다.
- close/drain/finalization은 공통 runtime contract로 설명돼야 한다.
