# `[07]` `core` 최종 소스 디렉터리 구조 초안

> 상태: draft
> 목적: POSD 리팩토링 완료 후 `core/src`의 목표 디렉터리 구조와 책임 정리

| nav | link |
| --- | --- |
| 목록 | [README](README.ko.md) |
| 이전 | [06 Review Log](06-core-system-review-log.ko.md) |
| 관련 | [00 상위 계획](00-core-system-posd-refactor-plan.ko.md), [04 Phase 2 Socket Runtime Split](04-core-system-phase2-socket-runtime-split.ko.md), [05 Phase 3 Engine/Transport/Service](05-core-system-phase3-engine-transport-service-plan.ko.md) |

## 1. 문서 목적

이 문서는 리팩토링이 끝났을 때
`core/src`가 **어떤 책임 경계로 보이게 될지**를 정리한 목표 구조 문서다.

중요:

- 이 문서는 "반드시 이 경로로 파일을 이동한다"는 강제 spec이 아니다.
- POSD 원칙상 **디렉터리보다 책임 경계가 먼저**다.
- 따라서 이 문서는 최종 구조를 **모듈 책임과 코드 탐색 기준**으로 정리한다.
- 실제 파일 이동은 Phase 7에서 정말 필요할 때만 수행한다.

즉 이 문서의 역할은
"최종적으로 어떤 소스 트리를 보면 구조 설명이 그대로 읽혀야 하는가"
를 고정하는 것이다.

## 2. 최종 해석 원칙

최종 `core/src`는 아래 흐름이 디렉터리 구조에도 드러나야 한다.

```text
API facade
  -> service facade / runtime
  -> socket semantic / socket runtime
  -> engine facade / pipeline
  -> transport adapter
  -> protocol codec
  -> core primitives
```

핵심 원칙:

- `api/`는 얇은 facade entry만 가진다.
- `services/`는 service 의미와 lifecycle을 가진다.
- `sockets/`는 semantic과 mechanism을 분리한다.
- `engine/`는 facade와 hot path policy를 분리한다.
- `transport/`는 scheme 세부를 adapter 아래로 숨긴다.
- `core/`는 primitive만 가진다.

## 3. 목표 디렉터리 구조 초안

아래 트리는 문서 기준의 **목표 배치 예시**다.
실제 파일명은 구현 단계에서 달라질 수 있지만,
책임 경계는 이 수준으로 맞아야 한다.

```text
core/src/
  api/
    zlink.cpp

  core/
    own.*
    reaper.*
    mailbox/*
    pipe/*
    poller/*

  sockets/
    base/
      socket_base.*
      socket_runtime.*
      endpoint_registry.*
      peer_state.*
      monitor_bridge.*
      dispatch_bridge.*
      lifecycle_hooks.*
    families/
      pair/*
      pubsub/*
      dealer/*
      router/*
      stream/*

  engine/
    facade/
      asio_engine.*
    pipeline/
      handshake/*
      ingress/*
      egress/*
      buffer_policy/*
      heartbeat/*
    codec/
      raw/*
      zmp/*

  transport/
    adapter/
      transport_adapter.*
      transport_factory.*
    tcp/*
    ipc/*
    ws/*
    wss/*
    tls/*

  services/
    common/
      service_runtime_base.*
      service_control_runtime.*
      monitor/*
      readiness/*
    discovery/*
    gateway/*
    spot/*
```

## 4. 디렉터리별 책임

### 4.1 `api/`

책임:

- 외부 호출 진입점
- opaque handle 생성/파괴 entry
- option/lifecycle/message API의 facade routing

가지면 안 되는 것:

- service internal topology 지식
- socket role 직접 분기
- transport scheme 조립 세부

해석:

- `zlink.cpp`는 더 이상 조립 허브가 아니다.
- public API의 semantic 이름만 유지하고, 내부 배선은 아래 계층으로 내린다.

### 4.2 `core/`

책임:

