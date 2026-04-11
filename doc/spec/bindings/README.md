[Spec Index](../README.md)

# Bindings API Policy

> request-reply 와 SPOT routed 구현 기준은
> [`doc/plan/spot-refactor`](../../plan/spot-refactor) 아래 문서를 따른다.
> 언어별 인터페이스 시그니처와 사용 예는
> `cpp/`, `java/`, `dotnet/`, `node/`, `python/`, `go/`, `rust/` 를 참조한다.

## 목적
이 문서는 `bindings/` 전체의 public API 정책을 정의한다.

이 문서의 목적은 각 언어 바인딩이 제각각 다른 표면과 예외 규칙을 갖는 것을
막고, `core/include/zlink.h`를 기준으로 설명 가능하고 일관된 공통 계약을
강제하는 데 있다.

`cpp/`, `java/`, `dotnet/`, `node/`, `python/`, `go/`, `rust/` 아래 문서는
각 바인딩 구현이 실제로 외부에 제공해야 하는 public API contract를 정의한다.
이 문서들이 규정하는 것은 공개 타입, 메서드, 시그니처, 반환값, 오류 의미이며,
바인딩 구현이 노출하는 public 인터페이스는 이 계약과 달라지면 안 된다.
다만 이 문서는 실제 구현 클래스 계층이나 내부 파일 구조까지 규정하지는 않는다.
각 바인딩은 같은 공개 계약을 유지하는 범위에서 내부 구조를 언어 관례에 맞게
자유롭게 설계할 수 있다.

이 문서는 단순 스타일 가이드가 아니다. 다음을 위한 설계 기준 문서다.
- public API 설계 기준
- 리뷰 기준
- 리팩터링 기준
- 샘플과 테스트 기준

이 문서의 의도는 다음과 같다.
- 언어별로 이름만 비슷하고 의미가 다른 API를 없앤다.
- 같은 능력을 여러 방식으로 중복 노출하는 얕은 표면을 없앤다.
- raw option bag, 불필요한 편의 래퍼, 암묵적 ownership, 숨은 오류 경로를
  줄인다.
- binding 사용자가 internal sequencing, native 세부사항, hidden transport
  switch를 알지 않아도 되게 만든다.
- POSD 원칙에 맞는 깊은 모듈과 낮은 변경 파급(change amplification) 구조를 유도한다.
- correctness뿐 아니라 비용 모델, 샘플 품질, 테스트 가능성까지 공통 기준으로
  묶는다.

기준은 항상 `core/include/zlink.h` 이다. 각 바인딩은 코어 계약을 따르되,
표현 방식은 언어 관례에 맞게 선택할 수 있다. 다만 의미 계약은 바뀌면 안 된다.

이 문서는 “각 언어가 어떻게 보일 수 있는가”보다 “각 언어가 무엇을 보장해야
하는가”를 정의한다.

## 문서 해석 규칙
- 이 문서의 정책 본문은 기본적으로 규범 문서다.
- 아래 용어는 다음 의미로 해석한다.
  - `Required`: 현재 리뷰와 구현에서 반드시 지켜야 하는 항목.
    미준수 시 리뷰에서 차단된다.
  - `Recommended`: 강하게 권장하지만, 바인딩 특성에 따라 단계적으로 적용할
    수 있는 항목. 미준수 시 리뷰에서 사유를 요구하지만 차단하지 않는다.
  - `Target`: 장기적으로 맞춰가야 하는 목표 항목. 해당 바인딩이 이
    컴포넌트를 구현하기로 결정한 경우에만 적용된다. 구현하지 않기로
    결정한 경우 리뷰에서 요구하지 않는다.
- 별도 표시가 없으면 정책 본문은 `Required`로 본다.
- 섹션 제목에 `(Target)` 또는 `(Recommended)`가 표시된 경우, 해당 섹션
  전체는 표시된 수준으로 해석한다. 무표시 기본값(`Required`)보다 우선한다.
- `Non-Normative Backlog: Implementation Follow-Ups` 섹션은 규범 본문이 아니라
  비규범 backlog다.
- backlog 항목은 현재 미준수 가능성을 추적하기 위한 것이며, 문서 본문의 의미 계약을 대체하지 않는다.

## 핵심 원칙
- 코어 계약은 `zlink.h`가 단일 기준이다.
- public API는 multipart 모델을 기준으로 설계한다.
- blocking과 non-blocking은 이름으로 구분한다.
- 동일한 능력을 여러 방식으로 중복 노출하지 않는다.
- 값의 의미는 `int`가 아니라 enum, boolean, value object로 올린다.
- raw option bag은 public에 노출하지 않는다.
- 바인딩은 코어의 상태 오류를 추론하지 않는다.
- 입력 값의 형식, 범위, overflow, truncation 위험은 바인딩이 먼저 막는다.
- 구조는 POSD 원칙에 따라 깊은 모듈, 정보 은닉, 낮은 변경 파급을
  우선한다.
- 이 문서는 의미 계약을 우선 정의한다.
- 언어별 표면은 각 언어 관례에 맞게 달라질 수 있지만, 의미 계약은 같아야
  한다.

## Monitor Ready Contract
- `*_READY_CHANGED` monitor event 의 `value` 는 aggregate ready count 계약이 아니다.
- binding public API는 monitor snapshot 에 ready-count surface 가 있다고
  가정하면 안 된다.
- readiness gate 가 필요하면 low-cost event edge 를 직접 사용해야 한다.
- raw perf/샘플은 `CONNECTION_READY` event counting 을 사용한다.
- SPOT perf/샘플은 service monitor gate 를 사용하지 않는다.
- SPOT perf 는 explicit `READY/START` barrier protocol 을 사용한다.
- delivery-ready/count 계열 monitor event 를 새 gate contract 로 만들면 안 된다.

## POSD Structure Policy
- 바인딩 설계는 John Ousterhout의 POSD 원칙을 따른다.
- public API는 사용자가 알아야 할 개념 수를 줄여야 한다.
- 내부 구현 복잡도는 facade, value object, domain object 뒤로 숨겨야 한다.
- 얕은 래퍼(shallow wrapper)는 지양한다.
  - 단순히 native 함수 이름만 바꾸고 새 의미를 추가하지 못하는 public
    wrapper는 늘리지 않는다.
- 같은 능력을 여러 타입과 여러 이름으로 반복 노출하지 않는다.
- 변화가 한 곳에서 끝나야 할 규칙은 한 모듈에 모은다.
  - 예: routing id 길이 제한
  - 예: send failure contract
  - 예: typed option ownership
- 시간 순서에 의존하는 분해(temporal decomposition)를 줄인다.
  - 예: 사용자가 `setOption` 조합 순서를 기억해야 하는 API 금지
- public API는 “무엇을 할 수 있는지”를 드러내고, “내부에서 어떻게 배선되는지”를
  드러내지 않아야 한다.
- 값 객체와 결과 객체는 깊은 모듈로 취급한다.
  - 호출자에게는 작은 인터페이스를 주고, 내부에서는 검증, ownership, shape
    규칙을 함께 캡슐화해야 한다

## Public Surface Rules

### Base Type Exposure
- 가능하면 컴파일 단계에서 사용자가 concrete socket type만 직접 쓰게 해야 한다.
- 사용자가 generic root base, raw compat base, shared base를 concrete socket
  type 대신 직접 쓰는 구조는 피한다.
- static typed binding은 public type/export/visibility를 이용해 이 규칙을
  강제해야 한다.
- dynamic binding은 export 제한과 surface test로 같은 규칙을 강제해야 한다.
- generic root base 또는 raw compat base는 공통 lifecycle과 공통 관리 기능만
  외부에 노출한다.
- capability-specific shared base는 모든 descendant가 공통으로 가지는 능력만
  외부에 노출할 수 있다.
- socket-type-specific capability를 generic root base나 raw compat base로
  올리면 안 된다.
- public base에서 외부 접근을 허용해도 되는 공통 기능 예:
  - `bind`, `unbind`
  - `connect`, `disconnect` on connectable base only
  - `close` / `dispose`
  - common typed options
  - `monitorOpen` 또는 동등한 monitor 진입점
- generic root base 또는 raw compat base에서 외부 접근을 허용하면 안 되는 기능:
  - `send(...)`
  - `trySend(...)`
  - `send(routingId, ...)`
  - `trySend(routingId, ...)`
  - `send(..., flags)`
  - `sendParts(...)`
  - `sendFrom(...)`
  - `recv()`
  - `tryRecv()`
  - `recv(flags)` / `recv(size, flags)`
  - `recvInto(...)`
  - `recvMsgInto(...)`
  - routed receive alias (`receiveRouted`, `tryReceiveRouted` 등)
  - `publish(...)`
  - `tryPublish(...)`
  - `setSubscription(...)`
  - `unsetSubscription(...)`
  - `subscribe()`
  - `trySubscribe()`
  - `receiveSubscriptionEvent()`
  - `tryReceiveSubscriptionEvent()`
  - `onReceive(...)`
  - `onSubscribe(...)`
  - `onSendReady(...)`
  - `setRoutingId(...)`, `getRoutingId()`
  - `attachDiscovery(...)`
  - `attachStreamRaw(...)`, `detachStream()`
  - `streamAttach(...)`, `streamAttachRaw(...)`, `streamDetach()`
  - `streamPeerRoutingId(...)`, `streamSend(...)`
  - raw option bag (`setOption`, `getOption`, `setSockOpt`, `getSockOpt` 등)
  - topic/socket-type-specific option facade
  - canonical 이름을 우회하는 legacy alias
    - 예: `recvHandler(...)`, `subscribeHandler(...)`
- capability-specific shared base는 descendant 전부에 공통인 capability에 한해
  허용할 수 있다.
  - 예: subscriber-only base의 `setSubscription`, `unsetSubscription`,
    `subscribe`, `trySubscribe`, `onSubscribe`
  - 예: publisher-only base의 `publish`, `tryPublish`, `onSendReady`
  - 예: discovery-capable socket base의 `attachDiscovery`
- 위 capability는 capability matrix에서 `Y`인 concrete socket type에만
  public으로 존재해야 한다.
- capability matrix에서 `—`인 socket type에 대해 base 경유 우회 호출이 가능하면
  안 된다.
- perf, sample, helper, compat layer도 canonical public surface 규칙을
  우회하는 base entry를 새 기준처럼 사용하면 안 된다.
- deprecated compat API가 필요하더라도 canonical public API와 분리된 compat
  namespace 또는 internal surface로 격리한다.
- 사용자가 `SocketType`과 raw flag 조합을 기억해서 올바른 send/recv 계열을
  선택해야 하는 구조는 POSD 위반으로 본다.

### Multipart Only
- send/receive public surface는 multipart 기준으로 통일한다.
- 단일 메시지 수신 편의 오버로드는 public에 두지 않는다.
- 단일 part 전송 편의 메서드는 허용할 수 있다.
  - 예: `send(Message part)`는 `send(List<Message> parts)`의 간편 오버로드
- 수신 결과는 언어에 맞는 도메인 객체 또는 동등한 multipart 표현으로
  반환한다.

### Blocking vs Non-Blocking
- blocking API는 기본 동작 이름을 사용한다.
  - 예: `send`, `recv`, `publish`, `subscribe`,
    `receiveSubscriptionEvent`
- non-blocking API는 `try*` 이름을 사용한다.
  - 예: `trySend`, `tryRecv`, `tryPublish`, `trySubscribe`,
    `tryReceiveSubscriptionEvent`
- public `flags` 파라미터로 blocking/non-blocking을 전환하지 않는다.
- `SendFlag` / `ReceiveFlag` 같은 transport switch는 internal helper로만
  사용할 수 있다.

### Explicit Non-Blocking Send Outcome
- non-blocking send 계열은 `bool` 하나로 성공/실패를 숨기지 않는다.
- send 결과는 `SendResult` 같은 명시적 enum으로 노출한다.
- 표준 send 결과 enum:

```text
Sent
Backpressured
NotReady
```

- 바인딩은 `core` 스펙에 없는 추가 errno 추정으로 `Backpressured`와
  `NotReady`를 구분하지 않는다.
- 현재 `zlink.h`는 non-blocking send 전용 결과 API를 따로 제공하지 않는다.
- 따라서 바인딩은 `core` 스펙에 문서화된 non-blocking send errno를
  `SendResult`로 고정 매핑해 public API로 노출할 수 있다.
- 이 매핑은 바인딩 내부에서 임의로 바뀌면 안 되며, 언어별 스펙에 명시되어야 한다.

### Send Failure Contract
- blocking send 계열:
  - `send`, `publish`, routed `send`
  - 성공 시 정상 반환
  - 실패 시 반드시 예외 또는 언어별 오류 경로로 전달한다
  - 실패를 `false`, `null`, empty result로 숨기지 않는다
- non-blocking send 계열:
  - `trySend`, `tryPublish`
  - `Backpressured`, `NotReady`는 정상 결과값으로 반환한다
  - `EAGAIN` 계열 외의 오류는 반드시 예외 또는 언어별 오류 경로로 전달한다
  - 문서화된 send-result 매핑 외의 오류를 임의로 삼키면 안 된다
- binding helper, wrapper, sample 코드도 blocking send 실패를 무시하거나
  무시하면 안 된다

### Receive Outcome
- non-blocking receive 계열은 “데이터 없음”만 정상 경로로 표현한다.
- 언어별 canonical 표현은 언어 관례를 따른다.
- 예:
  - Java: `Optional<T>`
  - .NET: `bool TryReceive(out ...)`
  - Go: `(T, bool)` 또는 `(T, error)`
  - Rust: `Option<T>`
  - Node/Python: empty/null/None 계열
- `EAGAIN` 외 오류는 예외 또는 언어별 오류 경로를 유지한다.

## Domain Object Policy
- Java, C#, Go, Rust, Node, Python은 가능하면 `out` 파라미터나 raw tuple보다
  도메인 객체를 우선한다.
- 최소 핵심 도메인 모델:
  - `Message`
  - `RoutingId`
  - `Received`
  - `TopicMessage`
  - `SubscriptionEvent`
  - `SendResult`
- 결과 객체는 payload shape, ownership, optional routing metadata를 함께
  설명해야 한다.
- 편의 기능은 결과 객체 메서드로 둔다.
  - 예: `singlePartOrThrow()`

## Socket Type Capability Policy
- 소켓 타입별 능력은 타입 자체에만 노출한다.
- 관련 없는 소켓은 관련 없는 함수에 접근할 수 없어야 한다.
  - 예: `PairSocket`에 publish/subscribe/xpub control surface 금지
  - 예: `StreamSocket`에 일반 connect surface 금지
- 소켓 타입별 option도 타입별 capability facade로만 노출한다.

### Socket Capability Matrix
- 이 표는 `core/include/zlink.h` C API를 기준으로 각 소켓 타입이 가져야 할
  능력을 정의한다.
- 각 바인딩은 이 표를 정답으로 삼아 surface test를 작성한다.
- `Y`는 해당 능력을 반드시 public API로 노출해야 함을 의미한다.
- `—`는 해당 능력을 public API로 노출하면 안 됨을 의미한다.

