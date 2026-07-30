# DeliveryDispatch Sample Scenario

[샘플 목록](../README.ko.md)

> 이 문서는 모든 framework 언어가 공유하는 DeliveryDispatch 샘플 시나리오를 정의한다.
> 언어별 샘플은 이 문서를 기준으로 서버 역할, 메시지 흐름, message 계약,
> ZLink 요소, smoke 검증 기준을 맞춘다.

## 1. 목적

DeliveryDispatch는 배송 배차와 배송 상태 추적을 보여 주는 실시간 업무형 샘플이다.
고객 client는 HTTP로 배송을 만들고 WebSocket 또는 stream session으로 상태 변경을
기다린다. 내부 서버들은 ZLink channel, entry spot, actor, stream session을 사용해서
배차 요청, 배송원 제안, timeout 재배정, 고객별 상태 push를 처리한다.

이 샘플은 실제 배송 플랫폼을 그대로 구현하는 것을 목표로 하지 않는다. 실무에서 자주
만나는 "요청 생성, 수행자 선택, 특정 사용자 연결로 push, 응답 timeout 후 재시도" 같은
기능을 구현할 때 각 책임이 ZLink framework의 어떤 기능에 대응되는지 보여 주는 것이
목적이다. 따라서 배송 업무 규칙 자체보다 HTTP 경계, channel, entry spot, actor,
stream session binding이 하나의 업무 흐름 안에서 어떻게 연결되는지를 기준으로 읽어야
한다.

이 샘플의 직접 모델은 음식 배달, 퀵 배송, 택배 같은 배송 서비스다. 같은 구조는
택시 호출, 현장 출동, 방문 서비스처럼 "요청을 만들고 수행자를 배정한 뒤 상태를
실시간으로 고객에게 보여 주는" 도메인에도 적용할 수 있다.

샘플로 구현할 때 확인할 핵심은 아래와 같다.

- 고객 client는 배송 생성 HTTP API와 상태 수신 stream endpoint를 사용한다.
- Dispatch server는 외부 HTTP 요청을 내부 dispatch channel message로 바꾸고, 같은 server
  안의 worker가 배송원 선택, timeout, 재배정을 처리한다.
- CourierSession server는 배송원 client의 stream 연결을 받고, courier id를 global
  `ActorId`로 사용해 `ActorManager.GetOrCreate`를 호출한다. Actor가 Ready가 되면 같은
  operation에서 받은 `ActorRef`에 현재 session을 bind한다.
- CourierActorNode는 배송원 actor와 entry spot을 가지며, 배송 제안을 actor로
  전달한다. 배송원 actor는 자기 courier id로 bound session에 제안을 push한다.
- Tracking 서버는 배송 상태 event를 기록하고 고객 actor에 보낼 상태 알림을 만든다.
- CustomerGateway server는 고객 stream 연결을 받으면 customer id를 global `ActorId`로 사용해
  `ActorManager.GetOrCreate`를 호출한다. Actor가 Ready가 되면 현재 session을 bind하고
  상태 알림을 client에 push한다.
- client scenario는 성공 배차와 timeout 재배차를 모두 검증한다.

DeliveryDispatch는 CRUD API 샘플이 아니다. 중요한 상태가 여러 역할 사이를 바로 이동하고
고객 session까지 도달하는지를 보여 주는 샘플이다.

## 2. 왜 실시간성이 중요한가

배송 배차에서 메시지 지연은 단순한 화면 갱신 지연이 아니다. 가까운 배송원이 다른 일을
잡기 전에 제안을 보내야 하고, 배송원이 응답하지 않으면 빠르게 다음 배송원에게 넘겨야 한다.
고객도 배정, 픽업, 배송 완료 상태가 늦게 보이면 문제가 생겼다고 느낀다.

| 늦게 전달된 메시지 | 생기는 문제 |
|--------------------|-------------|
| 배송 제안 | 가까운 배송원이 다른 일을 잡아 배차 기회를 놓친다. |
| 수락 또는 timeout | 재배정이 늦어져 고객 대기 시간이 늘어난다. |
| 픽업 완료 | 고객과 운영자가 아직 준비 중이라고 오해한다. |
| 배송 완료 | 정산, 리뷰 요청, 다음 작업 배정이 늦어진다. |

샘플은 위 지점을 보여 주기 위해 `delivery-success`와 `delivery-reassign` 두 흐름을
반드시 포함한다.

## 3. 기존 웹 방식

배송 시스템을 일반적인 웹 방식으로 만들면 client side와 server side를 나누어 구성한다.
client side는 사용자가 직접 보는 화면이고, server side는 HTTP API, WebSocket/SSE 서버,
worker, 저장소처럼 client 요청을 받아 처리하는 서버 역할이다.

