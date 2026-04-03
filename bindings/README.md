# Bindings API Policy

## 목적
이 문서는 `bindings/` 전체의 public API 정책을 정의한다.

이 문서의 목적은 각 언어 바인딩이 제각각 다른 표면과 예외 규칙을 갖는 것을
막고, `core/include/zlink.h`를 기준으로 설명 가능하고 일관된 공통 계약을
강제하는 데 있다.

이 문서는 단순 스타일 가이드가 아니다. 다음을 위한 설계 기준 문서다.
- public API 설계 기준
- 리뷰 기준
- 리팩터링 기준
- 샘플과 테스트 기준

이 문서의 의도는 다음과 같다.
- 언어별로 이름만 비슷하고 의미가 다른 API를 없앤다.
- 같은 능력을 여러 방식으로 중복 노출하는 얕은 표면을 없앤다.
- raw option bag, legacy convenience, 암묵적 ownership, 숨은 failure path를
  줄인다.
- binding 사용자가 internal sequencing, native 세부사항, hidden transport
  switch를 알지 않아도 되게 만든다.
- POSD 원칙에 맞는 깊은 모듈과 낮은 change amplification 구조를 유도한다.
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
- 구조는 POSD 원칙에 따라 깊은 모듈, 정보 은닉, 낮은 change amplification을
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
- shallow wrapper는 지양한다.
  - 단순히 native 함수 이름만 바꾸고 새 의미를 추가하지 못하는 public
    wrapper는 늘리지 않는다.
- 같은 능력을 여러 타입과 여러 이름으로 반복 노출하지 않는다.
- 변화가 한 곳에서 끝나야 할 규칙은 한 모듈에 모은다.
  - 예: routing id 길이 제한
  - 예: send failure contract
  - 예: typed option ownership
- 시간 순서에 의존하는 temporal decomposition을 줄인다.
  - 예: 사용자가 `setOption` 조합 순서를 기억해야 하는 API 금지
- public API는 “무엇을 할 수 있는지”를 드러내고, “내부에서 어떻게 배선되는지”를
  드러내지 않아야 한다.
- 값 객체와 결과 객체는 깊은 모듈로 취급한다.
  - 호출자에게는 작은 인터페이스를 주고, 내부에서는 검증, ownership, shape
    규칙을 함께 캡슐화해야 한다

## Public Surface Rules

### Base Type Exposure
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
- single-message receive convenience overload는 public에 두지 않는다.
- 단일 part 전송 convenience는 허용할 수 있다.
  - 예: `send(Message part)`는 `send(List<Message> parts)`의 얇은 convenience
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
- send 실패 원인 구분이 코어에 있다면 그대로 enum으로 surface 한다.
- 표준 send 결과 enum:

```text
Sent
Backpressured
NotReady
```

- managed layer는 errno heuristic으로 `Backpressured`와 `NotReady`를
  추론하지 않는다.
- 이 구분은 코어가 직접 제공해야 한다.

### Send Failure Contract
- blocking send 계열:
  - `send`, `publish`, routed `send`
  - 성공 시 정상 반환
  - 실패 시 반드시 예외 또는 언어별 오류 경로로 surface 한다
  - 실패를 `false`, `null`, empty result로 숨기지 않는다
- non-blocking send 계열:
  - `trySend`, `tryPublish`
  - `Backpressured`, `NotReady`는 정상 결과값으로 반환한다
  - `EAGAIN` 계열 외의 오류는 반드시 예외 또는 언어별 오류 경로로 surface 한다
  - managed layer가 send 실패 원인을 임의 해석해서 예외를 삼키면 안 된다