#### Connection Capabilities

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `bind` | Y | Y | Y | Y | Y | Y | Y | Y |
| `unbind` | Y | Y | Y | Y | Y | Y | Y | Y |
| `connect` | Y | Y | Y | Y | Y | Y | Y | — |
| `disconnect` | Y | Y | Y | Y | Y | Y | Y | — |

#### Send Capabilities

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `send` / `trySend` | Y | Y | — | — | — | — | — | — |
| `send(routingId)` / `trySend(routingId)` | — | — | Y | — | — | — | — | Y |
| `publish` / `tryPublish` | — | — | — | Y | — | Y | — | — |

#### Receive Capabilities

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `recv` / `tryRecv` | Y | Y | Y | — | — | — | — | Y |
| `subscribe` / `trySubscribe` | — | — | — | — | Y | — | Y | — |
| `receiveSubscriptionEvent` / `tryReceiveSubscriptionEvent` | — | — | — | — | — | Y | — | — |

#### Subscription Management

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `setSubscription` | — | — | — | — | Y | — | Y | — |
| `unsetSubscription` | — | — | — | — | Y | — | Y | — |

#### Callback Capabilities

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `onReceive` | Y | Y | Y | — | — | — | — | Y |
| `onSubscribe` | — | — | — | — | Y | — | Y | — |
| `onSendReady` | Y | Y | Y | Y | — | Y | — | Y |

#### Typed Option Capabilities

| Option Facade | 적용 소켓 |
|---|---|
| Common options (linger, HWM, timeout 등) | 전체 |
| Router options (mandatory, handover, probe, connectRoutingId) | Router |
| Dealer options (probe) | Dealer |
| Stream options (notify) | Stream |
| Pub options (verbose, verboser, noDrop, manual 등) | Pub, XPub |
| Sub options (topicsCount) | Sub, XSub |
| RoutingId (set/get) | Dealer, Router, Stream |
| `attachDiscovery` | Dealer, Router, Pub, Sub |

- `attachDiscovery` 후 해당 소켓에서 `connect`, `disconnect`, `unbind`,
  `close`는 차단된다. Discovery `close`가 소켓 lifecycle을 관리한다.

## Language Spec File Compliance Rules

각 언어별 스펙 파일(`doc/spec/bindings/{lang}/README.md`)은 아래 규칙을
반드시 준수해야 한다. 스펙 파일 작성이나 리뷰 시 이 체크리스트를 적용한다.

### Capability Matrix 정합성
- 각 소켓 타입 클래스는 위 Socket Capability Matrix에서 `Y`인 능력만
  public 메서드로 노출해야 한다.
- `—`인 능력은 해당 소켓 타입 클래스에 존재하면 안 된다.
- 특히 다음 위반이 자주 발생하므로 주의한다:
  - `RouterSocket` / `StreamSocket`에 plain `send` (routingId 없는 send) 금지 —
    반드시 `send(routingId, ...)` / `trySend(routingId, ...)` 형태여야 한다.
  - `PairSocket` / `XPubSocket` / `StreamSocket` / `XSubSocket`에 `attachDiscovery` 금지 —
    Dealer, Router, Pub, Sub에만 허용된다.
  - `XPubSocket`에 `onSubscribe` 콜백 금지 —
    XPub는 `receiveSubscriptionEvent` / `tryReceiveSubscriptionEvent`만 허용된다.

### Routed Send 필수 인자
- `RouterSocket`과 `StreamSocket`의 send는 routingId를 **필수** 인자로 받아야 한다.
- routingId를 optional/default 파라미터로 만들면 plain send가 가능해지므로 금지한다.

### Blocking Send 반환값
- blocking `send` / `publish`는 성공 시 반환값 없이 정상 반환하고,
  실패 시 예외 또는 언어별 오류 경로로 전달해야 한다.
- 상태 코드(int, number 등)를 반환하는 방식은 금지한다.

### 언어별 네이밍 일관성
- 한 바인딩 내에서 네이밍 컨벤션이 혼재되면 안 된다.
  - Python: 모든 public API는 `snake_case`. (프로퍼티 포함)
  - Java: `camelCase` 메서드, `PascalCase` 클래스.
  - C#: `PascalCase` 전체.
  - Go: `PascalCase` exported.
  - Rust: `snake_case` 메서드, `PascalCase` 타입.
  - C++: `snake_case` 메서드, `_t` 접미사 타입.
  - Node/TypeScript: `camelCase` 메서드, `PascalCase` 클래스.

### C API 전수 커버리지
- 각 언어별 스펙 파일은 `core/include/zlink.h`의 모든 ZLINK_EXPORT 함수에
  대응하는 바인딩 인터페이스를 빠짐없이 기술해야 한다.
- 대응은 1:1이 아닐 수 있다 (옵션 함수 그룹이 하나의 typed facade로 통합되는 등).
- 그러나 C API의 어떤 기능도 바인딩 스펙에서 누락되면 안 된다.
- 새로운 C API가 `zlink.h`에 추가되면 모든 언어 스펙 파일도 함께 갱신해야 한다.

## Service Layer Policy
- 이 섹션은 소켓 레이어 위에 올라가는 서비스 계층(Spot, Discovery, Registry)의
  public API 정책을 정의한다.
- 서비스 계층도 소켓 계층과 동일한 POSD 원칙, naming policy, error policy,
  ownership policy, testing policy를 따른다.
- 서비스 계층의 기준은 `core/include/zlink.h`의 Spot/Discovery/Registry C API다.

### Service Layer Architecture
- 서비스 계층은 다섯 개의 컴포넌트로 구성된다.

```
Registry (서버)
  ├── bind (PUB + ROUTER endpoint)
  ├── cluster: addPeer (다른 Registry와 동기화)
  ├── config: setId, setHeartbeat, setBroadcastInterval
  └── introspection: statusSnapshot, serviceSummarySnapshot,
      memberPeers, topologySnapshot, topologyQuery

Discovery (클라이언트 — 서비스 뷰)
  ├── connectRegistry (Registry에 연결)
  ├── metadata: setValue/getValue, setMetadata/getMetadata
  ├── introspection: memberPeers, memberPeerMetadata
  └── lifecycle: destroy → 연결된 모든 participant 종료

SpotNode (토폴로지 런타임)
  ├── bind (endpoint)
  ├── mesh: connectPeer / disconnectPeer
  ├── attachDiscovery (Discovery에 위임)
  ├── introspection: statusSnapshot, peersSnapshot, peersQuery,
  │   subjectsSnapshot
  └── TLS: setTlsServer, setTlsClient

Spot (pub/sub facade — SpotNode 위에 올라감)
  ├── publish / tryPublish
  ├── subscribe / trySubscribe
  ├── setSubscription / unsetSubscription
  ├── onSubscribe, onSendReady
  └── close (node는 살아 있음)

RegistryQueryClient (원격 토폴로지 조회)
  ├── connect (Registry endpoint)
  ├── snapshot (필터 기반 조회)
  └── close
```

### SpotNode Capability Matrix

| Capability | SpotNode |
|---|---|
| `bind` | Y |
| `connectPeer` | Y (discovery 미연결 시만) |
| `disconnectPeer` | Y (discovery 미연결 시만) |
| `attachDiscovery` | Y |
| `setTlsServer` | Y |
| `setTlsClient` | Y |
| `statusSnapshot` | Y |
| `peersSnapshot` | Y |
| `peersQuery` | Y |
| `subjectsSnapshot` | Y |
| `close` | Y |

- SpotNode는 data plane API(`send`/`recv`/`publish`/`subscribe`)를 직접
  노출하지 않는다.
- data plane은 `Spot` facade를 통해서만 접근한다.
- `connectPeer`/`disconnectPeer`는 discovery가 연결되면 EFSM으로 차단된다.

### Spot Capability Matrix

| Capability | Spot |
|---|---|
| `publish` / `tryPublish` | Y |
| `subscribe` / `trySubscribe` | Y |
| `setSubscription` / `unsetSubscription` | Y |
| `onSubscribe` | Y |
| `onSendReady` | Y |
| `close` | Y |

- Spot은 소켓 타입이 아니라 SpotNode 위에 올라가는 pub/sub facade다.
- Spot은 `recv`/`tryRecv`, `send`/`trySend`, `onReceive`를 갖지 않는다.
- Spot은 `bind`/`connect`를 갖지 않는다 (SpotNode가 담당).
- Spot `close`는 facade만 해제하고 SpotNode는 살아 있다.

### Discovery Capability Matrix

| Capability | Discovery |
|---|---|
| `connectRegistry` | Y |
| `setValue` / `getValue` | Y |
| `setMetadata` / `getMetadata` | Y |
| `memberPeers` | Y |
| `memberPeerMetadata` | Y |
| `close` | Y |

- Discovery는 생성 시 `serviceType`과 `serviceName`을 고정한다.
- 이후 변경할 수 없다.
- `close` 시 연결된 모든 participant(SpotNode 등)가 종료된다.
- Discovery는 data plane이 아니라 서비스 등록/발견 plane이다.

### Registry Capability Matrix (`Target`)
- 이 matrix는 `Target`이다. 전체 바인딩 필수가 아니며, 구현하는 바인딩만
  아래 표를 따른다.

| Capability | Registry |
|---|---|
| `bind` (pubEndpoint, routerEndpoint) | Y |
| `setId` | Y |
| `addPeer` | Y (클러스터 동기화) |
| `setHeartbeat` (interval, timeout) | Y |
| `setBroadcastInterval` | Y |
| `statusSnapshot` | Y |
| `serviceSummarySnapshot` | Y |
| `memberPeers` | Y |
| `memberPeerMetadata` | Y |
| `topologySnapshot` | Y |
| `topologyQuery` | Y |
| `close` | Y |

- Registry는 서버 측 컴포넌트다.
- PUB endpoint(서비스 목록 브로드캐스트)와 ROUTER endpoint(등록 수신)를
  동시에 바인드한다.
- cluster 모드에서는 `addPeer`로 다른 Registry와 동기화한다.

### RegistryQueryClient Capability Matrix (`Target`)
- 이 matrix는 `Target`이다. 전체 바인딩 필수가 아니며, 구현하는 바인딩만
  아래 표를 따른다.

| Capability | RegistryQueryClient |
|---|---|
| `connect` | Y |
| `snapshot` (필터 기반) | Y |
| `close` | Y |

- 원격에서 Registry 토폴로지를 조회하기 위한 클라이언트다.
- Discovery와 별개로 사용할 수 있다.

### Service Monitor Policy
- Discovery만 ServiceMonitor를 열 수 있다.
- SPOT(SpotNode, Spot)은 ServiceMonitor를 노출하지 않는다.
  SPOT 관찰은 `statusSnapshot`, `peersSnapshot`, `subjectsSnapshot` API를 사용한다.
- ServiceMonitor는 소켓의 SocketMonitor와 별도 타입이다.
- ServiceMonitor API:
  - `recv()` / `tryRecv()`: blocking/non-blocking event 수신
  - `onEvent(handler)`: callback 등록
  - `snapshot()`: 현재 상태 스냅샷
  - `close()`
- ServiceMonitor event는 typed event surface로 노출해야 한다.
- raw int event mask만 노출하면 안 된다.
- ServiceMonitor `onEvent` callback 해제 정책:
  - 소켓 callback과 동일 — `null`/`None` 설정 해제 금지
  - callback 해제는 `close()`로만 이루어진다
- SocketMonitor callback 해제 정책도 동일하다.
  - SocketMonitor는 callback 등록 API가 없으면 해당 없음
  - callback 등록 API가 있는 경우 `close()`로만 해제한다
- ServiceMonitor event 종류 (Discovery 전용):
  - `error`, `serviceUp`, `serviceDown`, `providersChanged`, `closed`

### Service Layer Domain Objects
- 서비스 계층도 domain object를 사용해야 한다.
- 최소 핵심 domain object:
  - `ServiceEvent`: ServiceMonitor에서 수신하는 이벤트
  - `MonitorSnapshot`: monitor 상태 스냅샷
  - `SpotNodeStatus`: SpotNode 상태 (state, peer count 등)
  - `SpotNodePeerEntry`: peer 정보
  - `SpotNodeSubjectEntry`: subject 정보
  - `RegistryStatus`: Registry 상태
  - `MemberPeerEntry`: 서비스 멤버 peer 정보
  - `RegistryTopologyEntry`: 토폴로지 엔트리
- 필터 객체:
  - `SpotNodePeerFilter`: peer 조회 필터
  - `SpotNodeSubjectFilter`: subject 조회 필터
  - `RegistryServiceSummaryFilter`: 서비스 요약 조회 필터
  - `RegistryTopologyFilter`: 토폴로지 조회 필터
- enum/value object:
  - `ServiceType`: `SPOT`, `SOCKET`
  - `ServiceRole`: `SPOT`, `ROUTER`, `DEALER`, `PUB`, `SUB`
  - `SpotNodeState`: `IDLE`, `CONNECTING`, `PARTIAL_READY`, `READY`, `ERROR`
  - `SpotPeerSource`: `MANUAL`, `DISCOVERY`, `MIXED`
  - `SpotPeerState`: `CONFIGURED`, `CONNECTING`, `CONNECTED`
  - `RegistryState`: `IDLE`, `ACTIVE`, `DEGRADED`, `ERROR`
  - `TopologySource`: `MANUAL`, `DISCOVERY`, `REGISTRY`
  - `TopologyState`: `DISCOVERED`, `CONNECTING`, `READY`, `LOST`,
    `ERROR`, `STOPPED`

### Service Layer Naming Policy
- 서비스 계층도 Naming Policy를 따른다.
- 허용되는 변형: 케이싱 변형, overload 불가 언어의 최소 접미사.
- 단어 교체, 생략, 대체는 금지한다.
- 규칙 상세는 Naming Policy 본문과 동일하다.

#### Service Layer Canonical Name Table