```mermaid
flowchart LR
    subgraph ClientSide[Client side: user-facing screens]
        direction TB
        CustomerScreen[Customer screen]
        CourierScreen[Courier screen]
        OperatorScreen[Operator screen]
    end

    subgraph ServerSide[Server side]
        DeliveryApi[Delivery API]
        CourierApi[Courier API]
        CourierRealtime[Courier Realtime Server]
        Realtime[WebSocket/SSE Server]
        DispatchQueue[Dispatch Queue]
        Worker[Dispatch Worker]
        RetryJob[Timeout and Retry Job]
        CourierRegistry[Courier Session Registry]
        OfferStore[Courier Offer Store]
        EventBus[Status Event Bus]
        Database[(Delivery Database)]
    end

    CustomerScreen -->|create delivery| DeliveryApi
    CustomerScreen <-->|status stream| Realtime
    CourierScreen -->|decision and status API| CourierApi
    CourierScreen <-->|offer stream| CourierRealtime
    OperatorScreen -->|manual review| DeliveryApi

    DeliveryApi -->|write delivery| Database
    DeliveryApi -->|enqueue assignment| DispatchQueue
    DispatchQueue --> Worker
    Worker -->|select courier| Database
    Worker -->|create offer| CourierApi
    CourierApi -->|write offer| OfferStore
    CourierApi -->|push command| CourierRealtime
    CourierRealtime -->|lookup connection| CourierRegistry
    CourierRealtime -->|push offer| CourierScreen
    CourierApi -->|decision and status| Database
    CourierApi -->|decision event| EventBus
    RetryJob -->|timeout check| Database
    RetryJob -->|timeout check| OfferStore
    RetryJob -->|reassign| DispatchQueue
    Worker -->|status event| EventBus
    EventBus --> Realtime
```

이 그림에서 courier 관련 구성은 배송원 화면에 offer를 push하기 위한 일반적인 웹
구조를 뜻한다. `Courier Session Registry`는 courier id와 현재 연결을 매핑하고,
`Courier Realtime Server`는 그 매핑을 사용해 특정 배송원 화면에 offer를 보낸다.
배송원 응답과 상태 변경은 다시 `Courier API`를 통해 저장소와 event 흐름으로 들어간다.

이 방식은 익숙하고 운영 도구가 많다. 다만 배차 결정, 배송원 응답, timeout 재시도,
고객 push가 서로 다른 계층에 흩어지면 한 배송의 흐름을 여러 로그와 저장소 상태로
따라가야 한다. 고객 WebSocket만 빠르게 만들어도 배차 worker가 수락 실패를 늦게 알거나
상태 이벤트가 WebSocket 서버까지 늦게 도달하면 실시간성 문제는 남는다.

## 4. ZLink 샘플의 대체 지점

ZLink는 고객의 HTTP 요청이나 WebSocket 연결을 없애지 않는다. 외부 경계는 그대로 웹 기술을
사용한다. 대신 내부 배차 메시징, 배송원별 offer push, 고객별 상태 push를 ZLink 역할
메시지로 구성한다.

| 기존 웹 시스템 구성 | ZLink 샘플의 대응 | 설명 |
|--------------------|------------------|------|
| Delivery API + dispatch worker | `Dispatch server` | 고객 HTTP 요청을 받고, 같은 server 안의 worker가 후보 배송원을 고른 뒤 courier actor route로 제안을 보낸다. |
| Courier API 또는 worker | `CourierSession server` + `CourierActorNode` | CourierSession이 global `ActorId`로 courier actor를 보장하고 같은 operation에서 받은 `ActorRef`에 session을 bind한다. courier actor는 framework가 유지하는 bound session으로 제안을 push한다. |
| Delivery event table | `Tracking server` + evidence store | 상태 이벤트를 기록하고 고객에게 보낼 알림을 만든다. |
| Session map 또는 socket registry | `CustomerActor` + bound session | 고객 actor가 현재 stream session을 기억해 특정 고객에게만 status를 push한다. |
| WebSocket/SSE server | `CustomerGateway server` | 고객 stream 연결을 받고 global `ActorId`로 customer actor를 보장한 뒤 현재 session을 bind한다. |
| Actor entry point | `CustomerEntrySpot`, `CourierEntrySpot` | Framework가 새 actor의 initial membership을 승인받는 lifecycle 지점이다. 위치 조회나 directory 역할을 하지 않는다. |
| Courier placement | `ActorManager` + `CourierActor` | Framework가 stable type, capacity와 node weight로 owner를 선택한다. Application은 NodeRid를 고르지 않는다. |

```mermaid
flowchart LR
    subgraph ClientSide[Client side]
        Customer[Customer screen]
        CourierClientA[Courier screen A]
        CourierClientB[Courier screen B]
    end

    subgraph ServerSide[Server side]
        DispatchServer["Dispatch<br/>server"]
        CourierSessionServer["CourierSession<br/>server"]
        CourierActorServer1["Courier actor MeshNode 1<br/>server"]
        CourierActorServer2["Courier actor MeshNode 2<br/>server"]
        CustomerGatewayServer["CustomerGateway<br/>server"]
        TrackingServer["Tracking<br/>server"]
        Evidence[(Evidence log)]
    end

    Customer --> DispatchServer
    Customer --> CustomerGatewayServer
    CourierClientA --> CourierSessionServer
    CourierClientB --> CourierSessionServer

    DispatchServer -->|DispatchWorker module targets courier actor| CourierActorServer1
    DispatchServer -->|DispatchWorker module targets courier actor| CourierActorServer2
    DispatchServer -->|DispatchWorker module emits status| TrackingServer

    CourierSessionServer -->|bind session to courier actor| CourierSpotServer1
    CourierSessionServer -->|bind session to courier actor| CourierSpotServer2
    CourierSpotServer1 -->|actor pushes through bound session| CourierSessionServer
    CourierSpotServer2 -->|actor pushes through bound session| CourierSessionServer
    CourierSessionServer -->|offer push| CourierClientA
    CourierSessionServer -->|offer push| CourierClientB

    TrackingServer -->|EvidenceStore module writes| Evidence
    TrackingServer -->|Tracking module notifies customer actor| CustomerGatewayServer
    CustomerGatewayServer -->|status stream push| Customer
```