- binding helper, wrapper, sample 코드도 blocking send 실패를 무시하거나
  swallow 하면 안 된다

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
- convenience는 결과 객체 메서드로 둔다.
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
| `monitorOpen` | Y (ServiceMonitor 반환) |
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
| `monitorOpen` | Y (ServiceMonitor 반환) |
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
- SpotNode, Spot, Discovery는 ServiceMonitor를 열 수 있다.
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
- ServiceMonitor event 종류:
  - Discovery 계열: `readyChanged`, `error`, `serviceUp`, `serviceDown`,
    `providersChanged`, `closed`
  - Spot 계열: `readyChanged`, `peerUp`, `peerDown`, `error`,
    `subFilterApplied`, `subscriptionReadyChanged`,
    `pubQueueFull`, `pubQueueDrained`, `closed`,
    `pubDeliveryReadyChanged`, `subDeliveryReadyChanged`,
    `pubFirstDeliveryReadyChanged`

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
| SpotNode | `monitorOpen` | ServiceMonitor 열기 |
| SpotNode | `close` | 노드 종료 |
| Spot | `publish` / `tryPublish` | 토픽 발행 |
| Spot | `subscribe` / `trySubscribe` | 토픽 구독 수신 |
| Spot | `setSubscription` / `unsetSubscription` | 구독 필터 관리 |
| Spot | `onSubscribe` | 구독 수신 callback |
| Spot | `onSendReady` | send ready callback |
| Spot | `monitorOpen` | ServiceMonitor 열기 |
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
- ServiceMonitor canonical surface 존재 확인 (`recv`, `tryRecv`, `onEvent`,
  `snapshot`)
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
- ServiceMonitor: open/close lifecycle 누수 없음
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
- ServiceMonitor blocking recv 성공 경로
- ServiceMonitor non-blocking tryRecv empty 경로
- ServiceMonitor onEvent callback 호출 확인
- ServiceMonitor snapshot 상태 반환 확인
- SpotNode monitor: peerUp/peerDown event 수신 확인
- Spot monitor: readyChanged event 수신 확인
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
| ServiceMonitor | SpotNode/Spot/Discovery 지원 시 Required |

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
- option 값은 가능한 한 의미 기반 타입으로 surface 한다.
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
  - managed layer의 중복 포장
  - 결과를 만들기 위한 불필요한 boxing/unboxing
- convenience API는 canonical path보다 비용이 더 크면 문서화해야 한다.
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
- 고정 크기 native struct에 들어가는 값은 truncation 대신 fail-fast 한다.
- 문자열, topic, routing id, metadata 같은 경계 값은 다음을 함께 고려한다.
  - 길이 상한
  - 인코딩 비용
  - 복사 횟수
  - 재할당 정책
- core의 고정 크기 struct 필드에 대응하는 바인딩 입력의 길이 상한:

  | 필드 | C struct 크기 | 바인딩 검증 책임 |
  |------|--------------|----------------|
  | `RoutingId` | `data[255]` | 값 객체 생성 시 255바이트 초과 fail-fast |
  | topic / filter | C 문자열 (null-terminated) | 바인딩은 embedded null 문자 포함 시 fail-fast. 길이 상한은 core가 처리하므로 바인딩에서 별도 길이 검증하지 않는다 |
  | service_name | `char[256]` | 255바이트 초과 fail-fast |
  | endpoint | `char[256]` | 255바이트 초과 fail-fast |
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
  결정하고 바인딩은 그대로 surface 한다.

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

이 경우 바인딩은 native 오류를 예외로 surface 한다.
- Java: `ZlinkException`
- .NET: `ZlinkException`
- Go: `error` (`ZlinkError` 또는 동등한 typed error)
- Rust: `Result<T, ZlinkError>`

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
  - 생성 후 미전송: 바인딩이 직접 생성한 메시지를 send하지 않았다면 반드시
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
- blocking `send` failure가 예외 또는 언어별 오류 경로로 surface 되는지 확인
- blocking `publish` failure가 예외 또는 언어별 오류 경로로 surface 되는지 확인
- `trySend` backpressure 결과 확인
- `trySend` not-ready 결과 확인
- `tryPublish` backpressure 또는 not-ready 결과 확인
- `EAGAIN` 외 오류가 `try*`에서 swallow 되지 않는지 확인

### Receive Failure Contract Tests
- callback mode와 direct recv 충돌 시 native 계약대로 surface 되는지 확인
- direct recv 불가 상태에서 empty/null로 숨기지 않는지 확인
- `EAGAIN`만 empty/non-success 결과로 처리되는지 확인

### Boundary Validation Tests
- `RoutingId` 최대 길이 경계 (255바이트 OK)
- `RoutingId` 초과 길이 fail-fast (256바이트 이상 → 예외)
- `Duration -> int millis` overflow 경계
- offset/length bounds 검증
- null 불가 인자 검증
- enum 범위 밖 값 검증
- `service_name` 255바이트 초과 fail-fast (고정 크기 `char[256]`)
- `endpoint` 255바이트 초과 fail-fast (고정 크기 `char[256]`)
- topic/filter에 embedded null 문자 포함 시 fail-fast

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
- sample/helper의 canonical API 준수, send 실패 swallow 방지, legacy surface
  우회 방지는 Review Checklist에서 검증한다. 자동화 테스트 항목이 아니다.