| Component | Canonical Name | 설명 |
|---|---|---|
| SpotNode | `bind` | endpoint 바인드 |
| SpotNode | `connectPeer` | peer 연결 |
| SpotNode | `disconnectPeer` | peer 연결 해제 |
| SpotNode | `attachDiscovery` | Discovery 연결 |
| SpotNode | `setTlsServer` | TLS 서버 설정 |
| SpotNode | `setTlsClient` | TLS 클라이언트 설정 |
| SpotNode | `statusSnapshot` | 노드 상태 스냅샷 |
| SpotNode | `peersSnapshot` | peer 목록 스냅샷 |
| SpotNode | `peersQuery` | peer 필터 조회 |
| SpotNode | `subjectsSnapshot` | subject 목록 스냅샷 |
| SpotNode | `close` | 노드 종료 |
| Spot | `publish` / `tryPublish` | 토픽 발행 |
| Spot | `subscribe` / `trySubscribe` | 토픽 구독 수신 |
| Spot | `setSubscription` / `unsetSubscription` | 구독 필터 관리 |
| Spot | `onSubscribe` | 구독 수신 callback |
| Spot | `onSendReady` | send ready callback |
| Spot | `close` | facade 종료 |
| Discovery | `connectRegistry` | Registry에 연결 |
| Discovery | `setValue` / `getValue` | 서비스 값 설정/조회 |
| Discovery | `setMetadata` / `getMetadata` | 서비스 메타데이터 설정/조회 |
| Discovery | `memberPeers` | 멤버 peer 목록 조회 |
| Discovery | `memberPeerMetadata` | 멤버 peer 메타데이터 조회 |
| Discovery | `close` | Discovery 종료 (participant 포함) |
| Registry | `bind` | PUB + ROUTER endpoint 바인드 |
| Registry | `setId` | Registry ID 설정 |
| Registry | `addPeer` | 클러스터 peer 추가 |
| Registry | `setHeartbeat` | heartbeat interval/timeout 설정 |
| Registry | `setBroadcastInterval` | 브로드캐스트 주기 설정 |
| Registry | `statusSnapshot` | Registry 상태 스냅샷 |
| Registry | `serviceSummarySnapshot` | 서비스 요약 스냅샷 |
| Registry | `memberPeers` | 멤버 peer 목록 조회 |
| Registry | `memberPeerMetadata` | 멤버 peer 메타데이터 조회 |
| Registry | `topologySnapshot` | 토폴로지 스냅샷 |
| Registry | `topologyQuery` | 토폴로지 필터 조회 |
| Registry | `close` | Registry 종료 |
| RegistryQueryClient | `connect` | Registry에 연결 |
| RegistryQueryClient | `snapshot` | 토폴로지 스냅샷 (필터 선택) |
| RegistryQueryClient | `close` | 클라이언트 종료 |
| ServiceMonitor | `recv` / `tryRecv` | event 수신 |
| ServiceMonitor | `onEvent` | event callback |
| ServiceMonitor | `snapshot` | 상태 스냅샷 |
| ServiceMonitor | `close` | monitor 종료 |

### Service Layer Testing Policy
- 서비스 계층은 sample이나 perf에서 직접 검증되지 않는 컴포넌트를 포함한다.
- 특히 Discovery와 Registry는 테스트가 유일한 검증 경로다.
- 래핑 코드라도 FFI 매핑, lifecycle, 타입 변환이 올바른지 반드시 테스트해야 한다.
- 서비스 계층도 Test Matrix와 동일한 카테고리로 테스트한다.

#### Service Layer Surface Tests
- SpotNode capability matrix 정렬 확인
- Spot capability matrix 정렬 확인
- Discovery capability matrix 정렬 확인
- Registry capability matrix 정렬 확인 (구현된 경우)
- RegistryQueryClient capability matrix 정렬 확인 (구현된 경우)
- ServiceMonitor canonical surface 존재 확인 — Discovery 전용 (`recv`,
  `tryRecv`, `onEvent`, `snapshot`)
- typed domain object 존재 확인 (ServiceEvent, SpotNodeStatus,
  MemberPeerEntry 등)
- typed enum 존재 확인 (ServiceType, ServiceRole, SpotNodeState 등)

#### Service Layer Contract Tests
- SpotNode: create/bind/close lifecycle 누수 없음
- Spot: create/close lifecycle (SpotNode는 살아 있어야 함)
- Discovery: create/connectRegistry/close lifecycle 누수 없음
- Discovery close 시 연결된 participant(SpotNode 등) 종료 확인
- Registry: create/bind/close lifecycle 누수 없음 (구현된 경우)
- RegistryQueryClient: create/connect/close lifecycle (구현된 경우)
- ServiceMonitor: open/close lifecycle 누수 없음 (Discovery 전용)
- 예외/오류 경로에서도 native 리소스가 정리되는지 확인

#### Service Layer Behavior Tests
- SpotNode bind → Spot publish → Spot subscribe 경로 성공
- Spot trySubscribe → 데이터 없음 시 empty 반환
- Spot tryPublish → explicit outcome 반환
- Spot onSubscribe callback 호출 확인
- Spot onSendReady callback 호출 확인
- SpotNode connectPeer → peer 간 publish/subscribe 경로 성공
- SpotNode attachDiscovery 후 connectPeer 차단 (EFSM) 확인
- Discovery connectRegistry → 서비스 등록 경로 성공
- Discovery setValue/getValue round-trip 확인
- Discovery setMetadata/getMetadata round-trip 확인
- Discovery memberPeers 조회 확인
- Registry bind → Discovery connectRegistry → 서비스 발견 경로 성공
  (Registry 구현된 경우)
- Registry statusSnapshot 결과 확인 (구현된 경우)
- Registry topologySnapshot/topologyQuery 결과 확인 (구현된 경우)
- RegistryQueryClient snapshot 결과 확인 (구현된 경우)
- Socket attachDiscovery → connect/disconnect/unbind/close 차단 확인
  (Discovery 지원 시)

#### Service Layer Monitor Tests
- ServiceMonitor blocking recv 성공 경로 (Discovery 전용)
- ServiceMonitor non-blocking tryRecv empty 경로 (Discovery 전용)
- ServiceMonitor onEvent callback 호출 확인 (Discovery 전용)
- ServiceMonitor snapshot 상태 반환 확인 (Discovery 전용)
- Discovery monitor: serviceUp/serviceDown event 수신 확인 (Discovery
  지원 시)

#### Service Layer Introspection Tests
- SpotNode statusSnapshot → SpotNodeStatus 필드 검증
  (state, peerCount, subjectCount 등)
- SpotNode peersSnapshot → SpotNodePeerEntry 목록 검증
- SpotNode peersQuery → 필터 적용 결과 검증
- SpotNode subjectsSnapshot → SpotNodeSubjectEntry 목록 검증
- Registry statusSnapshot → RegistryStatus 필드 검증 (구현된 경우)
- Registry serviceSummarySnapshot → 필터 적용 결과 검증 (구현된 경우)
- Registry memberPeers → MemberPeerEntry 목록 검증 (구현된 경우)
- Registry topologySnapshot → RegistryTopologyEntry 목록 검증 (구현된 경우)
- Discovery memberPeers → MemberPeerEntry 목록 검증
- Discovery memberPeerMetadata → metadata 반환 검증

#### Service Layer Scope for Tests

| Test Category | SpotNode+Spot | Discovery | Registry | QueryClient |
|---|---|---|---|---|
| Surface | Required | Required | 구현 시 Required | 구현 시 Required |
| Contract | Required | Required | 구현 시 Required | 구현 시 Required |
| Behavior | Required | Required | 구현 시 Required | 구현 시 Required |
| Monitor | Required | Required | — | — |
| Introspection | Required | Required | 구현 시 Required | 구현 시 Required |

- service/spot 계열이 없는 바인딩은 이 테스트를 제외할 수 있다.

### Service Layer Sample Policy
- Canonical Sample Set에 정의된 서비스 계열 샘플:
  - `spot_recv_sample`: Spot direct subscribe
  - `spot_callback_sample`: Spot onSubscribe callback
  - `monitor_recv_sample`: monitor event 수신 (socket monitor 포함)
- service/spot 계열이 없는 바인딩은 `spot_*` 샘플을 제외할 수 있다.

### Service Layer Scope per Binding
- 모든 바인딩이 서비스 계층 전체를 구현해야 하는 것은 아니다.
- 최소 요구 사항:

| Component | 요구 수준 |
|---|---|
| SpotNode + Spot | 해당 바인딩에 spot 지원이 있으면 Required |
| Discovery | 해당 바인딩에 discovery 지원이 있으면 Required |
| Registry | Target (서버 측 컴포넌트, 전체 바인딩 필수 아님) |
| RegistryQueryClient | Target (조회 전용 클라이언트) |
| ServiceMonitor | Discovery 지원 시 Required (SPOT은 ServiceMonitor 미사용) |

### Callback API Policy
- callback 등록 API는 각 소켓 타입의 capability에 따라 노출한다.
- 위 Callback Capabilities 표가 기준이다.
- canonical callback 이름:
  - `onReceive`: direct message 수신 callback
  - `onSubscribe`: topic message 수신 callback
  - `onSendReady`: send ready 상태 callback
- callback payload shape는 direct receive와 동일해야 한다.
  - `onReceive` callback payload = `recv()` 반환 shape
  - `onSubscribe` callback payload = `subscribe()` 반환 shape
- callback 등록 후 동일 subject에 대한 direct recv/subscribe는 native 계약에
  따라 차단된다 (EBUSY).
- callback을 `null`/`None`으로 설정하여 해제하는 것은 허용하지 않는다.
  callback 해제는 socket close로만 이루어진다.

## Core API Additions

이 섹션은 `core/include/zlink.h`에 추가된 core API를 정리한다.
각 바인딩은 이 API를 언어별 typed surface로 노출해야 한다.

### Request-Reply Policy

> 언어별 인터페이스 시그니처와 사용 예는
> `cpp/`, `java/`, `dotnet/`, `node/`, `python/`, `go/`, `rust/` 를 참조한다.
> 구현 기준 상세는
> [`doc/plan/spot-refactor/SOCKET_REQUEST_REPLY_API_SPEC.md`](../../plan/spot-refactor/SOCKET_REQUEST_REPLY_API_SPEC.md),
> [`doc/plan/spot-refactor/ZMP_REQUEST_REPLY_PROTOCOL.md`](../../plan/spot-refactor/ZMP_REQUEST_REPLY_PROTOCOL.md),
> [`doc/plan/spot-refactor/SPOT_ROUTED_MESSAGE_SPEC.md`](../../plan/spot-refactor/SPOT_ROUTED_MESSAGE_SPEC.md)
> 를 따른다.

#### 설계 원칙

- request-reply 는 ZMP protocol envelope 로 처리한다.
  `zlink_msg_t` 에 request 표시를 붙이는 방식은 사용하지 않는다.
- dispatch, pending map, timeout, reply 매칭은 core C API 에서 처리한다.
  바인딩은 이 로직을 다시 구현하지 않는다.
- core 는 callback 기반 비동기 모델을 제공한다.
  바인딩은 callback 위에 coroutine/future/promise 표면을 얹는다.
- `request()` / `tryRequest()` 는 thread blocking API 가 아니다.
- request-reply 는 Router/Dealer 소켓과 SPOT 의 기능 확장이다.
  별도 추상 레이어가 아니라 기존 표면에 capability 를 얹는다.

#### 제거된 API

message-level request-reply marker API 와 per-message metadata API 는 제거되었다.

- `zlink_msg_set_request`, `zlink_msg_set_reply`, `zlink_msg_get_request_info`
- `zlink_msg_set_metadata`, `zlink_msg_get_metadata`, `zlink_msg_clear_metadata`

바인딩은 이 함수나 상수를 public surface 로 다시 노출하면 안 된다.
`Message` 객체 안에 request marker 상태를 두지 않는다.

#### 유효한 Request-Reply 조합

**Socket 경로:**

| 요청자 | 응답자 | 가능 | reply 경로 |
|--------|--------|------|-----------|
| Dealer | Router | Y | Router 가 Dealer 의 routing_id 로 회신 |
| Router | Router | Y | 서로 routing_id 로 회신 |
| Dealer | Dealer | **N** | 양쪽 다 routing_id 없음 |
| Router | Dealer | **N** | Dealer 가 특정 peer 에 회신 불가 |

**SPOT 경로:**

| 요청자 | 응답자 | 가능 | reply 경로 |
|--------|--------|------|-----------|
| Spot | Spot | Y | 상대 주소 + request_seq 로 회신 |
| Spot | Router | Y | Spot 이 Router 에 request, Router 가 Spot 에 reply |
| Router | Spot | Y | Router 가 Spot 에 request, Spot 이 Router 에 reply |

RequestDealer 연결 제약:
- 연결 대상은 전부 Router 여야 한다.
  Dealer 에 Router 와 Dealer 가 섞이면 request 가 실패할 수 있다.
- 바인딩은 이 제약을 런타임에 검증하지 않는다. 사용자 책임이며 API 문서에 명시한다.

#### C API 표면

**공통 타입:**

```c
typedef void (*zlink_reply_handler_fn)(
    int errno_, zlink_msg_t *parts, size_t part_count, void *userdata);

typedef void (*zlink_router_handler_fn)(
    const zlink_routing_id_t *peer_rid, uint64_t request_seq,
    zlink_msg_t *parts, size_t part_count, void *userdata);
```

callback 으로 전달된 `parts`, `peer_rid` 는 borrowed view 다.
callback 반환 시점까지만 유효하다. 밖에서 유지하려면 복사한다.

**Socket API:**

```c
int zlink_dealer_request(void *dealer, zlink_msg_t *parts, size_t part_count,
    zlink_reply_handler_fn handler, void *userdata, zlink_send_flags_t flags,
    uint32_t timeout_ms);

int zlink_router_request(void *router, const zlink_routing_id_t *peer_rid,
    zlink_msg_t *parts, size_t part_count, zlink_reply_handler_fn handler,
    void *userdata, zlink_send_flags_t flags, uint32_t timeout_ms);

int zlink_router_reply(void *router, const zlink_routing_id_t *peer_rid,
    uint64_t request_seq, zlink_msg_t *parts, size_t part_count);

int zlink_router_handler(void *router, zlink_router_handler_fn handler,
    void *userdata);

int zlink_router_recv(void *router, const zlink_routing_id_t **peer_rid_out,
    uint64_t *request_seq_out, zlink_msg_t **parts_out,
    size_t *part_count_out, int flags);
```

**SPOT API:**

```c
int zlink_spot_request_spot(void *spot, ...);
int zlink_spot_reply_spot(void *spot, ...);
int zlink_spot_request_router(void *spot, ...);
int zlink_spot_reply_router(void *spot, ...);
int zlink_router_request_spot(void *router, ...);
int zlink_router_reply_spot(void *router, ...);
int zlink_router_send_spot(void *router, ...);
int zlink_spot_handler(void *spot, ...);
int zlink_spot_recv(void *spot, ...);
int zlink_router_spot_recv(void *router, ...);
int zlink_router_spot_handler(void *router, ...);
```

전체 시그니처는 `core/include/zlink.h` 를 참조한다.

#### 수신 Dispatch 모델

core 가 request-reply dispatch 를 처리한다. 바인딩은 dispatch owner 를 구현하지 않는다.

- `request_seq = 0` 이면 ordinary message.
- `request_seq != 0` 이면 request-reply message.
- core 가 pending map 에서 `peer_rid + request_seq` 로 매칭한다.
- 매칭 실패한 reply (stray/late reply) 는 drop 한다.
- ROUTER 는 generic `zlink_recv()` 대신 `zlink_router_recv()` / `zlink_router_handler()`
  typed surface 를 사용한다. generic `zlink_recv()` 호출 시 `EOPNOTSUPP`.
- peer-directed ROUTER 수신 plane 과 spot-origin 수신 plane 은 서로 다른 표면이다.

#### request/tryRequest, reply/tryReply 구분

`send`/`trySend` 패턴과 동일하다. 같은 core C API 를 호출하고,
바인딩이 send 단계의 backpressure 처리만 다르게 한다.

| | `request()` / `reply()` | `tryRequest()` / `tryReply()` |
|---|---|---|
| backpressure | writable 될 때까지 비동기 대기 | 즉시 실패 반환 |
| 대기 방식 | async 컨텍스트 대기 (스레드 blocking 아님) | 대기 없음 |
| 실패 시 | `ZlinkError(errno)` 예외 | `EAGAIN` / `SendResult` |
| 대응 기존 API | `send()` | `trySend()` |