위 그림은 server 배치와 server 사이의 의존성 방향만 보여준다. 각 server 안의 module 구성은
아래 표와 도메인 흐름에서 설명한다. 각 요청의 시간 순서와 응답 흐름은 아래 도메인 흐름에서
따로 설명한다.

`CourierSession server`는 배송원 client의 stream 연결을 받는 server다.
`CourierActorNode 1/2`는 실제 actor와 entry spot을 가진 node다. 두 역할은 한 프로세스에 함께
배치할 수도 있지만, 아키텍처 설명에서는 논리적으로 분리한다. 그래야 client 연결을 받는
책임과 actor placement, actor 메시지 진입점 책임이 섞이지 않는다.

## 5. 서버 구성

DeliveryDispatch의 Channel 역할과 물리 연결은 [공통 topology 기준](../README.ko.md#channel-역할과-물리-topology-기준)을
따른다. Object routing은 두 RouteMesh로 나눈다.

- `deliverydispatch.courier`: Dispatch·CourierSession은 Object Client이고
  CourierActorNode 1/2는 Object Server다.
- `deliverydispatch.customer`: Tracking은 Object Client이고 CustomerGateway는 Object Server다.

업무 Channel은 Object RouteMesh와 분리한다. `deliverydispatch.dispatch`는
CourierActorNode가 Client이고 Dispatch가 Server인 독립 ClientServer다.
`deliverydispatch.tracking`도 Dispatch가 Client이고 Tracking이 Server인 독립
ClientServer다. Dispatch는 courier Object Client이므로 같은 RouteMesh에 Channel Server를
등록하지 않는다.

| 프로세스 | 구성 요소 | 책임 |
|----------|-----------|------|
| `DeliveryDispatch.Dispatch` | courier Object Client, 독립 `deliverydispatch.dispatch` Server, 독립 `deliverydispatch.tracking` Client, HTTP API, dispatch worker | `POST /deliveries`를 받고 배차 요청 접수, 배송원 제안, timeout, 재배정, 상태 event 생성을 맡는다. |
| `DeliveryDispatch.CourierSession` | stream server, courier session, courier Object Client | 배송원 client stream 연결을 받고 `ActorManager.GetOrCreate`로 courier actor를 보장한 뒤 현재 session route를 bind한다. |
| `DeliveryDispatch.CourierActorNode1/2` | courier Object Server, 독립 `deliverydispatch.dispatch` Client, courier entry spot, courier actor | Framework가 선택한 node에서 배송원 actor를 만들고, actor가 bound session으로 제안을 push한다. |
| `DeliveryDispatch.Tracking` | customer Object Client, 독립 `deliverydispatch.tracking` Server, evidence store | 상태 event 기록과 global `CustomerId` 대상 고객 알림 생성을 맡는다. |
| `DeliveryDispatch.CustomerGateway` | stream server, customer Object Server, customer entry spot, customer actor | 고객 연결을 받고 `ActorManager.GetOrCreate`로 customer actor를 보장한 뒤 session bind와 status push를 맡는다. |
| `DeliveryDispatch.Client` | HTTP client, stream connector | 배송 생성, subscription, status notify 검증을 수행한다. |
| `Location Store` | framework location store 계약의 공유 저장소 구현체(예: Redis) | 서버 endpoint peer discovery(자동 연결)와 actor/session 위치 조회를 담으며, 등록·조회·lifecycle 정책은 framework가 소유. |

언어별 샘플은 프로세스 이름과 프로젝트 이름을 언어 관용에 맞게 바꿀 수 있다. 다만 위 역할
분리는 유지해야 한다. `Dispatch server`는 HTTP edge와 dispatch worker를 한 server에 둘 수
있지만, `CourierSession server`와 `CourierActorNode`는 논리 책임을 분리해서 설명해야
한다. 한 프로세스에 함께 배치하더라도 문서와 코드 책임은 client stream 연결, actor placement,
actor 실행 위치가 섞이지 않게 나눈다.

## 6. 사용하는 ZLink 요소

| 역할 | 사용하는 요소 |
|------|---------------|
| `Location Store` | 공유 저장소 기반 peer discovery와 readiness 확인 |
| `Dispatch server` | HTTP endpoint, courier Object Client, 두 독립 ClientServer의 handler/client, background dispatch worker |
| `CourierSession server` | stream node, courier Object Client, courier session callback |
| `CourierActorNode 1/2` | courier Object Server, 독립 dispatch ClientServer client, courier entry spot, courier actor |
| `Tracking server` | customer Object Client, 독립 tracking ClientServer handler, evidence store |
| `CustomerGateway server` | stream node, customer Object Server, customer entry spot, customer actor |
| Client | HTTP client와 stream connector wait API |

역할 간 channel은 다음 이름을 기준으로 둔다.

| 이름 | Framework 요소 | 연결 |
|------|----------------|------|
| `deliverydispatch.dispatch` | 독립 ClientServer Channel | `CourierActorNode` Client → ready `Dispatch` Server |
| `deliverydispatch.tracking` | 독립 ClientServer Channel | `Dispatch` Client → ready `Tracking` Server |
| `deliverydispatch.customer` | RouteMesh | `CustomerEntrySpot`에서 customer actor 관리 |
| `deliverydispatch.courier` | RouteMesh | `CourierSession/DispatchWorker module -> global ActorId -> CourierActor` |

두 ClientServer Channel은 `deliverydispatch.courier`나 `deliverydispatch.customer`
MeshNode의 ChannelName membership이 아니다. 따라서 Object Client에 Channel Server를
등록하지 않는다.

`deliverydispatch.courier`는 배송원마다 하나씩 늘어나는 Channel이 아니다. 모든 배송원 actor가
같은 mesh 안에 있고, courier id가 어느 MeshNode의 actor에 들어갈지는 framework 배치가 정한다.
courier별 session binding은 Framework가 해당 courier actor와 함께 관리한다.

`DispatchWorker module`은 courier id를 전역 `ActorId`로 사용해 actor를 보장하고 offer를
보낸다. 어느 CourierActorNode에서 actor를 materialize할지는 framework의 capacity와 node weight,
Location Store가 결정한다. application request와 설정에는 owner NodeRid가 없으며, 전송할 때도
physical node를 먼저 선택하지 않는다
([10 §5](../../spec/07-channel-topology.ko.md), [24 §3](../../spec/16-spot-address-messaging.ko.md)).
`CourierEntrySpot`은 actor 생성과 session bind를 framework 경로에 연결하지만, 호출자에게 owner
node 선택 책임을 노출하지 않는다.

stream client가 다시 연결될 때 두 역할은 같은 규칙을 사용한다.

- **courier**는 전역 `ActorId`로 `GetOrCreate`한다. Framework가 반환한 exact `ActorRef`는
  새 session bind에 그대로 사용할 수 있지만, 이 값은 placement 입력이 아니다.
- **customer**도 전역 `ActorId`로 `GetOrCreate`하고 같은 operation의 `ActorRef`에 session을 bind한다.

이렇게 해야 재연결해도 사용자가 보던 actor 상태가 유지되고, binding만 최신 연결로 바뀐다.

일반 message 전송은 `ActorId`만 받는 Actor Client를 사용한다. Application은 `ActorRef`,
owner `NodeRid`나 위치 snapshot을 보관하지 않는다. Framework가 Location Store에서 current
owner를 찾는다. Client wire에도 `ActorRef`, `NodeRid`와 session route를 노출하지 않는다.

`ActorManager`나 Actor Client를 호출하는 host에는 해당 RouteMesh의 Object Client role이
필요하다. CourierSession·Dispatch는 courier Object Client를, Tracking은 customer Object
Client를 등록한다. Object Server role을 가진 CustomerGateway와 CourierActorNode는 자기
Actor factory와 Entry Spot을 등록한다.

이 샘플의 courier·customer Actor factory는 relocation policy를 `DisableRelocation`으로 등록한다.
DeliveryDispatch는 actor 생성, direct messaging과 session bind를 설명하는 샘플이며,
relocation이나 Message Follow 동작을 시연하거나 검증하지 않는다.

### 6.1 Entry Spot 대기 규칙

`CustomerEntrySpot`과 `CourierEntrySpot`은 Actor가 처음 진입하는 lifecycle 지점이다.
Entry Spot과 Entry Spot Actor에서는 `Yield`를 사용할 수 없다. Request나 worker 결과를
기다려야 하면 `Async`로 현재 job 안에서 완료한다. `Yield`를 호출하면 operation을 제출하지
않고 `InvalidOperation`으로 끝난다.

Entry Spot callback은 짧게 유지한다. Actor 생성에 필요하지 않은 외부 I/O는
`ActorManager.GetOrCreate`를 호출하기 전에 caller가 처리하거나, Actor가 Ready가 된 뒤
Actor handler에서 처리한다. 생성 callback 안에 장기 HTTP·DB·다른 service 왕복을 넣어
Entry lifecycle을 지연시키지 않는다.

이 샘플의 Entry Spot은 생성 중인 Actor를 `OnCreateActorAsync` 인자로 직접 받는다. Callback은
그 Actor의 local initial state만 설정하고 승인 또는 거절을 반환한다. 아직 Ready가 아니므로
callback 안에서 `ActorManager.FindAsync`를 호출하면 안 된다. Session bind는
`ActorManager.GetOrCreate`가 Ready 결과를 반환한 뒤 caller가 수행한다.

`Yield` 예제는 `SpotWide` User Spot의 shared turn을 반납할 수 있는
[Bingo §7.1](../bingo/README.ko.md)에만 둔다.

## 7. Message 계약

공통 message 계약은 언어 중립 schema로 읽는다. 언어별 구현은 record, class, struct,
interface, type alias처럼 자기 언어에 맞는 표현으로 같은 필드와 의미를 구현한다.
이 표의 message 이름과 필드는 .NET 샘플을 다른 framework 언어 샘플로 포팅할 때 맞춰야
하는 공통 샘플 계약이다. 언어별 타입 표현과 파일 배치는 달라도 message 의미, 필드,
성공/timeout 흐름에서의 역할은 유지한다.

### 7.1 고객 HTTP와 stream

| Message | 방향 | 필드 | 의미 |
|---------|------|------|------|
| `CreateDeliveryReq` | Client -> Dispatch server HTTP | `DeliveryId`, `CustomerId`, `PickupAddress`, `DropoffAddress` | 새 배송 생성을 요청한다. |
| `CreateDeliveryRes` | Dispatch server HTTP -> Client | `DeliveryId` | Dispatch server 접수 결과를 반환한다. |
| `SubscribeDeliveryReq` | Client -> CustomerGateway stream | `DeliveryId` | 현재 stream session이 특정 delivery 상태를 받겠다고 요청한다. |
| `SubscribeDeliveryRes` | CustomerGateway stream -> Client | `DeliveryId` | subscription이 등록됐음을 알린다. |
| `DeliveryStatusNotify` | CustomerGateway stream -> Client | `DeliveryId`, `Status`, `CourierId`, `OccurredAt` | delivery 상태 변경을 push한다. |

### 7.2 Dispatch와 Courier

| Message | 방향 | 필드 | 의미 |
|---------|------|------|------|
| `AssignDeliveryMsg` | Dispatch server HTTP API module -> dispatch channel module | `DeliveryId`, `CustomerId`, `PickupAddress`, `DropoffAddress` | 배차 대상 배송을 내부 dispatch 흐름에 넣는다(응답 없는 one-way send). |
| `BindCourierSessionReq` | Courier client -> CourierSession server stream | `CourierId` | courier id의 actor를 보장하고 현재 stream session에 bind하도록 요청한다. |
| `BindCourierSessionRes` | CourierSession server -> Courier client | `CourierId` | Actor가 Ready이고 session bind가 완료됐음을 반환한다. Actor 위치는 반환하지 않는다. |
| `OfferDeliveryMsg` | DispatchWorker module -> CourierActor direct | `DeliveryId`, `CourierId`, `Attempt`, `PickupAddress`, `DropoffAddress` | Global `CourierId`로 특정 배송원 actor에게 제안을 보낸다(**응답 없는 one-way send**). `Attempt`는 이 배송의 몇 번째 제안인지다. |
| `CourierDecisionMsg` | Courier client -> CourierSession server -> CourierActor | `DeliveryId`, `CourierId`, `Accepted`, `Reason` | 배송원 client가 stream session을 통해 수락 또는 거절 결정을 보낸다. actor는 현재 제안의 `Attempt`와 결합해 배차 결과를 만든다. |
| `OfferDeliveryResultMsg` | CourierActor -> DispatchWorker module | `DeliveryId`, `CourierId`, `Attempt`, `Accepted`, `Reason` | 배송원의 결정을 배차 쪽으로 돌려준다(**응답 없는 one-way send**). `Attempt`가 현재 제안과 다르면 늦게 도착한 결정이므로 버린다. |

### 7.3 Tracking과 actor/session bind

| Message | 방향 | 필드 | 의미 |
|---------|------|------|------|
| `DeliveryStatusChangedReq` | DispatchWorker module -> Tracking server | `DeliveryId`, `CustomerId`, `Status`, `CourierId`, `OccurredAt` | 상태 event를 Tracking 서버에 기록한다. `CustomerId`는 Tracking이 `DeliveryStatusUpdatedMsg`를 **어느 고객 actor에게** 보낼지 정하는 값이다 — 이 필드가 없으면 Tracking이 고객을 out-of-band로 추측해야 하고(하드코딩), 그건 계약 위반이다. |
| `DeliveryStatusChangedRes` | Tracking server -> DispatchWorker module | `DeliveryId`, `Status` | Tracking 서버가 상태 event를 처리했음을 반환한다. |
| `DeliveryStatusUpdatedMsg` | Tracking server -> CustomerActor direct | `DeliveryId`, `CustomerId`, `Status`, `CourierId`, `OccurredAt` | Global `CustomerId`로 고객 actor에 상태 변경을 전달한다(응답 없는 one-way send). |

`ActorRef`, owner `NodeRid`와 session route는 application message 계약에 포함하지 않는다.
CourierSession과 CustomerGateway는 `ActorManager.GetOrCreate`의 Ready 결과에 포함된 exact
`ActorRef`를 해당 session bind 호출에만 사용하고 저장하거나 client에 반환하지 않는다.

`Status` 값은 최소한 아래 값을 포함한다.

| 값 | 의미 |
|----|------|
| `Assigned` | 첫 배송원에게 제안이 전달됐다. |
| `Reassigned` | 이전 배송원이 timeout되어 다른 배송원에게 다시 제안됐다. |
| `Accepted` | 배송원이 제안을 수락했다. |
| `PickedUp` | 배송원이 물건을 픽업했다. |
| `Delivered` | 배송이 완료됐다. |

## 7.4 배송 제안은 왜 request/reply가 아닌가 (구현 지침)

**배송원의 결정을 request의 응답으로 받으면 안 된다.** 제안과 결정 사이에는 사람이 화면을 보고
버튼을 누르는 시간이 들어간다. 그 시간을 응답 시간에 묶으면 요청 하나가 그동안 **열린 채로 남고**,
그 요청을 처리하던 실행 줄(spot의 직렬 줄)이 결정이 올 때까지 잡힌다. 서버는 client에게 request를
걸 수 없고 push만 할 수 있으므로, 결정은 애초에 **client가 자기 타이밍에 보내는 별개의 in-bound
메시지**다. 인과가 이미 끊겨 있는 것을 하나의 RPC로 위장하는 셈이다.

그래서 제안(`OfferDeliveryMsg`)도, 결정 결과(`OfferDeliveryResultMsg`)도 **둘 다 one-way send**로
보낸다. 어느 쪽도 상대의 응답을 기다리지 않는다.

기다리지 않는 대신, 진행 중인 제안의 **상태를 store에 기록**하고 그 기록이 다음 단계를 정한다.
`DispatchWorker`가 소유하는 상태는 배송마다 아래 한 줄이다.

```text
DeliveryOffer {
  DeliveryId, CourierId, Attempt: int, Deadline: timestamp,
  Status: Offered | Accepted | Rejected | Expired
}
```

처리 루프는 다음 순서로 실행된다.

```mermaid
flowchart TD
    A[Receive AssignDeliveryMsg] --> B[Select first courier]
    B --> C[Store offered attempt and Assigned event]
    C --> D[Send OfferDeliveryMsg]
    D --> E[Target MeshNode relays to courier actor]
    E --> F[Actor pushes offer to bound session]
    F --> G{Courier decision arrives?}
    G -->|Accepted| H[Store Accepted, PickedUp, Delivered events]
    G -->|Rejected| I[Select next courier]
    G -->|Deadline expired| J[Mark attempt Expired]
    I --> K{Candidate remains?}
    J --> K
    K -->|Yes| L[Store Reassigned event and next attempt]
    L --> D
    K -->|No| M[Store Failed event]
    G -->|Stale attempt| N[Discard late decision]
```

**지켜야 할 것:**

- **어느 handler도 배송원의 결정을 기다리지 않는다.** 결정 대기를 위해 스레드를 재우거나
  (`condition_variable`, `Future.get()`) task를 붙잡고 있으면 안 된다. 그러면 그 실행 줄로 오는
  다른 제안·조회가 전부 그 배송원의 반응 시간만큼 밀린다.
- **제안 시한은 `DispatchWorker`가 소유한다.** MeshNode가 시한을 세고 "거절"을 만들어 돌려주면
  배차 정책이 노드에 숨는다. 노드는 제안을 전달하고 결정을 돌려줄 뿐이다.
- **`Attempt`로 늦은 결정을 막는다.** 시한이 지나 재제안한 뒤 이전 배송원의 결정이 도착할 수 있다.
  기록된 현재 `Attempt`와 다른 결정은 버린다.
- **상태 기록이 곧 재개 지점이다.** 노드가 비정상 종료된 뒤 재시작되어도 `Offered`와 지난 `Deadline` 기록으로
  같은 sweeper 경로로 이어서 진행된다.

## 8. 도메인 흐름

### 8.1 성공 배차

```mermaid
sequenceDiagram
    participant CustomerClient as Customer Client
    participant CourierClient as Courier Client A
    participant CourierSession as CourierSession
    participant ObjectRuntime as Framework object runtime
    participant CourierEntry as CourierEntrySpot
    participant CourierActor as CourierActor courier-a
    participant CustomerSession as CustomerSession
    participant CustomerEntry as CustomerEntrySpot
    participant CustomerActor as CustomerActor
    participant DispatchHttp as Dispatch HTTP
    participant DispatchWorker as DispatchWorker
    participant Tracking as Tracking

    CourierClient->>CourierSession: connect stream
    CourierClient->>CourierSession: BindCourierSessionReq(courier-a)
    CourierSession->>ObjectRuntime: ActorManager.GetOrCreate(courier-a)
    ObjectRuntime->>CourierEntry: OnCreateActorAsync(actor, create request)
    CourierEntry-->>ObjectRuntime: Accept
    ObjectRuntime-->>CourierSession: Ready ActorRef
    CourierSession->>ObjectRuntime: Bind session to exact ActorRef
    CourierSession-->>CourierClient: BindCourierSessionRes
    CustomerClient->>CustomerSession: SubscribeDeliveryReq(delivery-success)
    CustomerSession->>ObjectRuntime: ActorManager.GetOrCreate(customer id)
    ObjectRuntime->>CustomerEntry: OnCreateActorAsync(actor, create request)
    CustomerEntry-->>ObjectRuntime: Accept
    ObjectRuntime-->>CustomerSession: Ready ActorRef
    CustomerSession->>ObjectRuntime: Bind session to exact ActorRef
    CustomerSession-->>CustomerClient: SubscribeDeliveryRes(delivery-success)
    CustomerClient->>DispatchHttp: POST /deliveries
    DispatchHttp->>DispatchWorker: AssignDeliveryMsg (enqueue work)
    DispatchHttp-->>CustomerClient: CreateDeliveryRes(deliveryId)
    DispatchWorker->>ObjectRuntime: SendToActor(courier-a, OfferDeliveryMsg)
    ObjectRuntime->>CourierActor: dispatch offer (one-way)
    CourierActor->>CourierSession: push offer by bound session
    CourierSession->>CourierClient: push offer
    Note over DispatchWorker,CourierClient: 서버는 아무 실행 줄도 잡지 않고 결정을 기다린다
    CourierClient-->>CourierSession: accepted
    CourierSession->>ObjectRuntime: SendToActor(courier-a, CourierDecisionMsg)
    ObjectRuntime-->>CourierActor: dispatch decision
    CourierActor-->>DispatchWorker: OfferDeliveryResultMsg(Attempt=1, Accepted) (one-way)
    DispatchWorker->>Tracking: DeliveryStatusChangedReq Assigned
    Tracking-->>DispatchWorker: DeliveryStatusChangedRes Assigned
    DispatchWorker->>Tracking: DeliveryStatusChangedReq Accepted
    Tracking-->>DispatchWorker: DeliveryStatusChangedRes Accepted
    DispatchWorker->>Tracking: DeliveryStatusChangedReq PickedUp
    Tracking-->>DispatchWorker: DeliveryStatusChangedRes PickedUp
    DispatchWorker->>Tracking: DeliveryStatusChangedReq Delivered
    Tracking-->>DispatchWorker: DeliveryStatusChangedRes Delivered
    Note over Tracking,CustomerClient: 각 상태 변경을 global CustomerId로 CustomerActor에 보낸다
    Tracking->>ObjectRuntime: SendToActor(customer id, DeliveryStatusUpdatedMsg)
    ObjectRuntime->>CustomerActor: dispatch notify
    CustomerActor->>CustomerSession: DeliveryStatusNotify(Delivered)
    CustomerSession->>CustomerClient: status stream notify(Delivered)
```

### 8.2 Timeout 재배차

```mermaid
sequenceDiagram
    participant CustomerClient as Customer Client
    participant CourierClientA as Courier Client A
    participant CourierClientB as Courier Client B
    participant CourierSession as CourierSession
    participant ObjectRuntime as Framework object runtime
    participant CourierActorA as CourierActor courier-a
    participant CourierActorB as CourierActor courier-b
    participant CustomerSession as CustomerSession
    participant CustomerActor as CustomerActor
    participant DispatchHttp as Dispatch HTTP
    participant DispatchWorker as DispatchWorker
    participant Tracking as Tracking

    CourierClientA->>CourierSession: connect stream
    CourierClientA->>CourierSession: BindCourierSessionReq(courier-a)
    CourierSession->>ObjectRuntime: GetOrCreate(courier-a) and bind
    CourierSession-->>CourierClientA: BindCourierSessionRes
    CourierClientB->>CourierSession: connect stream
    CourierClientB->>CourierSession: BindCourierSessionReq(courier-b)
    CourierSession->>ObjectRuntime: GetOrCreate(courier-b) and bind
    CourierSession-->>CourierClientB: BindCourierSessionRes
    CustomerClient->>CustomerSession: SubscribeDeliveryReq(delivery-reassign)
    CustomerSession->>ObjectRuntime: GetOrCreate(customer id) and bind
    CustomerSession-->>CustomerClient: SubscribeDeliveryRes(delivery-reassign)
    CustomerClient->>DispatchHttp: POST /deliveries
    DispatchHttp->>DispatchWorker: AssignDeliveryMsg (enqueue work)
    DispatchHttp-->>CustomerClient: CreateDeliveryRes(deliveryId)
    DispatchWorker->>DispatchWorker: DeliveryOffer{Attempt=1, Deadline} 기록
    DispatchWorker->>Tracking: DeliveryStatusChangedReq Assigned
    Tracking-->>DispatchWorker: DeliveryStatusChangedRes Assigned
    DispatchWorker->>ObjectRuntime: SendToActor(courier-a, OfferDeliveryMsg)
    ObjectRuntime->>CourierActorA: dispatch offer (one-way)
    CourierActorA->>CourierSession: push offer by bound session
    CourierSession->>CourierClientA: push offer
    CourierClientA--x CourierSession: 시한까지 응답 없음
    Note over DispatchWorker: sweeper가 Deadline 지난 Offered 기록을 훑어 Expired 처리
    DispatchWorker->>Tracking: DeliveryStatusChangedReq Reassigned
    Tracking-->>DispatchWorker: DeliveryStatusChangedRes Reassigned
    Tracking->>ObjectRuntime: SendToActor(customer id, DeliveryStatusUpdatedMsg)
    ObjectRuntime->>CustomerActor: dispatch notify
    CustomerActor->>CustomerSession: DeliveryStatusNotify(Reassigned)
    CustomerSession->>CustomerClient: status stream notify(Reassigned)
    DispatchWorker->>ObjectRuntime: SendToActor(courier-b, OfferDeliveryMsg)
    ObjectRuntime->>CourierActorB: dispatch offer (one-way)
    CourierActorB->>CourierSession: push offer by bound session
    CourierSession->>CourierClientB: push offer
    CourierClientB-->>CourierSession: accepted
    CourierSession->>ObjectRuntime: SendToActor(courier-b, CourierDecisionMsg)
    ObjectRuntime-->>CourierActorB: dispatch decision
    CourierActorB-->>DispatchWorker: OfferDeliveryResultMsg(Attempt=2, Accepted) (one-way)
    DispatchWorker->>Tracking: DeliveryStatusChangedReq Accepted
    Tracking-->>DispatchWorker: DeliveryStatusChangedRes Accepted
    DispatchWorker->>Tracking: DeliveryStatusChangedReq Delivered
    Tracking-->>DispatchWorker: DeliveryStatusChangedRes Delivered
    Note over Tracking,CustomerClient: Accepted·Delivered도 위와 같은 push 체인을 개별로 탄다 (마지막 Delivered push만 표시)
    Tracking->>ObjectRuntime: SendToActor(customer id, DeliveryStatusUpdatedMsg)
    ObjectRuntime->>CustomerActor: dispatch notify
    CustomerActor->>CustomerSession: DeliveryStatusNotify(Delivered)
    CustomerSession->>CustomerClient: status stream notify(Delivered)
```

## 9. 구현 구조

DeliveryDispatch는 업무형 샘플이므로 domain 흐름과 framework adapter가 뒤섞이지 않게 한다.
언어별 문법과 build system은 달라도 아래 책임 분리를 기준으로 둔다.

```text
Server/Dispatch/
  Application/
    Dispatch/
      DispatchWorker
      DispatchWorkQueue
      CourierSelectionPolicy
  Infrastructure/
    Http/
      DeliveryEndpoint
    ZLink/
      Handlers/
        AssignDeliveryHandler
      Clients/
        CourierClient
        TrackingClient

Server/CourierSession/
  Infrastructure/
    ZLink/
      Sessions/
        CourierSession
      Clients/
        CourierActorClient

Server/CourierActorNode/
  Infrastructure/
    ZLink/
      Actors/
        CourierActor
      Spots/
        CourierEntrySpot

Server/Tracking/
  Domain/
    DeliveryTracking/
      DeliveryStatusHistory
      DeliveryTrackingState
  Infrastructure/
    ZLink/
      Handlers/
        DeliveryStatusChangedHandler
      Stores/
        EvidenceStore

Server/CustomerGateway/
  Infrastructure/
    ZLink/
      Actors/
        CustomerActor
      Spots/
        CustomerEntrySpot
      Sessions/
        CustomerSession
      Clients/
        CustomerActorClient
      Handlers/
        SubscribeDeliveryHandler
        DeliveryStatusUpdatedHandler
      Stores/
        CustomerSessionDirectory
```

작은 언어별 샘플은 디렉토리를 더 단순하게 둘 수 있다. 그래도 아래 기준은 유지한다.

- Dispatch server는 HTTP 변환, dispatch channel, worker를 한 server 안에 둘 수 있다.
- DispatchWorker module은 배차 흐름과 timeout 재시도를 소유한다.
- CourierSession server는 배송원 stream 연결을 받고 `ActorManager.GetOrCreate`가 반환한
  Ready `ActorRef`에 session을 bind한다.
- CourierActorNode는 actor 생성과 actor 메시지 처리를 맡는다. Session binding은
  Framework가 관리한다.
- Tracking은 상태 event 기록과 고객 알림 생성을 맡는다.
- CustomerGateway server는 고객 stream 연결을 받고 `ActorManager.GetOrCreate`가 반환한
  Ready `ActorRef`에 session을 bind한다.

## 10. Client self-check 기준

언어별 client scenario는 성공 로그만 출력하면 안 된다. request 응답과 push payload를
직접 검증해야 한다.

필수 검증은 아래와 같다.

- client가 stream endpoint에 연결한 뒤 `SubscribeDeliveryReq`를 보내고
  `SubscribeDeliveryRes`의 `DeliveryId`를 확인한다.
- `delivery-success` 생성 응답이 같은 `DeliveryId`를 반환하는지 확인한다.
- `courier-a`와 `courier-b` actor가 전역 `ActorId`로 생성되고 두 actor가 `CourierSession server`의
  session route로 client에 offer를 push하는지 확인한다. 어느 physical node가 owner인지는 성공
  조건으로 사용하지 않는다.
- `OnCreateActorAsync`는 callback 인자로 받은 actor만 초기화하며, Ready 전 `FindAsync`를
  호출하거나 `ActorRef`를 application cache에 저장하지 않는지 확인한다.
- CourierSession·Dispatch·Tracking host가 자신이 호출하는 Actor RouteMesh에 Object Client
  role을 등록했는지 startup contract test로 확인한다.
- Client request와 response에 `ActorRef`, owner `NodeRid`와 session route가 포함되지 않는지
  wire contract test로 확인한다.
- `delivery-success`에 대해 `Assigned`, `Accepted`, `PickedUp`, `Delivered`가 순서대로
  도착하는지 확인한다.
- `delivery-reassign` 생성 응답이 같은 `DeliveryId`를 반환하는지 확인한다.
- `delivery-reassign`에 대해 `Assigned`, `Reassigned`, `Accepted`, `Delivered`가 순서대로
  도착하는지 확인한다.
- `delivery-reassign`의 `Accepted`와 `Delivered` 상태는 `courier-b`가 처리했음을 확인한다.
- server evidence check가 두 delivery의 상태 순서를 누락 없이 기록했는지 확인한다.

push message 대기는 stream connector의 public wait interface를 사용한다. notification
수집용 inbox나 로그 queue는 결과 출력과 추가 검증을 위해 둘 수 있지만, push 도착을 기다리는
기준 경로가 되어서는 안 된다.

## 11. Smoke 실행 기준

언어별 runner는 아래 순서를 따른다.

1. 공유 location store(예: Redis)가 준비됐는지 확인한다.
2. Tracking 서버를 시작하고 `deliverydispatch.tracking`의 public runtime readiness가 ready인지 확인한다.
3. CustomerGateway 서버를 시작한다.
4. CourierSession 서버를 시작한다.
5. CourierActorNode 1과 2를 시작한다.
6. Dispatch 서버를 시작한다.
7. Client scenario가 배송원 A/B stream 연결, 성공 배차, timeout 재배차를 실행한다.
8. Evidence check가 상태 순서를 검증한다.

샘플 성공 로그는 아래 의미를 포함해야 한다.

```text
topology=ready
deliverydispatch-reassignment=completed
deliverydispatch-server-evidence=completed
deliverydispatch=completed
```