## Sample Policy
- 샘플 제작 규칙은 [`doc/spec/sample/SAMPLE_POLICY.md`](../doc/spec/sample/SAMPLE_POLICY.md)
  를 단일 기준 문서로 사용한다.
- 이 문서는 `core/samples/`와 `bindings/*/samples/`를 함께 포괄한다.
- 바인딩 샘플을 추가, 수정, 리뷰할 때는 위 문서를 기준으로 판단한다.

## Perf Policy
- perf 코드는 데모가 아니라 바인딩 라이브러리의 성능을 측정하고 개선하기 위한
  코드다.
- perf 의 1차 목적은 바인딩 레이어의 비용을 드러내고, 병목과 회귀를 식별하고,
  개선 작업의 전후 차이를 측정하는 것이다.
- perf 코드는 다음 기준을 반드시 따른다.
  - `core/perf` 에서 제공하는 패턴과 시나리오를 기준으로 한다
  - `doc/perf` 정책을 준수한다
- perf 코드를 작성하거나 리뷰할 때는 반드시 다음 정책 문서를 읽고 준수해야
  한다.
  - [`doc/perf/PERF_POLICY.md`](../doc/perf/PERF_POLICY.md) — 공통 perf 정책
  - [`doc/perf/PERF_SINGLE_TEST_POLICY.md`](../doc/perf/PERF_SINGLE_TEST_POLICY.md) — single suite 정책
  - [`doc/perf/PERF_MULTI_TEST_POLICY.md`](../doc/perf/PERF_MULTI_TEST_POLICY.md) — multi suite 정책
- core perf가 C API 형태로 제공되더라도, 각 언어 perf 코드는 성능 테스트의
  목적을 해치지 않는 범위에서 해당 언어의 스타일에 맞게 작성한다.
- 즉 perf 코드는 다음 둘을 동시에 만족해야 한다.
  - core perf와 비교 가능한 시나리오
  - 각 언어 사용자에게 자연스러운 스타일
- perf 정책은 성능 측정 surface를 공식 제공하는 바인딩에서는 `Required`다.
- perf 코드를 아직 제공하지 않는 바인딩에는 `Target`으로 본다.

### Perf Design Rules
- perf 코드는 바인딩 성능을 측정하는 코드여야 한다.
- benchmark harness 자체의 복잡도, 불필요한 helper, 과도한 추상화로 핵심
  비용이 가려지면 안 된다.
- 핵심 로직의 가시성이 높아야 한다.
- send/recv/publish/subscribe/callback 핵심 경로가 perf 파일 본문에서
  직접 읽혀야 한다.
- 느린 fallback path, extra logging, debug-only conversion을 perf hot path에
  넣으면 안 된다.
- perf 는 correctness 예제가 아니라 cost measurement 코드이므로, 편의성보다
  측정 충실도를 우선한다.

### Perf Structure Rules
- 각 perf 패턴은 별도 파일로 제공해야 한다.
- 한 파일이 여러 messaging 패턴을 섞으면 안 된다.
- 패턴별 파일 분리 예:
  - pair throughput/latency
  - pubsub throughput/latency
  - routed messaging throughput/latency
  - stream throughput/latency
  - callback delivery cost
- direct recv 패턴과 callback 패턴도 가능하면 별도 파일로 분리한다.
- 파일명만 보고 어떤 패턴의 perf 인지 알 수 있어야 한다.

### Perf Alignment Rules
- 각 언어 perf 는 `core/perf` 의 목적과 형태를 기준으로 맞춘다.
- 다만 구현 표면은 각 언어 스타일을 반영할 수 있다.
  - C++: RAII, typed wrapper
  - .NET: idiomatic object model
  - Java: domain object / typed API
  - Go: idiomatic Go (explicit error, struct, interface)
  - Rust: idiomatic Rust (ownership, Result, newtype)
  - Node/Python: 해당 언어 관례
- 단, 언어 스타일을 반영한다는 이유로 측정 대상이 바뀌면 안 된다.
- perf 간 비교 가능성을 유지하려면 다음을 맞춰야 한다.
  - 메시징 패턴
  - 측정 단위
  - warmup / run 구조
  - 성공 조건과 종료 조건

### Perf Script Interface Rules
- 각 바인딩은 `core/perf`와 동일한 실행 스크립트 인터페이스를 제공해야 한다.
- 스크립트 형태, CLI 옵션, 기본값, 출력 포맷, 결과 파일 naming이 core와
  일치해야 한다.
- 바인딩이 독자적인 옵션 이름, 기본값, 출력 형식을 만들면 안 된다.