#### SPOT Request-Reply

SPOT 직접 전달 위에서도 같은 request-reply 프로토콜을 사용한다.
`SPOT routed envelope -> request-reply envelope -> payload` 순서로 싣는다.
SPOT reply 도 ctx 없이 상대 주소 + request_seq 로 보낸다.
같은 Spot 에서 여러 request 를 동시에 outstanding 상태로 둘 수 있다.
high-level request 완료는 첫 reply 1건으로 끝난다.

#### Timeout

- timeout 은 core 가 관리한다. 바인딩은 timeout 로직을 구현하지 않는다.
- 기본 timeout: `5000ms`. per-call > socket default > 구현 기본 `5000ms`.
- `timeout_ms = 0` 이면 socket default timeout 을 사용한다.
- timeout 은 send 대기 + reply 대기를 합산한 전체 경과 시간에 적용된다.
- timeout 시 core 가 pending map 에서 제거하고 callback 에 `ETIMEDOUT` 전달.
- timeout 후 late reply 는 core 가 drop 한다.

#### Pending map

- `request_seq` 채번, pending 등록, reply 매칭, timeout 제거 모두 core 에서 한다.
- 바인딩은 pending map 을 별도로 유지하지 않는다.
- 바인딩이 유지하는 것은 callback → Future/Promise resolve 매핑뿐이다.

#### Wire format

- `request_seq` 는 부호 없는 64비트 정수 (8바이트, network byte order).
- 시작값 `1`. `0` 은 ordinary message 예약값.
- overflow 시 `1` 로 wrap. outstanding 충돌값은 건너뛴다.
- envelope 은 4개 control part: protocol id, version, message type, request_seq.
- SPOT routed 조합 시 8개 SPOT control part + 4개 request-reply control part + payload.
- 바인딩은 envelope 을 직접 파싱하지 않는다. core 가 처리한다.

#### 반환 타입

- `request()` 성공 시 기존 `Received` domain object 를 반환한다.
  별도 `Reply` 타입은 만들지 않는다.
- `Received` 에 `routingId` 와 `requestSeq` 가 포함된다.
- request handler 는 `peer_rid`, `request_seq`, payload 를 함께 전달한다.
  별도 `Request` 타입이나 `onRequest` 전용 callback 은 만들지 않는다.

#### 소유권

- `request()` / `reply()` 호출 시 메시지 ownership 은 기존 send 계약을 따른다.
- reply handler callback 으로 전달된 `parts` 는 borrowed view 다.
  callback 반환 후 무효. 바인딩은 callback 에서 복사하여 `Received` 를 만든다.
- 소켓 close 시 core 가 pending map 의 모든 미완료 request 를 `ETERM` callback 으로 reject 한다.

#### Callback 계약

- callback 은 정확히 한 번 호출된다.
  성공이면 `err = null/0` + `received`, 실패면 `err` + `received = null`.
- 언어별 패턴:
  - C++: `std::function<void(ZlinkError, Received)>`
  - Java: `BiConsumer<Received, ZlinkException>`
  - .NET: `Action<ZlinkException, Received>`
  - Node: `(err, reply)`
  - Python: `callback(err, received)`
  - Go: `func(Received, error)`
  - Rust: `FnOnce(Result<Received, ZlinkError>)`

### SPOT Messaging Policy

> 언어별 SPOT 인터페이스는 `cpp/`, `java/`, `dotnet/`, `node/`, `python/`, `go/`, `rust/` 를 참조한다.

SPOT 은 pub/sub 메시징과 routed direct messaging 두 가지를 지원한다.
request-reply 는 routed direct messaging 위에 얹어진다.

#### Pub/Sub 메시징

SPOT pub/sub 는 topic 기반 발행/구독 모델이다.

```c
/* 발행 */
int zlink_publish(void *subject, const char *topic_id,
    zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags);

/* 구독 수신 */
int zlink_subscribe(void *subject, zlink_routing_id_t *source_rid_out,
    zlink_msg_t **parts_out, size_t *part_count_out,
    char *topic_id_out, size_t *topic_id_len_out, zlink_send_flags_t flags);

/* 구독 필터 */
int zlink_set_subscription(void *handle, const char *filter);
int zlink_unset_subscription(void *handle, const char *filter);
```

바인딩 규칙:
- C API 는 `try_*` 발행 함수를 따로 두지 않는다.
- 바인딩의 `tryPublish` 는 `zlink_publish(..., ZLINK_DONTWAIT)` 를 호출한 뒤
  errno 를 `zlink_send_result_t` 로 분류해서 만든다.
- `subscribe` 수신은 typed receive surface 또는 handler callback 으로 노출한다.
- topic filter 설정은 typed subscription API 로 노출한다.

#### Routed Direct Messaging

SPOT routed direct messaging 은 특정 Spot 또는 Router 에 직접 메시지를 보낸다.
request-reply 가 아닌 ordinary 메시지 전송이다.

```c
/* spot -> spot */
int zlink_spot_send_spot(void *spot,
    const zlink_routing_id_t *dest_node_rid,
    const zlink_routing_id_t *dest_spot_rid,
    zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags);

/* spot -> router */
int zlink_spot_send_router(void *spot,
    const zlink_routing_id_t *peer_rid,
    zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags);

/* router -> spot */
int zlink_router_send_spot(void *router,
    const zlink_routing_id_t *dest_node_rid,
    const zlink_routing_id_t *dest_spot_rid,
    zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags);
```

바인딩 규칙:
- routed send 는 기존 `sendRid` 패턴과 동일하다.
- 목적지 주소는 `dest_node_rid + dest_spot_rid` 또는 `peer_rid` 로 지정한다.
- routed recv 는 아래 Event Dispatcher 의 handler/recv surface 를 사용한다.

#### SPOT Lifecycle

```c
void *zlink_spot_new(void *node);          /* SPOT facade 생성 */
int zlink_spot_destroy(void **spot_p);     /* SPOT facade 해제 */

void *zlink_spot_node_new(void *ctx);      /* SPOT Node 런타임 생성 */
int zlink_spot_node_destroy(void **node_p);/* SPOT Node 해제 */
int zlink_spot_node_bind(void *node, const char *endpoint);
int zlink_spot_node_connect_peer(void *node, const char *peer_endpoint);
int zlink_spot_node_disconnect_peer(void *node, const char *peer_endpoint);
int zlink_spot_node_attach_discovery(void *node, void *discovery);
```

바인딩 규칙:
- `SpotNode` 와 `Spot` 은 별도 typed handle 로 노출한다.
- `Spot` 은 `SpotNode` 위에 올라가는 facade 다. `SpotNode` 해제 시 `Spot` 도 무효가 된다.
- `zlink_spot_node_attach_discovery` 후에는 peer connect/disconnect 를 수동으로 하면 `EFSM`.

### SPOT Event Dispatcher Policy

core 는 callback 기반 event dispatcher 모델을 제공한다.
하나의 I/O thread context 안에서 여러 이벤트 소스
(sub recv, routed recv, timer, send-ready) 를 동기화 없이 처리할 수 있다.

핵심 원리:
- handler callback 을 등록하면 core I/O thread 가 이벤트 발생 시 callback 을 호출한다.
- 모든 callback 은 같은 thread context 에서 실행되므로 lock 없이 상태를 공유할 수 있다.
- callback 안에서 recv, send, reply 를 호출해도 동기화 문제가 없다.
- timer 도 같은 context 에서 실행된다.

#### Callback 등록 API

```c
/* 소켓/subject 에 direct recv callback 등록 */
int zlink_recv_handler(void *s, zlink_socket_msg_handler_fn handler, void *userdata);

/* pub/sub subject 에 topic-aware recv callback 등록 */
int zlink_subscribe_handler(void *s, zlink_subscribe_handler_fn handler, void *userdata);

/* writable 알림 callback 등록 */
int zlink_send_ready_handler(void *s, zlink_send_ready_handler_fn handler, void *userdata);

/* SPOT typed recv callback */
int zlink_spot_handler(void *spot, zlink_spot_handler_fn handler, void *userdata);

/* ROUTER typed recv callback */
int zlink_router_handler(void *router, zlink_router_handler_fn handler, void *userdata);

/* ROUTER 의 SPOT-origin recv callback */
int zlink_router_spot_handler(void *router, zlink_router_spot_handler_fn handler,
    void *userdata);
```

규칙:
- callback 등록은 한 subject 당 하나만 가능하다.
  이미 등록된 상태에서 다시 등록하면 `EBUSY`.
- callback 등록 후 같은 subject 에 대한 direct recv (`zlink_recv`, `zlink_subscribe`) 와
  poller `ZLINK_POLLIN` 등록은 `EBUSY` 로 실패한다.
- callback 은 replace-only 다. `NULL` 전달은 허용하지 않는다.

#### Spot Dispatch Event Handler

Spot 의 핵심 event dispatcher 는 `zlink_spot_dispatch_event_handler()` 다.
이 handler 를 등록하면 Spot 에 관련된 모든 이벤트가 하나의 callback 으로 올라온다.
callback 안에서 event 종류를 확인하고 recv 를 호출하면 동기화 문제 없이 처리할 수 있다.

```c
typedef enum zlink_spot_dispatch_event_t {
    ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE = 1,  /* pub/sub 메시지 도착 */
    ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE    = 2,  /* routed/request 메시지 도착 */
    ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE     = 3   /* timer fire */
} zlink_spot_dispatch_event_t;

typedef void (*zlink_spot_dispatch_event_handler_fn)(
    void *spot, zlink_spot_dispatch_event_t event, void *userdata);

int zlink_spot_dispatch_event_handler(void *spot,
    zlink_spot_dispatch_event_handler_fn handler, void *userdata);
```

사용 패턴:
- dispatch event handler 를 등록한다.
- callback 이 호출되면 `event` 를 확인한다.
- `SUBSCRIBE_READABLE` 이면 `zlink_subscribe()` 로 pub/sub 메시지를 recv 한다.
- `ROUTED_READABLE` 이면 `zlink_spot_recv()` 로 routed/request 메시지를 recv 한다.
- `TIMER_READABLE` 이면 `zlink_timer_recv()` 로 timer fire 를 recv 한다.
- 모든 recv 가 같은 callback context 안에서 실행되므로 lock 이 필요 없다.

#### Spot Timer API

Spot 소유 timer 는 `zlink_spot_timer_new(spot)` 로 생성하고, 이후 공통
`zlink_timer_*` 함수로 제어한다. Spot dispatch event context 에서 실행된다.

```c
void *zlink_spot_timer_new(void *spot);

/* 이후 공통 timer API 사용 */
int zlink_timer_destroy(void **timer_p);
int zlink_timer_start(void *timer, uint64_t interval_ns, uint64_t repeat_count);
int zlink_timer_stop(void *timer);

typedef void (*zlink_timer_handler_fn)(
    void *timer, uint64_t fire_count, void *userdata);

int zlink_timer_handler(void *timer,
    zlink_timer_handler_fn handler, void *userdata);
int zlink_timer_recv(void *timer, uint64_t *fire_count_out, int flags);
```

규칙:
- timer 는 `zlink_spot_timer_new(spot)` 로 Spot 에 종속하여 생성한다.
- 생성 후에는 `zlink_timer_start`, `zlink_timer_stop`, `zlink_timer_recv`,
  `zlink_timer_handler`, `zlink_timer_destroy` 공통 API로 제어한다.
- `interval_ns` 는 나노초 단위다. `repeat_count = 0` 이면 무한 반복.
- timer fire 는 dispatch event handler 에 `TIMER_READABLE` 로 올라온다.
- timer handler callback 을 직접 등록하거나 `zlink_timer_recv()` 로 polling 할 수 있다.
- timer callback 도 같은 dispatch context 에서 실행된다.

바인딩 규칙:
- timer 는 typed wrapper 로 노출한다.
- `interval_ns` 는 언어별 Duration 타입으로 변환한다.
- timer 와 dispatch event 를 통합하여, 사용자는 callback 등록만으로
  sub recv + routed recv + timer 를 동기화 없이 처리할 수 있어야 한다.

#### Dispatch 모델 요약

```
zlink_spot_dispatch_event_handler callback (단일 context, 동기화 불필요)
  ├── SUBSCRIBE_READABLE → zlink_subscribe()    (pub/sub 메시지)
  ├── ROUTED_READABLE    → zlink_spot_recv()    (routed / request 메시지)
  └── TIMER_READABLE     → zlink_timer_recv()   (timer fire)
```

이 callback 안에서 recv, send, reply 를 호출하거나
다른 handler 의 상태를 읽어도 lock 이 필요 없다.

#### Typed Receive Surface

SPOT 수신은 여러 typed surface 를 제공한다.
바인딩은 이 typed surface 위에 언어별 handler/callback 표면을 얹는다.

#### Spot 수신

```c
typedef void (*zlink_spot_handler_fn)(
    const zlink_routing_id_t *source_rid,
    const zlink_routing_id_t *spot_rid,
    uint64_t request_seq,
    zlink_msg_t *parts, size_t part_count, void *userdata);

int zlink_spot_handler(void *spot, zlink_spot_handler_fn handler, void *userdata);
int zlink_spot_recv(void *spot, ...);
```

- `request_seq = 0` 이면 ordinary routed message 또는 pub/sub message 다.
- `request_seq != 0` 이면 request-reply message 다.
- `source_rid + spot_rid` 는 발신자 주소이며 reply target 으로 사용한다.
- `zlink_spot_handler()` 와 `zlink_spot_recv()` 는 같은 수신 plane 을 공유한다.
  동시에 허용하지 않는다. 충돌 시 `EBUSY`.

#### Router 의 SPOT 수신

```c
typedef void (*zlink_router_spot_handler_fn)(
    const zlink_routing_id_t *source_node_rid,
    const zlink_routing_id_t *source_spot_rid,
    uint64_t request_seq,
    zlink_msg_t *parts, size_t part_count, void *userdata);

int zlink_router_spot_handler(void *router,
    zlink_router_spot_handler_fn handler, void *userdata);
int zlink_router_spot_recv(void *router, ...);
```

- peer-directed ROUTER 수신 plane 과 spot-origin 수신 plane 은 서로 다른 표면이다.
- `zlink_router_spot_recv()` 와 `zlink_router_spot_handler()` 도 같은 plane 을
  공유하므로 동시에 허용하지 않는다. 충돌 시 `EBUSY`.

#### Pub/Sub 수신

```c
typedef void (*zlink_subscribe_handler_fn)(
    zlink_routing_id_t *source_rid,
    zlink_msg_t *parts, size_t part_count,
    void *userdata);

int zlink_subscribe_handler(void *s, zlink_subscribe_handler_fn handler,
    void *userdata);
```

#### Service Monitor

SPOT Node 상태 변경을 모니터링하는 event surface 다.

```c
typedef void (*zlink_service_monitor_handler_fn)(
    const zlink_service_event_t *event, void *userdata);

void *zlink_service_monitor_open(void *node,
    const zlink_service_monitor_open_options_t *options);
int zlink_service_monitor_handler(void *monitor,
    zlink_service_monitor_handler_fn handler, void *userdata);
int zlink_service_monitor_recv(void *monitor,
    zlink_service_monitor_event_t *out, int flags);
```