- `own`, `reaper`, mailbox, pipe, poller 같은 primitive
- 상위 계층이 공통으로 사용하는 기초 lifecycle / event / queue 메커니즘

가지면 안 되는 것:

- service 의미
- socket family 의미
- transport scheme 의미

해석:

- `core/`는 "무엇을 하는 서비스인가"를 몰라야 한다.
- primitive만 남아야 하며 policy는 위 계층으로 올라가면 안 된다.

### 4.3 `sockets/base/`

책임:

- socket 공통 mechanism
- endpoint registry
- peer state
- monitor bridge
- dispatch bridge
- quiesce / lifecycle hooks

가지면 안 되는 것:

- `PAIR`, `ROUTER`, `STREAM` 등의 family-specific 의미
- service-specific topology

중요 제약:

- `socket_runtime`은 단일 mega-class가 아니다.
- `endpoint_registry`, `peer_state`, `monitor_bridge`,
  `dispatch_bridge`, `lifecycle_hooks`는 독립 component로 유지한다.

해석:

- `sockets/base/`는 mechanism layer다.
- family 구현이 알아야 할 개념 수를 줄이는 것이 목적이다.

### 4.4 `sockets/families/`

책임:

- family-specific semantic
- routing / subscription / load-balancing 의미
- family-specific poll/read/write 반응

가지면 안 되는 것:

- generic endpoint 저장 구조
- monitor wire format
- callback subject 저장 세부

해석:

- `socket_base_t`와 family 코드는 semantic 중심으로 읽혀야 한다.
- mechanism이 다시 family 안으로 새면 실패다.

### 4.5 `engine/facade/`

책임:

- connection lifecycle facade
- start / stop / ingress / egress / state notify

가지면 안 되는 것:

- speculative I/O policy 세부
- buffer growth policy 세부
- handshake 세부 단계

해석:

- `asio_engine_t`는 facade처럼 읽혀야 한다.
- 내부 hot path policy는 `pipeline/` 아래로 내려간다.

### 4.6 `engine/pipeline/`

책임:

- handshake
- ingress / egress completion 흐름
- buffer policy
- heartbeat / timer
- speculative I/O / gather write 정책

가지면 안 되는 것:

- service-specific 의미
- transport scheme 선택 정책

해석:

- 성능 민감 정책은 여기서 닫혀야 한다.
- facade가 pipeline 세부를 다시 알기 시작하면 실패다.

### 4.7 `engine/codec/`

책임:

- raw / zmp frame boundary
- wire encoding / decoding

가지면 안 되는 것:

- transport open logic
- connection lifecycle

해석:

- codec은 frame 의미까지만 알고, channel이나 endpoint는 몰라야 한다.

### 4.8 `transport/adapter/`

책임:

- transport adapter facade
- channel abstraction
- transport factory
- scheme-specific open/connect/listen 선택의 상위 contract

가지면 안 되는 것:

- service-specific option 의미
- engine hot path policy

해석:

- 상위는 capability를 보고, scheme 조립 세부는 adapter 아래로 숨겨야 한다.

### 4.9 `transport/{tcp,ipc,ws,wss,tls}/`

책임:

- scheme-specific mechanism
- async primitive
- handshake layering 세부

가지면 안 되는 것:

- 상위 API 의미
- service topology 의미

해석:

- transport별 특수성은 이 아래에서 닫혀야 한다.

### 4.10 `services/common/`

책임:

- 공통 lifecycle runtime
- control runtime
- readiness / monitor 공통 의미

가지면 안 되는 것:

- gateway/spot/discovery별 topology 세부
- concrete socket close mechanics

해석:

- `service_runtime_base`는 coordinator이지 closer가 아니다.
- 공통 service lifecycle 언어를 제공하는 deep module이어야 한다.

### 4.11 `services/discovery/`

책임:

- discovery service 의미 API
- registry connect / registration / query 의미
- discovery-specific runtime / topology

가지면 안 되는 것:

- `socket_role` 기반 public API
- observer / summary / dealer 내부 wiring을 외부에 노출하는 surface

해석:

- dynamic dealer lifecycle은 internal runtime 책임이다.
- option은 socket role이 아니라 discovery semantic 기준으로 재설계한다.

### 4.12 `services/gateway/`

책임:

- gateway service 의미 API
- routing / LB / discovery attach 의미
- gateway-specific runtime / topology

가지면 안 되는 것:

- `router()` 같은 internal accessor
- runtime struct 직접 노출

해석:

- gateway는 router socket을 직접 노출하지 않고 service 의미로 설명 가능해야 한다.

### 4.13 `services/spot/`

책임:

- spot node / pub / sub 의미 API
- attachment / peer connect / subscription 의미
- spot-specific runtime / topology

가지면 안 되는 것:

- `runtime()`, `ctx()` 같은 internal accessor
- control plane helper를 public API처럼 노출

해석:

- spot은 child semantics를 유지하되 internal receiver/control topology는 숨겨야 한다.

## 5. 현재 트리와의 주요 차이

현재 구조 대비 최종 구조의 핵심 차이는 아래다.

```text
현재                                    최종
──────────────────────────────────────  ──────────────────────────────────────
api/zlink.cpp가 조립 허브                 api/zlink.cpp는 얇은 facade entry
socket_base_t에 mechanism 집중           sockets/base/* 로 mechanism 분리
asio_engine_t에 policy 집중              engine/facade + engine/pipeline 분리
transport 세부가 상위에 새어 나옴        transport/adapter 아래로 수렴
service별 topology + lifecycle 혼재      services/common + per-service runtime 분리
public/internal surface 섞임             semantic API만 남기고 나머지는 internalize/remove
```

## 6. 파일 이동 원칙

이 문서는 최종 디렉터리 구조를 제시하지만,
실제 구현에서는 아래 원칙을 따른다.

- ownership 정리가 끝나기 전에는 대규모 파일 이동을 하지 않는다.
- Phase 2/3에서 책임 경계가 먼저 확정된 뒤 파일을 옮긴다.
- 기존 파일 안에서 책임 분리로 해결 가능한 경우, 이동보다 분리를 우선한다.
- Phase 7에서만 rename / directory move를 최종 정리한다.

즉 이 트리는 **최종 상태의 목표 모습**이지,
각 phase에서 바로 만들어야 하는 중간 상태가 아니다.

## 7. 구현 체크 질문

실제 구현 중 디렉터리/파일 이동이 필요할 때는 아래를 먼저 묻는다.

1. 이 이동이 상위가 알아야 할 개념 수를 줄이는가?
2. 이 파일은 semantic인가 mechanism인가?
3. 이 코드는 service/gateway/spot/discovery 중 어디 의미에 속하는가?
4. 이 코드는 engine policy인가 transport mechanism인가?
5. primitive여야 할 코드가 service 의미를 알기 시작하지 않았는가?
6. 디렉터리만 바뀌고 책임은 그대로인 얕은 이동이 아닌가?

하나라도 애매하면 파일 이동보다 책임 재정의를 먼저 한다.

## 8. 완료 판정

최종 구조 문서 기준으로 아래가 보이면 완료에 가깝다.

- `api/ -> services/ -> sockets/ -> engine/ -> transport/ -> core/`
  흐름이 코드 탐색만으로 보인다.
- `socket_base_t`, `asio_engine_t`, `service_runtime_base_t`가 더 이상 허브 타입처럼 읽히지 않는다.
- 새 socket family / transport / service 기능 추가 시 수정 범위가 해당 서브트리 아래로 국소화된다.
- internal concept API가 facade 밖으로 새지 않는다.

## 9. 한 줄 요약

최종 `core/src`는
"예쁜 디렉터리 트리"가 아니라,
**책임 중심 단어만 봐도 ownership, hot path, topology hiding이 읽히는 트리**
가 되어야 한다.