#### 실행 스크립트 형태
- 실행 스크립트는 `bindings/<언어>/perf/` 디렉토리에 위치해야 한다.
- 각 바인딩 perf 디렉토리에 다음 entry point를 제공한다.
  - `perf/run_benchmarks.sh` — single suite
  - `perf/run_benchmarks_multi.sh` — multi suite
- Windows 지원이 필요한 경우 `.ps1` 도 함께 제공한다.
- 스크립트는 해당 언어 빌드/런타임을 내부에서 처리하고, 사용자에게는
  core와 동일한 CLI 인터페이스를 노출해야 한다.

#### CLI 옵션
- core/perf 스크립트가 제공하는 공통 옵션을 동일한 이름과 의미로 지원한다.
- 공통 옵션:

| 옵션 | 의미 |
|------|------|
| `--pattern` | 패턴 목록 (comma-separated 또는 `ALL`) |
| `--recv` | 수신 모델 (`recv` 또는 `callback`) |
| `--duration` | active measurement 구간 (초) |
| `--warmup` | warmup 구간 (초) |
| `--msg-sizes` | 메시지 크기 목록 (comma-separated) |
| `--transports` | transport 목록 (comma-separated) |
| `--runs` | 반복 횟수 |
| `--results-dir` | 결과 디렉토리 경로 |
| `--results-tag` | 결과 파일 이름 태그 |

- multi suite 추가 옵션:

| 옵션 | 의미 |
|------|------|
| `--clients` | client 소켓 수 |

- 옵션 이름을 바인딩별로 다르게 만들면 안 된다.
  - 예: `--message-size`, `--size`, `--msg_size` 등으로 변형 금지

#### 기본값
- 기본값은 `core/perf` 기본값을 단일 기준으로 따른다.
- 바인딩이 다른 기본값을 사용하면 안 된다.
- single suite 기본값:

| 항목 | 기본값 |
|------|--------|
| duration | `5`초 |
| warmup | `2`초 |
| recv | `callback` |
| msg_sizes | `64, 256, 1024, 65536, 131072, 262144` |

- multi suite 기본값:

| 항목 | 기본값 |
|------|--------|
| duration | `5`초 |
| warmup | `2`초 |
| recv | `recv` |
| clients | `100` (STREAM: `10000`) |
| msg_sizes | `64, 256, 1024, 65536, 131072, 262144` (STREAM: `64, 256, 1024, 65536`) |

#### 출력 포맷
- 출력 구조는 core/perf와 동일해야 한다.
- `## Effective Options (start)` 헤더를 반드시 포함한다.
- RESULT line 형식:
  ```
  RESULT,<lib>,<pattern>,<transport>,<size>,<metric>,<value>
  ```
- 필수 metric:
  - `throughput` — Kmsg/s 또는 Kops/s
  - `bandwidth` — MB/s
  - `latency` — mean
  - `latency_p95` — 95th percentile
  - `latency_p99` — 99th percentile
- 마크다운 테이블도 core와 동일한 형태로 출력한다.

#### 결과 파일
- 결과 파일은 다음 디렉토리에 저장한다.
  - `results/{single|multi}/report/`
- 파일 이름 형식:
  ```
  perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt
  ```
- `<platform>`: `linux`, `windows`, `macos`
- `<recv_mode>`: 실제 사용된 모드 (`recv` 또는 `callback`)
- `<tag>`: `--results-tag` 값 (선택)

### Perf Verification Requirements
- perf 코드는 실제 측정 가능한 실행 entry를 제공해야 한다.
- perf 실행 경로는 문서화되어야 한다.
- 새로운 perf 추가 시 다음을 확인해야 한다.
  - 개별 perf 실행 가능
  - 패턴 설명 가능
  - 핵심 메시징 경로가 보이는지 확인
  - core perf / doc perf 정책과 충돌하지 않는지 확인

### Perf Review Checklist
- 이 perf 가 바인딩 라이브러리 비용을 측정하고 있는가
- harness 복잡도가 핵심 비용을 가리고 있지 않은가
- 핵심 로직 가시성이 충분한가
- 각 패턴이 별도 파일로 분리되어 있는가
- 언어 스타일은 반영하되 측정 목적을 해치지 않았는가
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
- blocking send 실패가 예외 또는 오류 경로로 반드시 surface 되는가
- `trySend`가 `Backpressured`/`NotReady`만 결과값으로 반환하고 나머지를 숨기지 않는가
- binding이 truncation/overflow를 선검증하는가
- native 상태 오류를 managed layer가 임의 추론하지 않는가
- reflection test와 behavior test가 같이 있는가
- 값 객체 검증과 호출 직전 검증의 책임 위치가 설명 가능한가
- legacy flag 타입이 public contract에서 제거되었는가
- sample code가 canonical API만 사용하는가
- helper가 blocking send 실패를 swallow 하지 않는가
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
- shallow wrapper 판별 기준:
  - native 함수를 1:1로 감싸기만 하고 새 의미를 추가하지 않는가
  - 호출자가 native 계약(시퀀스, 크기, 인코딩)을 알아야 사용할 수 있는가
  - 동일 규칙이 여러 소켓 타입에 중복 구현되어 있는가