이벤트 종류:
- `ZLINK_SERVICE_EVENT_PEER_ADDED` — peer 추가
- `ZLINK_SERVICE_EVENT_PEER_REMOVED` — peer 제거
- `ZLINK_SERVICE_EVENT_PEER_READY` — peer 연결 완료
- `ZLINK_SERVICE_EVENT_SUBJECT_ADDED` — 주제 추가
- `ZLINK_SERVICE_EVENT_SUBJECT_REMOVED` — 주제 제거
- `ZLINK_SERVICE_EVENT_SUBJECT_READY` — 주제 준비 완료

바인딩 규칙:
- monitor 는 typed handle 로 노출한다.
- event 수신은 handler callback 또는 direct recv 로 제공한다.
- event mask 필터링은 open 옵션으로 설정한다.
- `zlink_service_event_t` 는 바인딩이 언어별 typed event object 로 변환한다.

#### SPOT Node Status Query

```c
int zlink_spot_node_status_snapshot(void *node, zlink_spot_node_status_t *out);
int zlink_spot_node_peers_snapshot(void *node,
    zlink_spot_node_peer_entry_t **entries_out, size_t *count_out);
int zlink_spot_node_peers_query(void *node,
    const zlink_spot_node_peer_filter_t *filter,
    zlink_spot_node_peer_entry_t **entries_out, size_t *count_out);
int zlink_spot_node_subjects_snapshot(void *node,
    zlink_spot_node_subject_entry_t **entries_out, size_t *count_out);
```

바인딩 규칙:
- snapshot 결과는 언어별 typed domain object 배열로 변환한다.
- filter query 는 typed filter builder 또는 struct 로 노출한다.
- 반환된 배열의 메모리는 바인딩이 적절히 해제해야 한다.

### SpotNode Peer Publish Batching Options

SpotNode 내부 peer publish 경로의 batching 최적화 옵션.
기본값 disabled. public contract(publish/subscribe/callback)은 변경 없다.

#### 옵션 enum

```c
typedef enum zlink_spot_node_option_t {
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_ENABLE                  = 0x3601,
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_DELAY_MS                = 0x3602,
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_MAX_MESSAGES            = 0x3603,
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_MAX_BYTES               = 0x3604,
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_BYPASS_BYTES            = 0x3605,
    ZLINK_SPOT_NODE_OPT_PEER_UNBATCH_MAX_MESSAGES_PER_TURN = 0x3606,
    ZLINK_SPOT_NODE_OPT_PEER_UNBATCH_MAX_BYTES_PER_TURN    = 0x3607,
} zlink_spot_node_option_t;
```

#### 기본값

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `PEER_BATCH_ENABLE` | `false` | batching 활성화 (운영자 opt-in) |
| `PEER_BATCH_DELAY_MS` | `20` | topic bucket flush 최대 지연 (ms) |
| `PEER_BATCH_MAX_MESSAGES` | `32` | bucket당 최대 메시지 수 |
| `PEER_BATCH_MAX_BYTES` | `65536` | bucket당 최대 바이트 |
| `PEER_BATCH_BYPASS_BYTES` | `65536` | 이 크기 이상 메시지는 즉시 전송 |
| `PEER_UNBATCH_MAX_MESSAGES_PER_TURN` | `32` | turn당 unbatch 최대 메시지 수 |
| `PEER_UNBATCH_MAX_BYTES_PER_TURN` | `65536` | turn당 unbatch 최대 바이트 |

#### 바인딩 규칙

- SpotNode의 typed option facade로 노출한다.
- raw `setOption/getOption` bag으로 노출하지 않는다.
- `PEER_BATCH_ENABLE`은 boolean으로 노출한다.
- 나머지는 정수(int)로 노출한다.
- v1은 homogeneous deployment만 지원한다. capability negotiation은 없다.
- application-visible batch API가 아니다. 내부 최적화 옵션이다.

## Option Policy

### Public Option Surface
- public raw `setOption/getOption` bag은 금지한다.
- public raw `setsockopt/getsockopt` bag도 금지한다.
- 공용 옵션은 언어에 맞는 typed surface로 노출한다.
- 특화 옵션도 언어에 맞는 capability surface로 노출한다.
- 예:
  - Java/.NET: `CommonSocketOptions`, `RouterSocketOptions`
  - Go: typed method set, capability interface
  - Rust: typed builder, method set, newtype
  - Python/Node: property, namespace object, capability object, typed method set

#### Option Facade Canonical Type Names
- 각 바인딩은 아래 canonical facade 타입을 제공해야 한다.
- 타입 이름은 언어 케이싱 관례만 변형한다.

| Facade | 내용 | 적용 소켓 |
|---|---|---|
| `CommonSocketOptions` | linger, sendHwm, recvHwm, sendTimeout, recvTimeout, immediate, connectTimeout, ipv6, tcpNoDelay, tcpKeepalive, heartbeatInterval/Ttl/Timeout, maxMsgSize, backlog, reconnectInterval/Max | 전체 |
| `RouterSocketOptions` | mandatory (bool), handover (bool), probe (bool), connectRoutingId (RoutingId) | Router |
| `DealerSocketOptions` | probe (bool) | Dealer |
| `StreamSocketOptions` | notify (bool) | Stream |
| `PubSocketOptions` | verbose (bool), verboser (bool), noDrop (bool), manual (bool) | Pub, XPub |
| `SubSocketOptions` | topicsCount (int, read-only) | Sub, XSub |

- 각 facade의 option 항목은 `core/include/zlink.h`의 해당 option enum 값을
  기준으로 한다.
- facade 내 option 값 타입은 Option Value Types 정책을 따른다.

### Option Value Types
- option 값은 가능한 한 의미 기반 타입으로 노출한다.
- 정책:
  - `0/1` 옵션: `boolean`
  - 유한 상태 집합: `enum`
  - 시간 의미: `Duration` 또는 언어 표준 시간 타입
  - binary identifier: `RoutingId` 같은 value object
  - 진짜 수치 설정: `int`/`long`
  - 문자열/바이트: `String`/`byte[]`
- option 이름만 enum이고 값은 raw `int`인 형태는 충분하지 않다.

## Performance Policy
- 성능은 별도 최적화 항목이 아니라 public API 설계의 일부다.
- canonical hot path는 숨은 비용이 가장 적은 경로여야 한다.
- hot path에서는 다음을 기본적으로 금지한다.
  - 숨은 payload 복사
  - 숨은 배열/리스트 재할당
  - 불필요한 UTF-8 인코딩/디코딩
  - 바인딩 레이어의 중복 포장
  - 결과를 만들기 위한 불필요한 boxing/unboxing
- 편의 API는 기본 경로보다 비용이 더 크면 문서화해야 한다.
- callback path와 direct receive path는 payload shape뿐 아니라 비용 모델도
  과도하게 벌어지면 안 된다.
- zero-copy, borrowed, owned 경로가 다르면 ownership과 함께 비용 모델도
  문서화해야 한다.
- 성능 검증 강도는 언어와 런타임 특성에 따라 달라질 수 있다.
- 다만 모든 바인딩은 hot path에서 불필요한 복사, 할당, 변환을 줄이는 방향을
  기본 정책으로 삼아야 한다.

## Boundary Cost Policy
- 경계 검증은 가장 이른 안전한 위치에서 한 번 수행하는 것을 우선한다.
- 같은 검증을 여러 레이어에서 반복하면 이유가 명확해야 한다.
- 고정 크기 native struct에 들어가는 값은 truncation 대신 즉시 오류를 반환한다.
- 문자열, topic, routing id, metadata 같은 경계 값은 다음을 함께 고려한다.
  - 길이 상한
  - 인코딩 비용
  - 복사 횟수
  - 재할당 정책
- core의 고정 크기 struct 필드에 대응하는 바인딩 입력의 길이 상한:

  | 필드 | C struct 크기 | 바인딩 검증 책임 |
  |------|--------------|----------------|
  | `RoutingId` | `data[255]` | 값 객체 생성 시 255바이트 초과 시 즉시 오류 반환 |
  | topic / filter | C 문자열 (null-terminated) | 바인딩은 embedded null 문자 포함 시 즉시 오류 반환. 길이 상한은 core가 처리하므로 바인딩에서 별도 길이 검증하지 않는다 |
  | service_name | `char[256]` | 255바이트 초과 시 즉시 오류 반환 |
  | endpoint | `char[256]` | 255바이트 초과 시 즉시 오류 반환 |
  | metadata | `zlink_msg_t` (가변) | core가 처리, 바인딩은 null 검증만 |

- 바인딩은 고정 크기 필드에 들어가는 값이 상한을 넘으면 truncation 없이
  즉시 예외/오류를 반환한다.
- public 도메인 객체를 만들 때 불필요한 중간 컬렉션 생성은 피한다.
- helper나 sample이 느린 경로를 canonical path처럼 보이게 만들면 안 된다.

## Monitor Policy
- monitor plane도 같은 규칙을 따른다.
- public monitor receive는:
  - blocking: `recv()`
  - non-blocking: `tryRecv()`
- public `recv(flags)`는 두지 않는다.
- monitor event는 data plane과 별도지만, blocking/non-blocking 구분 방식은
  동일해야 한다.
- monitor는 socket의 상태 변화, readiness 변화, lifecycle event를 관찰하는
  별도 plane 이다.
- monitor payload는 message data plane payload와 혼동되면 안 된다.
- monitor event type은 typed event surface 또는 동등한 의미 surface로
  노출해야 한다.
- monitor consumer는 raw integer mask만이 아니라 event 의미를 읽을 수 있어야
  한다.
- monitor lifecycle은 관찰 대상 socket lifecycle과의 관계가 설명 가능해야 한다.
  - monitor open 시점
  - monitor close 시점
  - observed socket close 이후의 동작
- monitor는 data plane을 대체하는 API가 아니다.
- monitor의 readiness/state event 의미는 data plane contract와 충돌하지
  않아야 한다.
- monitor sample과 test는 다음을 보여야 한다.
  - event 수신 성공 경로
  - non-blocking empty 경로
  - socket state 변화와 monitor event의 관계

## Error Policy

### Binding Validation vs Native Error
- 입력 값의 형식/범위 오류는 바인딩이 즉시 막는다.
- socket 상태, 연결 상태, transport 상태, protocol 상태 오류는 코어가
  결정하고 바인딩은 그대로 caller에 전달한다.

### Binding Must Validate
- truncation 가능성이 있는 값
- overflow 가능성이 있는 값
- fixed-size native struct에 들어가는 값
- 명백한 길이 상한이 있는 값
- offset/length 범위 오류
- null 불가 인자
- enum 범위 밖의 값

이 경우 바인딩 예외를 사용한다.
- Java: `IllegalArgumentException`, `IndexOutOfBoundsException`,
  `NullPointerException`
- .NET: `ArgumentException`, `ArgumentOutOfRangeException`,
  `ArgumentNullException`
- Go: 즉시 `error` 반환 또는 `panic` (프로그래머 오류)
- Rust: compile-time 보장 (`NonZero`, newtype) 또는 `panic!` / `Result<T, E>`

### Native Must Decide
- peer 없음
- backpressure
- readiness 부족
- callback mode와 direct recv 충돌
- socket type/state/runtime 문제
- transport, TLS, endpoint, protocol 오류

이 경우 바인딩은 native 오류를 언어별 예외로 변환하여 caller에 전달한다.
- Java: `ZlinkException`
- .NET: `ZlinkException`
- Go: `error` (`ZlinkError` 또는 동등한 typed error)
- Rust: `Result<T, ZlinkError>`
- Node: `ZlinkError` (extends `Error`)
- Python: `ZlinkError` (extends `Exception`)

### Error Code 표

zlink 에서 사용하는 주요 errno 코드와 의미. 바인딩은 이 코드를 언어별
예외/오류 타입에 매핑하여 caller 가 원인을 구분할 수 있게 한다.

#### POSIX 표준 errno

POSIX 에서 해당 상수가 정의되지 않은 플랫폼에서는 `ZLINK_HAUSNUMERO` 기반
대체 값을 사용한다. 바인딩은 상수 이름으로 비교해야 하며 정수 값에 직접
의존하면 안 된다.

| errno | 대체 값 (POSIX 미정의 시) | 의미 | 대표 발생 상황 |
|-------|-------------------------|------|--------------|
| `ENOTSUP` | HAUSNUMERO + 1 | 지원하지 않는 작업 | 해당 소켓 타입에서 불가능한 작업 |
| `EPROTONOSUPPORT` | HAUSNUMERO + 2 | 프로토콜 미지원 | 지원하지 않는 프로토콜 요청 |
| `ENOBUFS` | HAUSNUMERO + 3 | 버퍼 공간 부족 | 내부 버퍼 할당 실패 |
| `ENETDOWN` | HAUSNUMERO + 4 | 네트워크가 다운됨 | transport 레이어 장애 |
| `EADDRINUSE` | HAUSNUMERO + 5 | 주소가 이미 사용 중 | bind 시 endpoint 충돌 |
| `EADDRNOTAVAIL` | HAUSNUMERO + 6 | 주소를 사용할 수 없음 | 잘못된 endpoint 형식 |
| `ECONNREFUSED` | HAUSNUMERO + 7 | 연결 거부됨 | 대상이 연결을 거부 |
| `EINPROGRESS` | HAUSNUMERO + 8 | 작업 진행 중 | 비동기 연결 진행 중 |
| `ENOTSOCK` | HAUSNUMERO + 9 | 소켓이 아닌 대상 | 잘못된 handle 전달 |
| `EMSGSIZE` | HAUSNUMERO + 10 | 메시지 크기 초과 | 메시지가 설정된 최대 크기 초과 |
| `EAFNOSUPPORT` | HAUSNUMERO + 11 | 주소 체계 미지원 | 지원하지 않는 주소 체계 |
| `ENETUNREACH` | HAUSNUMERO + 12 | 네트워크에 도달 불가 | 라우팅 불가 |
| `ECONNABORTED` | HAUSNUMERO + 13 | 연결이 중단됨 | 연결이 비정상 종료 |
| `ECONNRESET` | HAUSNUMERO + 14 | 연결이 재설정됨 | peer 가 연결을 강제 종료 |
| `ENOTCONN` | HAUSNUMERO + 15 | 연결되지 않은 상태 | 연결 전에 send/recv 시도 |
| `ETIMEDOUT` | HAUSNUMERO + 16 | 작업 시간 초과 | request reply timeout, 연결 timeout |
| `EHOSTUNREACH` | HAUSNUMERO + 17 | 대상에 도달할 수 없음 | peer 미연결, 라우팅 불가 |
| `ENETRESET` | HAUSNUMERO + 18 | 네트워크가 재설정됨 | 네트워크 연결 끊김 |
| `EAGAIN` | (POSIX 표준) | 자원이 일시적으로 사용 불가 | non-blocking send 시 HWM 도달 (backpressure) |
| `EINVAL` | (POSIX 표준) | 잘못된 인자 | 범위 초과, 잘못된 옵션 값 |
| `ECANCELED` | (POSIX 표준) | 작업이 취소됨 | caller 가 request 를 취소 |

`ZLINK_HAUSNUMERO` 값은 `156384712` 이다.

#### zlink 전용 errno

zlink 고유 오류 코드. POSIX errno 와 충돌하지 않도록 `ZLINK_HAUSNUMERO`
기반 오프셋을 사용한다.

| 대체 값 | 상수 | 의미 | 대표 발생 상황 |
|--------|------|------|--------------|
| HAUSNUMERO + 51 | `EFSM` | 유한 상태 기계 오류 | 소켓 상태에서 허용되지 않는 작업 (예: callback 모드에서 direct recv) |
| HAUSNUMERO + 52 | `ENOCOMPATPROTO` | 호환되지 않는 프로토콜 | 서로 다른 프로토콜 버전의 peer 연결 |
| HAUSNUMERO + 53 | `ETERM` | 컨텍스트/소켓 종료 | context 또는 소켓이 close 된 상태에서 작업 시도 |
| HAUSNUMERO + 54 | `EMTHREAD` | I/O 스레드 부족 | context 의 I/O 스레드가 부족 |

#### 언어별 ErrorCode 매핑

각 바인딩은 errno 정수값을 언어별 enum 으로 매핑하여 타입 안전한 오류 구분을
제공한다.

| 언어 | enum 타입 | 접근 방식 |
|------|----------|----------|
| Java | `ErrorCode` enum | `ZlinkException.getErrorCode()` |
| .NET | `ErrorCode` enum | `ZlinkException.ErrorCode` |
| Go | `ErrorKind` enum | `ZlinkError.Kind` (native/validation/state) + `Code` |
| Rust | `i32` code | `ZlinkError.code()` |
| Node | `number` errno | `ZlinkError.errno` |
| Python | `int` errno + `ErrorCode` enum | `ZlinkError.errno`, `ZlinkError.error_code` |
| C++ | `int` errno | `ZlinkError.code()` |

### Request-Reply Error Policy

별도 `RequestError` 타입을 도입하지 않는다.
기존 `ZlinkError` + errno 체계를 그대로 사용한다.

오류 코드는 두 계층으로 나뉜다.

**Wire error reply 코드** — peer 가 보내는 protocol-level error reply.
wire 에서 사용 가능한 errno 는 3개로 제한된다: `ENOENT`, `EOPNOTSUPP`, `EINVAL`.

**API/completion 코드** — core 가 callback 에 전달하는 errno:

| errno | 발생 시점 |
|-------|----------|
| `ENOENT` | 대상 peer/spot 을 찾지 못함 (wire 또는 local) |
| `EOPNOTSUPP` | peer 종류 불일치 또는 지원 안 함 |
| `EINVAL` | 잘못된 파라미터 |
| `ETIMEDOUT` | reply 대기 중 timeout 초과 |
| `EPROTO` | envelope parse 실패 또는 잘못된 remote reply |
| `EBUSY` | 수신 표면 충돌 (handler 중복 등록) |

**request 오류:**

| 상황 | `request()` | `tryRequest()` |
|------|------------|---------------|
| backpressure | writable 대기 (timeout 에 합산) | 즉시 `ZlinkError(EAGAIN)` |
| timeout | `ZlinkError(ETIMEDOUT)` | 동일 |
| 대상 없음 | `ZlinkError(ENOENT)` | 동일 |
| remote error reply | `ZlinkError(해당 errno)` | 동일 |
| 소켓 close | `ZlinkError(ETERM)` | 동일 |
| caller 취소 | `ZlinkError(ECANCELED)` | 동일 |
| pending map 에 없는 reply | 무시 | 무시 |

**reply 오류:**

| 상황 | `reply()` | `tryReply()` |
|------|-----------|-------------|
| backpressure | writable 대기 | `SendResult::Backpressured` |
| not ready | writable 대기 | `SendResult::NotReady` |
| send 성공 | 정상 반환 | `SendResult::Sent` |
| send 오류 (EAGAIN 외) | `ZlinkError(errno)` 예외 | 동일 |

- 모든 실패는 async completion 경로 (Future reject / callback) 로 전달한다.
- 언어별 표현:
  - Java: `ZlinkException(errno)` — `getErrorCode()` 로 원인 구분
  - .NET: `ZlinkException(errno)` — `Errno` property
  - Go: `ZlinkError{Code: errno}` — `Code` 필드
  - Rust: `Err(ZlinkError{code: errno})` — `code` 필드
  - Node: `ZlinkError` — `errno` property
  - Python: `ZlinkError(errno)` — `errno` attribute

## Length and Range Boundary Policy
- 검증 책임은 두 층으로 나눈다.
- 값 객체가 존재하는 타입:
  - 값 객체 생성 시점에 canonical validation을 수행한다.
  - 예: `RoutingId`, typed enum wrapper, bounded identifier
- 값 객체가 존재하지 않거나 호출 문맥 의존 변환이 필요한 타입:
  - native 호출 직전에 검증한다.
  - 예: `Duration -> int millis`, offset/length slicing, output buffer sizing
- native 호출 직전 재검증은 아래 경우에만 필수다.
  - 값 객체를 거치지 않는 raw 경로가 존재하는 경우
  - 값 객체 생성 후 호출 직전 추가 변환이 들어가는 경우
  - 값 객체가 아닌 복합 입력 조합에서 overflow/truncation이 생길 수 있는 경우
- truncation 후 native로 넘기는 동작은 금지한다.

예:
- `RoutingId`는 `zlink_routing_id_t`의 `data[255]` 계약을 넘기지 않아야 한다.
- `Duration -> int millis` 변환은 overflow를 허용하면 안 된다.
- topic, subscription, metadata처럼 고정 출력 버퍼가 개입되는 경로는 길이와
  재할당 정책이 명확해야 한다.

## Ownership Policy
- `Message` ownership은 코어 계약과 일치해야 한다.
- 모든 바인딩은 내부적으로 C API를 호출하므로, GC 언어를 포함한 전 언어에서
  native message의 ownership을 올바르게 관리해야 한다.
- ownership 경로:
  - send 성공: ownership이 native로 이동한다. 바인딩은 이후 접근하면 안 된다.
  - send 실패: restore 가능한 경로와 consume되는 경로를 혼동하지 않는다.
  - recv: native가 생성한 메시지의 ownership을 바인딩이 넘겨받는다. 바인딩이
    해제 책임을 진다.
  - 생성 후 미전송: 바인딩이 직접 생성한 메시지를 전송하지 않았다면 반드시
    명시적으로 close/해제해야 한다. GC가 managed wrapper만 수거할 뿐, native
    메모리는 해제하지 않으므로 누수가 발생한다.
- callback delivery와 direct receive는 동일한 payload shape를 가져야 한다.
- callback 후 frame validity는 계약으로 명확해야 한다.

## Naming Policy
- 메서드명은 언어 관례만 반영한다.
- 개념 이름은 바인딩 간 최대한 동일하게 유지한다.
- 아래 목록은 의미 기준 canonical name 이다.
- 실제 바인딩 메서드명은 다음 두 가지 변형만 허용한다.
  1. **케이싱 변형**: 언어 관례에 맞게 camelCase/PascalCase/snake_case를
     변환한다. 단어 구성은 바뀌지 않는다.
     - 예: `connectPeer` → Go: `ConnectPeer`, Python: `connect_peer`,
       C++: `connect_peer`, Rust: `connect_peer`
  2. **overload 불가 언어의 최소 접미사**: Go와 Rust처럼 overloading이 없는
     언어에서, 동일 동작의 파라미터 변형을 구분하기 위해 최소한의 접미사를
     허용한다. 이 접미사는 동작 구분이며, 파라미터 인코딩이 아니다.
     - 예: `send` → Go: `Send` / `SendTo`, Rust: `send` / `send_to`
     - 허용 접미사 범위: `To` 수준의 최소 동작 구분 접미사까지만 허용한다.
       파라미터 타입이나 의미를 풀어쓴 접미사는 금지한다.
       - 허용: `SendTo`, `send_to`
       - 금지: `SendWithRoutingId`, `send_routed`, `send_multipart`
     - 접미사 허용은 overloading도 keyword/optional parameter도 없는
       언어(Go, Rust)에만 적용된다.
     - 접미사 없이 시그니처로 구분 가능한 언어에서는 접미사를 사용하지
       않는다.
       - overloading: Java, C#, C++
       - keyword / optional parameter: Python
       - optional / union type: Node/TypeScript
- **그 외의 단어 교체, 단어 생략, 다른 단어 대체는 허용하지 않는다.**
  - 금지 예: `onReceive`를 `recvHandler`로 바꾸는 것 → 단어 교체
  - 금지 예: `querySnapshot`을 `snapshot`으로 줄이는 것 → 단어 생략이므로,
    canonical 이름 자체를 `snapshot`으로 정의해야 한다
- 케이싱이나 접미사가 달라져도 역할 구분과 의미 계약은 같아야 한다.
- 예: `receiveSubscriptionEvent` → Python: `receive_subscription_event`,
  Go: `ReceiveSubscriptionEvent`
- 추천 canonical 이름:
  - `bind`, `connect`, `close`
  - `send`, `trySend`
  - `recv`, `tryRecv`
  - `publish`, `tryPublish`
  - `subscribe`, `trySubscribe`
  - `receiveSubscriptionEvent`, `tryReceiveSubscriptionEvent`
  - `setSubscription`, `unsetSubscription`
  - `onReceive`, `onSubscribe`, `onSendReady`

### Method Name Conciseness
- 이 규칙은 public API에 엄격히 적용한다.
- internal/private API는 파라미터 인코딩이 가독성을 높이면 허용한다.
  - 내부 코드는 overloading 없이 명시적 이름이 더 읽기 좋을 수 있다.
  - 예: internal helper에서 `sendRouted(id, msg)`는 허용
- 메서드 이름은 동작(action)만 표현한다.
- 파라미터의 존재, 타입, 개수를 이름에 반복하지 않는다.
- 시그니처가 이미 설명하는 것을 이름에 다시 쓰면 안 된다.
- 동작 자체가 다른 경우(예: `send` vs `publish`)는 이름이 달라야 한다.
- 입력만 다른 경우(예: routing id 유무)는 이름을 늘리지 않는다.

안티패턴과 올바른 패턴:

| 안티패턴 | 올바른 패턴 | 이유 |
|---|---|---|
| `sendWithRoutingId(id, msg)` | `send(id, msg)` | `RoutingId` 타입이 이미 의미를 전달 |
| `sendMultipartMessages(parts)` | `send(parts)` | multipart-only이므로 이름에 반복 불필요 |
| `publishToTopic(topic, msg)` | `publish(topic, msg)` | publish는 topic이 있는 동작 |
| `recvWithTimeout(timeout)` | `recv(timeout)` | 시그니처로 충분 |
| `setLingerTimeoutMilliseconds(ms)` | `setLinger(duration)` | 타입이 단위를 전달 |

파라미터 조합이 다를 때 이름을 늘리는 대신 각 언어의 고유 disambiguation
메커니즘을 사용한다.

- Java / C# / C++: overloading
  - 이름은 하나, 시그니처가 구분
  - 예: `send(Message msg)`, `send(RoutingId id, Message msg)`
- Go: 가변 인자 / functional option / 별도 메서드
  - overloading이 없으므로 동작 의미가 다른 경우에만 최소 접미사를 허용한다
  - 예: `Send(msg Message)`, `SendTo(id RoutingId, msg Message)`
  - 파라미터를 그대로 이름에 넣지 않는다
- Python: keyword argument / optional parameter
  - 이름은 하나, keyword가 구분
  - 예: `send(self, message, *, routing_id=None)`
- Node/TypeScript: optional parameter / union type
  - 이름은 하나, 타입이 구분
  - 예: `send(message: Message)`, `send(routingId: RoutingId, message: Message)`
- Rust: trait bound / `Option<T>` / newtype
  - overloading이 없으므로 `impl Into<T>`, `Option<T>`, strong newtype으로 구분
  - 예: `send(msg: impl Into<Message>)`,
    `send_to(id: RoutingId, msg: impl Into<Message>)`
  - 동작 의미가 다른 경우에만 최소 접미사를 허용한다
  - 파라미터를 그대로 이름에 넣지 않는다

언어별 정리:

| 언어 | disambiguation 방식 | 이름에 파라미터 인코딩 |
|---|---|---|
| Java | overloading | 금지 |
| C# | overloading | 금지 |
| C++ | overloading + strong type | 금지 |
| Go | 별도 메서드 / functional option | 금지, 동작 구분 접미사만 허용 |
| Python | keyword / optional | 금지 |
| Node/TS | optional / union | 금지 |
| Rust | trait bound / Option / newtype | 금지, 동작 구분 접미사만 허용 |

## Compatibility Policy
- 호환성보다 일관된 public surface를 우선할 수 있다.
- deprecated compatibility layer는 가능한 빨리 제거한다.
- 새 canonical path를 도입할 때 기존 우회 표면을 같이 남겨 두지 않는다.
- legacy flag 타입 정책:
  - public method signature에서 제거된 `SendFlag` / `ReceiveFlag`는 더 이상
    public API contract의 일부가 아니다.
  - 구현 migration 기간에는 internal helper 또는 package/private helper로만
    유지할 수 있다.
  - canonical public surface가 전 바인딩에 정착하면, public 노출 타입 자체도
    삭제 또는 internal 이동을 우선한다.

## Cross-Language Alignment

### Shared Behavioral Contract
- blocking send/receive 계열은 실패 시 예외 또는 언어별 오류 경로
- non-blocking receive는 “데이터 없음”만 비예외 경로
- non-blocking send는 explicit outcome
- multipart-only
- typed option surface

### Language-Specific Return Style
- C API
  - raw contract와 errno
  - multipart-only 기준 surface
  - blocking API + explicit non-blocking entry
- C++
  - RAII와 typed wrapper
  - multipart-only 기준 surface
  - `try*`와 explicit send outcome을 지원해야 한다
- .NET
  - `Try*` + `out` + enum result
  - multipart-only 기준 surface
- Java
  - domain object + `Optional<T>` + enum result
  - multipart-only 기준 surface
- Go
  - `(T, error)` + strong type + explicit error check
  - multipart-only 기준 surface
  - `Try*`와 explicit send outcome (`SendResult, error`)
  - non-blocking receive는 `(T, bool, error)` 또는 동등한 결과 타입
- Rust
  - `Result<T, ZlinkError>` + strong newtype + ownership
  - multipart-only 기준 surface
  - `try_*`와 explicit send outcome (`Result<SendResult, ZlinkError>`)
  - non-blocking receive는 `Option<T>` 또는 `Result<Option<T>, ZlinkError>`
- Node/Python
  - 언어 관례를 따르되 의미 계약은 동일
  - multipart-only 기준 surface
  - non-blocking receive는 empty/null/None 계열
  - non-blocking send는 bool이 아니라 explicit outcome을 사용해야 한다

언어별 표면은 달라도 의미 계약은 같아야 한다.