- shallow wrapper를 발견하면:
  - 검증을 값 객체 또는 facade 내부로 이동한다
  - 중복 규칙을 한 모듈에 모은다
  - pass-through만 하는 public 타입은 제거하거나 internal에 병합한다
- 대표 위반 예:
  - RoutingId 길이 검증이 각 소켓 타입마다 중복 → RoutingId 값 객체 하나로 모은다
  - monitor event가 raw int → typed event surface로 승격한다
  - option value가 raw int → enum/boolean/Duration으로 승격한다

#### 4단계: Change Amplification 제거
- 같은 규칙이 여러 곳에 흩어진 지점을 찾아서 한 모듈에 모은다.
- 판별 기준:
  - 정책 하나가 바뀌면 2개 이상의 파일을 고쳐야 하는가
  - 새 소켓 타입을 추가할 때 기존 코드를 N곳 수정해야 하는가
- 대표 위반 예:
  - send failure contract 규칙이 소켓 타입마다 별도 구현
  - blocking/non-blocking 분기가 소켓 타입마다 별도 구현
  - option validation이 각 option setter마다 별도 구현

#### 5단계: Information Hiding 강화
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
  - 이 public 타입을 제거하면 사용자가 잃는 것이 없는가 → shallow wrapper
  - 이 규칙을 고치면 3개 이상의 파일을 건드려야 하는가 → change amplification
  - 사용자가 이 API를 쓰려면 다른 API의 내부 동작을 알아야 하는가 → information leak
  - 같은 능력이 2개 이상의 이름으로 노출되는가 → 중복 surface
  - 사용자가 호출 순서를 기억해야 올바르게 동작하는가 → temporal decomposition

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

3. **Shallow wrapper 제거**
   - native 함수를 1:1로 감싸기만 하는 public 타입이 없다.
   - 모든 public 타입이 검증, ownership, shape 규칙 중 하나 이상을 캡슐화한다.

4. **Change amplification 해소**
   - 동일 규칙이 2개 이상의 모듈에 중복 구현되어 있지 않다.
   - 정책 변경 시 수정해야 할 파일이 1개다.

5. **Information hiding 확보**
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
- single-message convenience
  - public receive/subscribe convenience overload 잔존 여부 점검

### Option Surface Follow-Ups
- raw option bag 잔존 여부 조사
- socket type별 option capability 누수 여부 조사
- option value가 아직 `int`에 머무는 항목 목록화
- context option도 같은 기준으로 typed facade 적용 여부 검토

### Error Contract Follow-Ups
- binding validation 예외와 native 예외가 혼재된 경로 조사
- managed layer가 errno를 임의 해석하는 경로 조사
- `EAGAIN` 외 오류를 잘못 empty/bool 경로로 숨기는 코드 조사
- blocking send 실패를 무시하거나 swallow 하는 helper/sample 조사

### Performance Follow-Ups
- hot path send/recv 경로의 숨은 복사 조사
- `Message`, `Received`, `TopicMessage` 생성 과정의 불필요한 컬렉션/배열
  할당 조사
- callback path와 direct path 비용 차이 조사
- string/topic/routing-id 변환의 인코딩/디코딩 비용 조사
- sample과 helper가 느린 fallback 경로를 canonical usage처럼 노출하는지 조사

### POSD Follow-Ups
- shallow wrapper만 제공하는 public 타입 조사
- 한 규칙이 여러 모듈에 흩어진 change amplification 지점 조사
- 사용자가 internal sequencing을 알아야 하는 temporal API 조사
- facade 뒤로 숨길 수 있는 raw/native 개념 누수 지점 조사

### Ownership and Callback Follow-Ups
- send failure restore 경로와 consume 경로가 문서와 일치하는지 점검
- callback 후 frame validity 계약 재검증
- callback mode와 direct recv 충돌 시 native 계약대로 surface 되는지 점검

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