### Cross-Language Capability Table
| Area | C API | C++ | .NET | Java | Go | Rust | Node | Python |
|---|---|---|---|---|---|---|---|---|
| Multipart-only public surface | Required | Required | Required | Required | Required | Required | Required | Required |
| Blocking API named directly | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| Non-blocking receive uses `try*` | N/A raw entry | Required | Required | Required | Required | Required | Required | Required |
| Non-blocking send explicit outcome | Core enum/result | Required | Required | Required | Required | Required | Required | Required |
| Public flags overloads | Raw C only | High-level public surface: No | No | No | No | No | No | No |
| Typed option surface | N/A raw C options | Required | Required | Required | Required | Required | Required | Required |
| Socket Capability Matrix 준수 | Core 기준 | Required | Required | Required | Required | Required | Required | Required |
| `onReceive` callback | Raw fn ptr | Required | Required | Required | Required | Required | Required | Required |
| `onSubscribe` callback | Raw fn ptr | Required | Required | Required | Required | Required | Required | Required |
| `onSendReady` callback | Raw fn ptr | Required | Required | Required | Required | Required | Required | Required |
| StreamSocket `connect` 차단 | N/A | Required | Required | Required | Required | Required | Required | Required |
| Monitor typed event surface | Raw struct | Required | Required | Required | Required | Required | Required | Required |

## Testing Policy
- reflection/surface test로 canonical public API를 고정한다.
- 공통 검증 항목:
  - 타입별 capability 분리 여부
  - raw option bag 비노출
  - `try*` naming convention 준수 여부
- contract test로 바인딩 ↔ native 계약을 검증한다.
  - FFI/native 호출 매핑이 올바른지
  - managed ↔ native 경계의 타입 변환이 올바른지
  - native handle lifecycle과 리소스 정리가 누수 없이 동작하는지
- behavior test로 바인딩 레이어가 core 계약을 올바르게 중계하는지 검증한다.
- ownership 회귀 테스트를 유지한다.
- callback mode와 direct mode의 충돌 규칙도 테스트한다.
- 정책 변경 시 필수 테스트 규칙:
  - public surface 변경: reflection/surface test 동반
  - contract 계약 변경: contract test 동반
  - blocking/non-blocking 계약 변경: behavior test 동반
  - ownership/receive shape 변경: callback regression 또는 ownership test 동반
  - option surface 변경: typed option reflection test와 negative capability test 동반
- 성능 회귀 검증은 별도 Perf Policy가 담당한다.
- Test Matrix에 정의되지 않은 테스트 항목이 기존 코드에 남아 있다면 삭제한다.
  - migration 검증, core 기능 재검증, 자동화 불가능한 리뷰 항목 등이 테스트로
    작성되어 있으면 정리 대상이다.
  - 테스트는 이 문서의 Test Matrix 카테고리에 해당하는 항목만 유지한다.

### Test Execution Script Policy
- 각 바인딩은 전체 테스트를 한번에 실행할 수 있는 스크립트를 제공해야 한다.
- 실행 스크립트는 `bindings/<언어>/tests/` 디렉토리에 위치해야 한다.
- 스크립트는 반복 실행 가능하고 성공/실패를 요약해서 보여줘야 한다.
- 권장 형태:
  - `tests/run_tests.sh`
  - `tests/run_tests.ps1`
  - language-specific test runner entry

### Bug Discovery Policy
- 테스트 또는 perf 작성/실행 중 버그를 발견한 경우 다음 절차를 따른다.
- 바인딩 라이브러리 버그:
  - 해당 바인딩에서 직접 수정한다.
  - 수정과 함께 회귀 테스트를 추가한다.
- core 라이브러리 버그:
  - 바인딩에서 core 버그를 직접 수정하지 않는다.
  - `bindings/<언어>/bug/` 디렉토리에 버그 리포트를 작성한다.
  - 리포트에는 최소한 다음을 포함한다.
    - 재현 조건 (소켓 타입, 패턴, 메시지 크기, transport 등)
    - 기대 동작
    - 실제 동작
    - 재현 코드 또는 테스트 참조
  - 바인딩 측에서 workaround가 필요하면 workaround임을 명시하고 bug 리포트를
    참조한다.

## Test Matrix
- 이 섹션은 각 바인딩이 최소한 가져야 할 테스트 항목을 정리한다.
- 바인딩별 표면은 달라도 아래 의미 계약은 모두 검증해야 한다.
- `Surface Tests`, `Contract Tests`, `Behavior Tests`, `Send Failure Contract Tests`,
  `Receive Failure Contract Tests`, `Boundary Validation Tests`, `Option Tests`,
  `Ownership Tests`, `Monitor Tests`는 기본적으로 `Required`다.

### Surface Tests
- canonical public API reflection/surface test
- socket type capability 분리 확인
- typed option surface 존재 확인
- raw option bag 비노출 확인
- monitor canonical surface 존재 확인
  - `recv()`
  - `tryRecv()`

### Contract Tests
- FFI/native 호출 매핑 검증
  - 바인딩 public API 호출이 올바른 C API 함수에 매핑되는지 확인
  - 파라미터 전달과 반환값 변환이 올바른지 확인
- managed ↔ native 경계 타입 변환 검증
  - 언어 타입에서 C 타입으로의 변환이 올바른지 확인
  - C 타입에서 언어 타입으로의 변환이 올바른지 확인
- 리소스 lifecycle 검증
  - context/socket native handle 생성과 해제가 누수 없이 동작하는지 확인
  - 예외/오류 경로에서도 native 리소스가 정리되는지 확인

### Behavior Tests
- 바인딩 레이어가 core 계약을 올바르게 중계하는지 검증한다.
- 목적은 core 메시징 기능 재검증이 아니라 바인딩 경로의 정확성 확인이다.
- blocking 경로:
  - `send` → core send 중계 성공
  - `recv` → core recv 중계 성공
  - `publish` → core publish 중계 성공
  - `subscribe` → core subscribe 중계 성공
  - routed `send` → routing id 포함 중계 성공
- non-blocking 경로:
  - `tryRecv` → 데이터 없음 시 empty 반환
  - `trySubscribe` → 데이터 없음 시 empty 반환
  - `tryReceiveSubscriptionEvent` → 데이터 없음 시 empty 반환
  - `trySend` → explicit outcome 반환
  - `tryPublish` → explicit outcome 반환

### Send Failure Contract Tests
- blocking `send` failure가 예외 또는 언어별 오류 경로로 caller에 전달되는지 확인
- blocking `publish` failure가 예외 또는 언어별 오류 경로로 caller에 전달되는지 확인
- `trySend` backpressure 결과 확인
- `trySend` not-ready 결과 확인
- `tryPublish` backpressure 또는 not-ready 결과 확인
- `EAGAIN` 외 오류가 `try*`에서 무시되지 않는지 확인

### Receive Failure Contract Tests
- callback mode와 direct recv 충돌 시 native 계약대로 오류가 전달되는지 확인
- direct recv 불가 상태에서 empty/null로 숨기지 않는지 확인
- `EAGAIN`만 empty/non-success 결과로 처리되는지 확인

### Boundary Validation Tests
- `RoutingId` 최대 길이 경계 (255바이트 OK)
- `RoutingId` 초과 길이 즉시 오류 반환 (256바이트 이상 → 예외)
- `Duration -> int millis` overflow 경계
- offset/length bounds 검증
- null 불가 인자 검증
- enum 범위 밖 값 검증
- `service_name` 255바이트 초과 즉시 오류 반환 (고정 크기 `char[256]`)
- `endpoint` 255바이트 초과 즉시 오류 반환 (고정 크기 `char[256]`)
- topic/filter에 embedded null 문자 포함 시 즉시 오류 반환

### Option Tests
- common option typed getter/setter
- socket type별 typed option getter/setter
- 잘못된 소켓 타입에서 option capability 접근 차단
- raw integer 대신 enum/boolean surface가 제공되는지 확인

### Ownership Tests
- send 성공 시 ownership 이동 계약 (native에 넘어감, 바인딩이 이후 접근 금지)
- send 실패 시 restore 또는 caller ownership 유지 계약
- 생성 후 send하지 않은 메시지의 명시적 close/해제 (close 없으면 native 메모리 누수)
- recv 결과 ownership 계약 (바인딩이 받아서 해제 책임)
- callback 후 frame validity 계약
- multipart receive shape와 callback delivery shape 일치 여부

### Monitor Tests
- blocking monitor `recv` 성공 경로
- non-blocking monitor `tryRecv` empty path
- monitor callback/state 변화와 data plane readiness 일치 여부

### Note: Performance and Sample Verification
- 성능 회귀 검증은 Perf Policy (`doc/perf/`)가 담당한다. Test Matrix에 중복하지
  않는다.
- sample/helper의 canonical API 준수, send 실패 무시 방지, legacy surface
  우회 방지는 Review Checklist에서 검증한다. 자동화 테스트 항목이 아니다.

## Sample Policy
- 샘플 제작 규칙은 [`doc/perf/PERF_POLICY.md`](../../perf/PERF_POLICY.md)
  를 단일 기준 문서로 사용한다.
- 이 문서는 `core/samples/`와 `bindings/*/samples/`를 함께 포괄한다.
- 바인딩 샘플을 추가, 수정, 리뷰할 때는 위 문서를 기준으로 판단한다.

## Perf Policy

perf 코드는 데모가 아니라 바인딩 라이브러리의 성능을 측정하고 개선하기 위한
코드다. perf 의 1차 목적은 바인딩 레이어의 비용을 드러내고, 병목과 회귀를
식별하고, 개선 작업의 전후 차이를 측정하는 것이다.

**perf 정책의 단일 기준은 `doc/perf/` 정책 문서다.** CLI 옵션, 기본값, 출력
포맷, RESULT line 형식, 패턴/transport matrix, phase 규칙, 결과 저장, 실패
처리, 환경 변수 등 모든 세부 규격은 아래 문서를 따른다. 본 섹션에서 중복
정의하지 않는다.

- [`doc/perf/PERF_POLICY.md`](../../perf/PERF_POLICY.md) — 공통 perf 정책
  (공통 원칙, 디렉터리 구조, RESULT 형식, 결과 저장, 출력 형식, 실패 처리,
  환경 변수, 리팩토링 원칙, 언어별 적용 범위)
- [`doc/perf/PERF_SINGLE_TEST_POLICY.md`](../../perf/PERF_SINGLE_TEST_POLICY.md) — single suite 정책
- [`doc/perf/PERF_MULTI_TEST_POLICY.md`](../../perf/PERF_MULTI_TEST_POLICY.md) — multi suite 정책

### 바인딩 perf 원칙

- perf 코드는 `doc/perf` 정책을 준수한다.
- `core/perf` 에서 제공하는 패턴과 시나리오를 기준으로 한다.
- core perf와 비교 가능한 시나리오를 유지하면서, 각 언어 스타일에 맞게 작성한다.
- 측정 anchor point, phase 의미, metric 집합, RESULT line 의미를 바꾸지 않는다.
- perf 정책은 성능 측정 surface를 공식 제공하는 바인딩에서는 `Required`다.
  perf 코드를 아직 제공하지 않는 바인딩에는 `Target`으로 본다.

### 바인딩 API Spec 문서

각 바인딩의 API surface는 아래 문서를 참조한다.
perf 정책은 [`doc/perf/PERF_POLICY.md`](../../perf/PERF_POLICY.md)에서 전 언어 공통으로 관리한다.

| 바인딩 | API Spec |
|--------|----------|
| Node.js | [`NODE_API_SPEC.md`](NODE_API_SPEC.md) |
| Python | [`PYTHON_API_SPEC.md`](PYTHON_API_SPEC.md) |

### Perf Review Checklist

- 이 perf 가 바인딩 라이브러리 비용을 측정하고 있는가
- 핵심 send/recv/callback 경로가 perf 파일 본문에서 직접 읽히는가
- 각 패턴이 별도 파일로 분리되어 있는가
- `core/perf` 패턴과 정렬되어 있는가
- `doc/perf` 정책을 준수하는가

## Script Location Policy
- 실행 스크립트는 실행 대상과 같은 디렉토리에 위치한다.
- 바인딩 루트가 아니라 각 하위 디렉토리에 둔다.

| 용도 | 위치 | 스크립트 예시 |
|------|------|---------------|
| 테스트 | `bindings/<언어>/tests/` | `run_tests.sh` |
| 샘플 | `bindings/<언어>/samples/` | `run_samples.sh` |
| perf | `bindings/<언어>/perf/` | `run_benchmarks.sh`, `run_benchmarks_multi.sh` |

- Windows 지원이 필요한 경우 `.ps1` 도 함께 제공한다.
- 바인딩 루트(`bindings/<언어>/`)에 `run_samples.sh` 같은 wrapper를
  두지 않는다. 이 위치의 wrapper는 `samples/run_samples.sh`와 중복되고,
  어느 것이 정답인지 혼선을 만든다.
- CI나 전체 검증을 위해 테스트+샘플+perf를 한번에 실행하는 orchestration
  스크립트가 필요하면 `bindings/<언어>/run_all.sh` 같은 이름으로 둘 수 있다.
  이 스크립트는 개별 `tests/run_tests.sh`, `samples/run_samples.sh` 등을
  호출하는 진입점이며, 개별 스크립트를 대체하지 않는다.

## Review Checklist
- public API가 multipart-only인가
- blocking/non-blocking이 이름으로 분리되었는가
- public flags 오버로드가 남아 있지 않은가
- raw option bag이 public에 남아 있지 않은가
- option 값이 enum/boolean/value object로 승격되었는가
- 타입별 capability가 제대로 닫혀 있는가
- blocking send 실패가 예외 또는 오류 경로로 반드시 caller에 전달되는가
- `trySend`가 `Backpressured`/`NotReady`만 결과값으로 반환하고 나머지를 숨기지 않는가
- binding이 truncation/overflow를 선검증하는가
- native 상태 오류를 바인딩이 임의로 추론하지 않는가
- reflection test와 behavior test가 같이 있는가
- 값 객체 검증과 호출 직전 검증의 책임 위치가 설명 가능한가
- legacy flag 타입이 public contract에서 제거되었는가
- sample code가 canonical API만 사용하는가
- helper가 blocking send 실패를 무시하지 않는가
- helper가 deprecated/legacy surface를 우회 호출하지 않는가

## POSD-Based Implementation Completion Policy
- 이 섹션은 바인딩 구현을 완성하고 리팩터링할 때 적용하는 POSD 기반 절차를
  정의한다.
- 바인딩은 기능 나열이 아니라 구조적 정확성을 기준으로 완성한다.
- 완성 기준은 Socket Capability Matrix, Callback API Policy, Option Policy,
  Test Matrix, Sample Policy다.
- 리팩터링은 코드를 이동하는 것이 아니라 시스템 복잡도를 줄이는 것이다.

### 완성 순서
- 바인딩 구현은 아래 순서를 따른다.
- 각 단계는 이전 단계의 결과에 의존한다.
- 한 단계를 건너뛰고 다음 단계를 진행하지 않는다.

#### 1단계: Capability Matrix 정렬
- Socket Capability Matrix를 기준으로 각 소켓 타입의 public API를 검토한다.
- 있어야 하는데 없는 API를 추가한다.
- 있으면 안 되는데 노출된 API를 제거하거나 internal로 이동한다.
- 검증: surface test가 matrix와 일치해야 한다.
- 대표 위반 예:
  - StreamSocket에 `connect()` 노출 → 제거
  - Node에 `onSendReady` 없음 → 추가
  - 잘못된 소켓에 publish/subscribe 노출 → 제거

#### 2단계: 이름 정규화
- Naming Policy와 Callback API Policy 기준으로 canonical 이름을 맞춘다.
- 이름만 다르고 의미가 같은 API는 canonical 이름으로 통일한다.
- deprecated alias는 제거한다.
- 검증: surface test에서 canonical 이름 존재를 확인한다.
- 대표 위반 예:
  - `recvHandler` → `onReceive`
  - `subscribeHandler` → `onSubscribe`
  - `on_topic_message` → `on_subscribe`

#### 3단계: 깊은 모듈 구조
- POSD deep module 원칙에 따라 public 타입의 깊이를 확보한다.
- 각 public 타입이 단순 pass-through가 아니라 내부에서 검증, ownership,
  shape 규칙을 캡슐화하는지 확인한다.
- 얕은 래퍼 판별 기준:
  - native 함수를 1:1로 감싸기만 하고 새 의미를 추가하지 않는가
  - 호출자가 native 계약(시퀀스, 크기, 인코딩)을 알아야 사용할 수 있는가
  - 동일 규칙이 여러 소켓 타입에 중복 구현되어 있는가
- 얕은 래퍼를 발견하면:
  - 검증을 값 객체 또는 facade 내부로 이동한다
  - 중복 규칙을 한 모듈에 모은다
  - pass-through만 하는 public 타입은 제거하거나 internal에 병합한다
- 대표 위반 예:
  - RoutingId 길이 검증이 각 소켓 타입마다 중복 → RoutingId 값 객체 하나로 모은다
  - monitor event가 raw int → typed event surface로 승격한다
  - option value가 raw int → enum/boolean/Duration으로 승격한다

#### 4단계: 변경 파급 제거
- 같은 규칙이 여러 곳에 흩어진 지점을 찾아서 한 모듈에 모은다.
- 판별 기준:
  - 정책 하나가 바뀌면 2개 이상의 파일을 고쳐야 하는가
  - 새 소켓 타입을 추가할 때 기존 코드를 N곳 수정해야 하는가
- 대표 위반 예:
  - send failure contract 규칙이 소켓 타입마다 별도 구현
  - blocking/non-blocking 분기가 소켓 타입마다 별도 구현
  - option validation이 각 option setter마다 별도 구현

#### 5단계: 정보 은닉 강화
- public API가 native 세부사항을 노출하는 지점을 찾아서 facade 뒤로 숨긴다.
- 판별 기준:
  - 사용자가 errno, flag 상수, native struct 크기를 알아야 하는가
  - 사용자가 internal sequencing(호출 순서)을 기억해야 하는가
  - public API에 native handle, raw pointer, raw buffer가 노출되는가
- 대표 위반 예:
  - raw `setSockOptRaw` / `setOption(int, byte[])` 가 public
  - monitor event에 raw int mask가 그대로 노출
  - SendFlag/ReceiveFlag가 public 타입으로 남아 있음

#### 6단계: 테스트 Matrix 완성
- Test Matrix의 모든 카테고리에 대해 테스트를 작성하거나 보강한다.
- 완성 기준:
  - Surface test가 Socket Capability Matrix를 검증한다
  - Contract test가 FFI 매핑과 lifecycle을 검증한다
  - Behavior test가 blocking/non-blocking 경로를 검증한다
  - Send/Receive Failure test가 오류 계약을 검증한다
  - Boundary test가 값 경계를 검증한다
  - Option test가 typed surface를 검증한다
  - Ownership test가 send/recv ownership을 검증한다
  - Monitor test가 recv/tryRecv를 검증한다

#### 7단계: 샘플 정렬
- Canonical Sample Set 기준으로 샘플을 완성한다.
- 각 샘플이 canonical API만 사용하는지 확인한다.
- 1-5단계에서 이름이나 API가 바뀌었다면 샘플도 같이 갱신한다.

### 리팩터링 판단 기준
- 다음 질문에 "예"이면 리팩터링이 필요한 지점이다.
  - 이 public 타입을 제거하면 사용자가 잃는 것이 없는가 → 얕은 래퍼
  - 이 규칙을 고치면 3개 이상의 파일을 건드려야 하는가 → 변경 파급
  - 사용자가 이 API를 쓰려면 다른 API의 내부 동작을 알아야 하는가 → 정보 누출
  - 같은 능력이 2개 이상의 이름으로 노출되는가 → 중복 surface
  - 사용자가 호출 순서를 기억해야 올바르게 동작하는가 → 시간 순서 의존

### 리팩터링 종료 조건
- 리팩터링은 아래 조건이 모두 충족될 때까지 반복한다.
- 하나라도 남아 있으면 완료가 아니다.
- 판단은 POSD 관점에서 수행한다.
- 종료 조건의 범위는 해당 바인딩이 구현하기로 한 scope에 한정한다.
  - `Required` 항목: 모든 바인딩에 적용
  - `Recommended` 항목(예: 샘플): 공개 배포 바인딩에 적용
  - `Target` 항목(예: Registry): 해당 바인딩이 구현한 경우에만 적용
  - 구현하지 않기로 한 `Target` 컴포넌트는 종료 조건에서 제외한다.

1. **Capability Matrix 완전 정렬**
   - Socket Capability Matrix의 모든 `Y` 항목이 public API에 존재한다.
   - Socket Capability Matrix의 모든 `—` 항목이 public API에 노출되지 않는다.
   - 해당 바인딩이 구현하는 서비스 계층 컴포넌트의 Capability Matrix도
     동일하게 정렬한다.
   - `Target`으로 표시된 컴포넌트(Registry, RegistryQueryClient)는 해당
     바인딩이 구현하지 않으면 종료 조건에서 제외한다.
   - Surface test가 이를 검증하고 통과한다.

2. **이름 정규화 완료**
   - 모든 public API가 Naming Policy의 canonical 이름을 사용한다.
   - deprecated alias가 남아 있지 않다.
   - Callback API Policy의 canonical 이름 3개(`onReceive`, `onSubscribe`,
     `onSendReady`)가 해당 소켓에 존재한다.

3. **얕은 래퍼 제거**
   - native 함수를 1:1로 감싸기만 하는 public 타입이 없다.
   - 모든 public 타입이 검증, ownership, shape 규칙 중 하나 이상을 캡슐화한다.

4. **변경 파급 해소**
   - 동일 규칙이 2개 이상의 모듈에 중복 구현되어 있지 않다.
   - 정책 변경 시 수정해야 할 파일이 1개다.

5. **정보 은닉 확보**
   - public API에 raw option bag, raw flag, raw native struct, raw errno가
     노출되지 않는다.
   - 사용자가 internal sequencing을 알지 않아도 API를 올바르게 사용할 수 있다.

6. **Test Matrix 완성**
   - Test Matrix의 9개 카테고리 전체에 대해 테스트가 존재하고 통과한다.

7. **Sample 정렬 완료**
   - Canonical Sample Set의 모든 샘플이 존재한다.
   - 해당 바인딩이 구현하는 서비스 계층 샘플도 포함한다.
   - 구현하지 않는 `Target` 컴포넌트의 샘플은 제외한다.
   - 모든 샘플이 canonical API만 사용한다.
   - deprecated/legacy 경로를 사용하는 샘플이 없다.

8. **Dead code 제거 완료**
   - 리팩터링 과정에서 발생한 모든 불필요한 코드가 제거되었다.
   - deprecated alias, legacy wrapper, 사용되지 않는 import/using/require가
     남아 있지 않다.
   - Capability Matrix에서 `—`로 표시된 API의 구현 코드가 internal에도 불필요하게
     남아 있지 않다.
   - 이름 정규화로 교체된 옛 이름의 함수/메서드/타입이 남아 있지 않다.
   - 호출되지 않는 private/internal helper가 남아 있지 않다.
   - 참조되지 않는 상수, enum 값, 타입 alias가 남아 있지 않다.
   - 주석으로 처리된 코드 블록(`// removed`, `// deprecated`, `// TODO: remove`)이
     남아 있지 않다.
   - 빈 파일, 빈 클래스, 빈 모듈이 남아 있지 않다.
   - dead code는 "나중에 쓸 수 있으니까" 남겨 두지 않는다. 필요하면 git
     history에서 복원한다.

### 리팩터링 반복 규칙
- 1-7단계를 한 번 수행한 뒤, 종료 조건을 다시 점검한다.
- 앞 단계의 변경이 뒤 단계에 영향을 줄 수 있으므로, 종료 조건이 하나라도
  미충족이면 해당 단계부터 다시 수행한다.
- 종료 조건 8개가 모두 충족될 때까지 반복한다.
- "더 고칠 곳이 보이지 않는다"가 아니라 "종료 조건 8개가 모두 통과한다"가
  완료 기준이다.

### 리팩터링 금지 사항
- 구조 개선을 이유로 의미 계약을 바꾸면 안 된다.
- 내부 리팩터링으로 public API의 시그니처가 달라지면 안 된다.
  - 시그니처가 달라져야 하면 그것은 API 변경이지 리팩터링이 아니다.
- 성능 개선을 이유로 correctness를 타협하면 안 된다.
- "나중에 쓸 수 있으니까" 미리 추상화를 만들면 안 된다.
- 한 번만 쓰이는 코드를 utility/helper로 빼면 안 된다.

## Non-Normative Backlog: Implementation Follow-Ups
- 이 섹션은 규범 본문이 아니라 backlog다.
- 정책은 확정됐지만 각 바인딩 구현에 아직 남아 있을 수 있는 대표 정리 항목을
  기록한다.
- 항목은 바인딩별 리뷰와 리팩터링 backlog의 기본 체크리스트로 사용한다.

### Value Validation Follow-Ups
- `RoutingId`
  - 값 객체 생성 시 길이 상한 검증
  - raw 경로가 남아 있다면 native 호출 직전 재검증
- `Duration` 기반 옵션
  - `int millis` 변환 overflow 검증
  - 음수 허용/비허용 계약 명시
- topic/filter/string identifier
  - 고정 크기 output buffer 경로의 재할당 정책 점검
  - truncation 없이 전체 문자열을 처리하는지 점검
- offset/length 기반 byte API
  - bounds 검증 일관화
- enum wrapper가 없는 raw 정수 옵션
  - enum 또는 boolean 승격 후보 조사

### Public Surface Follow-Ups
- `SendFlag` / `ReceiveFlag`
  - public method signature 제거 여부 재확인
  - public 타입 자체 삭제 또는 internal 이동 여부 결정
- monitor plane
  - `recv()` / `tryRecv()` canonical surface 유지 여부 확인
- callback API
  - callback payload shape가 direct receive shape와 동일한지 재확인
- 단일 메시지 편의 메서드
  - public receive/subscribe 편의 오버로드 잔존 여부 점검

### Option Surface Follow-Ups
- raw option bag 잔존 여부 조사
- socket type별 option capability 누수 여부 조사
- option value가 아직 `int`에 머무는 항목 목록화
- context option도 같은 기준으로 typed facade 적용 여부 검토

### Error Contract Follow-Ups
- binding validation 예외와 native 예외가 혼재된 경로 조사
- 바인딩이 errno를 임의로 해석하는 경로 조사
- `EAGAIN` 외 오류를 잘못 empty/bool 경로로 숨기는 코드 조사
- blocking send 실패를 무시하는 helper/sample 조사

### Performance Follow-Ups
- hot path send/recv 경로의 숨은 복사 조사
- `Message`, `Received`, `TopicMessage` 생성 과정의 불필요한 컬렉션/배열
  할당 조사
- callback path와 direct path 비용 차이 조사
- string/topic/routing-id 변환의 인코딩/디코딩 비용 조사
- sample과 helper가 느린 대체 경로를 기본 사용법처럼 노출하는지 조사

### POSD Follow-Ups
- 얕은 래퍼만 제공하는 public 타입 조사
- 한 규칙이 여러 모듈에 흩어진 변경 파급 지점 조사
- 사용자가 internal sequencing을 알아야 하는 temporal API 조사
- facade 뒤로 숨길 수 있는 raw/native 개념 누수 지점 조사

### Ownership and Callback Follow-Ups
- send failure restore 경로와 consume 경로가 문서와 일치하는지 점검
- callback 후 frame validity 계약 재검증
- callback mode와 direct recv 충돌 시 native 계약대로 오류가 전달되는지 점검

### Test Follow-Ups
- public surface 변경마다 reflection test 존재 여부 확인
- value boundary 검증 테스트 추가
  - 예: `RoutingId` 최대 길이
  - 예: `Duration` overflow
- option negative capability 테스트 보강
- ownership/callback regression 유지 여부 확인

## Binding Requirements

| Binding | 언어 버전 | 런타임/프레임워크 | 빌드 툴 |
|---------|-----------|-------------------|---------|
| C++ | C++17 | — | CMake 3.10+ |
| .NET | C# 12 | .NET 8.0 | MSBuild |
| Java | Java 22 | JDK 22 | Gradle 8.10.2 |
| Go | Go 1.22+ | — | Go modules |
| Rust | Rust 2024 edition | MSRV 1.85+ | Cargo |
| Node | TypeScript 5.8 | Node 22+ | npm |
| Python | Python 3.9 | CPython 3.9+ | setuptools 68+ |
- 각 바인딩의 정확한 버전은 해당 프로젝트 설정 파일이 기준이다.
  - C++: `CMakeLists.txt`
  - .NET: `Zlink.csproj`
  - Java: `build.gradle`, `gradle-wrapper.properties`
  - Go: `go.mod`
  - Node: `package.json`, `tsconfig.json`
  - Python: `pyproject.toml`

## API Reference

각 바인딩은 해당 언어의 표준 문서 도구로 API 레퍼런스를 생성한다.

| Binding | 문서 도구 | 생성 명령 | 출력 위치 |
|---------|-----------|-----------|-----------|
| C++ | Doxygen | `doxygen Doxyfile` | `cpp/doxygen/html/` |
| Java | Javadoc (Gradle) | `./gradlew javadoc` | `java/build/docs/javadoc/` |
| Python | Sphinx + autodoc | `sphinx-build -b html docs docs/_build/html` | `python/docs/_build/html/` |
| Node | TypeDoc | `npx typedoc` | `node/typedoc/html/` |
| .NET | DocFX | `docfx docfx.json` | `dotnet/_site/` |
| Go | godoc / pkgsite | `go doc ./...` | (동적 서버) |
| Rust | rustdoc | `cargo doc --no-deps` | `rust/target/doc/zlink/` |

- 생성 명령은 각 바인딩 디렉터리에서 실행한다.
- 출력 디렉터리는 `.gitignore`로 추적에서 제외한다.
- 각 바인딩의 `README.*.md` 파일에 상세 생성 절차와 스코프가 명시되어 있다.

## Related Docs
- `bindings/cpp/`
- `bindings/dotnet/`
- `bindings/java/`
- `bindings/go/`
- `bindings/rust/`
- `bindings/node/`
- `bindings/python/`
