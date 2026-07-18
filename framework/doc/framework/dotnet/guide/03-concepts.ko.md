<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Getting Started](02-getting-started.ko.md) | [다음: 기능 맵 — 무엇을, 얼마나 쉽게, 언제](04-feature-map.ko.md)
<!-- framework-adapter-nav:end -->

# 3. 핵심 개념

> 개념의 정식 의미는 [공통 스펙 목차](../../common/README.ko.md)가,
> 인터페이스의 정식 정의는 [spec/handler-interfaces](../../spec/server/languages/dotnet/02-handler-interfaces.ko.md)가
> 다룬다. 이 문서는 그 의미가 `.NET`에서 어떤 모양으로 보이는지 정리한다.

ZLink framework는 **다섯 가지 핵심 개념**으로 선다:
**channel · spot · actor · stream · location**. 나머지 챕터는 전부 이
다섯의 변주다. 낯선 단어가 나오면 먼저 §0 용어 표에서 한 줄로 잡고, §1~§5에서 다섯
개념을 차례로 본다. §6은 이들을 받치는 실행·구성 모델이다.

## 0. 용어 빠르게 잡기

가이드에 자주 나오는 용어를 **한 줄 풀이**로 먼저 잡는다. 다른 챕터에서 낯선 단어가
나오면 이 표로 돌아오면 된다(정식 정의는 위 spec 링크가 다룬다).

| 용어 | 한 줄 풀이 |
|------|-----------|
| **channel(채널)** | 서버 간 호출을 묶는 **논리 이름**. `host:port` 주소 대신 `"orders"` 같은 이름으로 부른다 |
| **역할(capability)** | 한 channel에서 이 앱이 맡는 일 — 서버로 **받기**(EnableServer) / 클라이언트로 **보내기**(EnableClient) / **발행**(Publisher) / **구독**(Subscriber) |
| **handler(핸들러)** | 들어온 메시지를 처리하는 메서드·클래스. `ASP.NET Core`의 컨트롤러 액션과 같은 위치 |
| **client(클라이언트)** | 다른 서비스로 호출을 **보내는** 주입 객체(예: `IZLinkChannelClient`) |
| **request / send / publish** | 각각 **응답 받는 호출** / **응답 없는 단방향 통지** / **여러 구독자에게 발행** |
| **pub/sub · fan-out** | 한 번 발행한 이벤트가 **여러 구독자에게 동시에 퍼지는** 것 |
| **packet name(패킷 이름)** | 같은 channel 안에서 **어느 메시지 종류인지** 구분하는 키 |
| **codec(코덱)** | payload(메시지 본문)를 바이트로 **직렬화/역직렬화**하는 방식(json·protobuf·messagepack) |
| **SPOT(스팟)** | room/zone처럼 **동적으로 생겼다 사라지는 상태 단위**. 한 SPOT의 콜백은 **한 줄로 직렬** 실행돼 lock이 필요 없다 |
| **actor(액터)** | **ID로 식별되는 상태 보유 객체**. 같은 ID로 온 메시지는 늘 같은 인스턴스가 처리 |
| **Entry Spot** | actor가 생성 직후 머무는 **기본 실행 위치** |
| **STREAM(스트림)** | 외부 client(모바일·게임)와의 **연결 지향 양방향 채널**. 연결 수명·재연결을 framework가 관리 |
| **session(세션)** | STREAM 연결 하나에 대응하는 **서버 측 객체** |
| **location store(위치 저장소)** | channel·SPOT 같은 논리 이름을 실제 endpoint로 풀기 위한 외부 저장소 |
| **RoutingId** | 노드·스팟의 **논리 주소**(특정 인스턴스를 가리키는 식별자) |
| **correlation(상관)** | 요청과 그 응답을 **짝지어 주는** 식별 정보. framework가 자동 처리 |
| **deadline / timeout** | 응답을 **얼마나 기다릴지**의 상한 시간 |
| **DI / lifecycle** | `ASP.NET Core` 의존성 주입 + hosted service **시작/종료** 수명 관리 |
| **mesh / sidecar**(비교용) | 서비스와 함께 배치되어 라우팅·분배를 대신하는 **별도 프록시**(Envoy/Istio). ZLink는 이게 없어도 된다 |

## 1. channel — 서버 간 연결

channel은 **서버↔서버 연결을 묶는 논리 이름**이다. 주소(`host:port`)가 아니라
`"orders"` 같은 이름으로 부른다. 현재 공개 표면에서는 server/publisher 역할이 자기
endpoint를 명시하고, client/subscriber 역할은 수동 연결을 쓰거나 별도 location runtime
설계가 확정된 뒤 이름 기반 연결을 사용한다. 배포 값(주소·topology)은 handler가 아니라
**channel 등록**이 소유하므로,
`[ZLinkRequest]` 같은 attribute는 channel 이름을 인자로 받지 않는다.

**channel 종류(kind)** — 서버 간 연결 방식이 다르다:

| 종류 | 등록 | 연결 패턴 |
|------|------|-----------|
| client-server | `AddClientServerChannel` | request-reply · 단방향 send — **ROUTER 서버에 DEALER 클라이언트**가 연결된다 (DEALER 소켓 = client, ROUTER 소켓 = server) |
| fanout | `AddFanoutChannel` | publisher → 다수 subscriber, topic (PUB / SUB) |
| route mesh | `AddRouteMesh` | router ↔ router — routing id로 특정 주소에 라우팅 (`SpotNode`가 이 route mesh로 구성된다: [06-spot](06-spot.ko.md)) |

**소켓 구조 한눈에** — 어떤 소켓이 어떻게 연결되는지가 네 종류의 차이다.

- **client-server** — ROUTER 서버 **하나**에 DEALER 클라이언트 **여럿**이 연결되는 비대칭 구조.

```mermaid
%%{init: {'themeVariables': {'edgeLabelBackground':'transparent'}}}%%
graph LR
    C1["client A<br/>DEALER"] --> S["server<br/>ROUTER"]
    C2["client B<br/>DEALER"] --> S
    C3["client C<br/>DEALER"] --> S
```

- **fanout** — PUB 하나가 발행하면 같은 메시지가 SUB 여럿에 동시에 퍼진다.

```mermaid
%%{init: {'themeVariables': {'edgeLabelBackground':'transparent'}}}%%
graph LR
    P["publisher<br/>PUB"] --> S1["subscriber A<br/>SUB"]
    P --> S2["subscriber B<br/>SUB"]
    P --> S3["subscriber C<br/>SUB"]
```

- **route mesh** — ROUTER끼리 연결되어, **routing id로 지정한 주소에만** 보낸다(분산 아님). `SpotNode`가 이 구조로 구성된다.

```mermaid
%%{init: {'themeVariables': {'edgeLabelBackground':'transparent'}}}%%
graph LR
    R["router<br/>ROUTER"] -->|"routing id = A"| A["node A<br/>ROUTER"]
    R -->|"routing id = B"| B["node B<br/>ROUTER"]
```

**역할(capability)** — 한 channel에서 이 앱이 맡는 일:

| 역할 | 의미 | 비고 |
|------|------|------|
| `EnableServer(endpoint)` | 이 channel의 request/send를 local handler가 받는다 | endpoint 인자 필수 |
| `EnableClient()` | 이 channel로 request/send를 내보낸다 | outbound 전용 앱 가능 |
| `EnablePublisher(endpoint)` | 이 channel로 이벤트를 publish 한다 | endpoint 인자 필수 |
| `EnableSubscriber()` | 이 channel의 이벤트를 구독한다 | |

한 channel이 여러 역할을 가질 수 있다(예: 서버이면서 다른 노드의 이벤트를 구독).
server/publisher는 외부가 접근할 endpoint가 필요하므로 `EnableServer(endpoint)`·
`EnablePublisher(endpoint)`에 endpoint를 직접 넘기고, client/subscriber는 필요 없다. request/send/pub-sub 사용법과 handler 노출·연결
제어 전체는 [05-channel-messaging](05-channel-messaging.ko.md)이 다룬다.

> **주의:** channel 이름과 handler **group 이름**은 서로 다른 namespace 다. group은
> 코드 안 논리 묶음(`"api"`)이고, channel은 배포 식별자(`"tictactoe.api"`)다.

## 2. spot — 상태 단위

spot은 room/zone/stage처럼 **동적으로 생겼다 사라지는 상태 단위**다. 한 spot에
들어오는 packet · timer · actor 콜백은 **한 줄로 직렬 실행**되므로, spot이 소유한
상태에 lock 없이 접근한다. "어디서 도는가"가 channel handler와 다르다(§6).

| | channel handler | SPOT handler |
|---|---|---|
| 위치 | channel server/subscriber 역할 | `SpotNode` 안의 entry/user Spot |
| 실행 | 서로 다른 요청은 동시에 실행 가능 | 같은 SPOT 안에서는 직렬 실행 |
| 상태 | 공유 상태를 직접 멤버에 두지 않음 | SPOT이 상태를 직접 소유 |

한 SPOT에 들어오는 모든 일은 **단일 큐**를 통과해 한 줄로 처리된다 — 그래서 상태에
lock이 없다.

```mermaid
%%{init: {'themeVariables': {'edgeLabelBackground':'transparent'}}}%%
graph LR
    M1["packet"] --> Q["단일 큐<br/>직렬 실행"]
    M2["timer"] --> Q
    M3["actor 콜백"] --> Q
    Q --> ST["SPOT 상태<br/>(lock 불필요)"]
```

상세(등록·lifecycle·timer·outbound)는 [06-spot](06-spot.ko.md).

## 3. actor — ID로 식별되는 상태 객체

actor는 **ID로 식별되는 상태 보유 객체**다. 같은 ID로 온 메시지는 늘 같은
인스턴스가 처리한다. 외부 client session을 actor에 바인딩하면 **연결 서버(세션)와
로직 서버(actor)를 분리**할 수 있다 — 연결을 받는 노드와 도메인 로직을 도는 노드를
나누는 패턴이다.

```mermaid
%%{init: {'themeVariables': {'edgeLabelBackground':'transparent'}}}%%
graph LR
    S1["msg · id=42"] --> RT{"actor id<br/>라우팅"}
    S2["msg · id=42"] --> RT
    S3["msg · id=7"] --> RT
    RT -->|id=42| A42["actor 42<br/>(같은 인스턴스)"]
    RT -->|id=7| A7["actor 7"]
```

상세는 [07-actor-spot](07-actor-spot.ko.md).

## 4. stream — 외부 client 연결

stream은 모바일·게임 같은 **외부 client와의 연결 지향 양방향 채널**이다. 서버
간 channel(§1)과 달리 연결 수명·재연결·heartbeat를 framework가 관리하고, 연결
하나가 서버 측 **session** 객체에 대응한다.

```mermaid
%%{init: {'themeVariables': {'edgeLabelBackground':'transparent'}}}%%
graph LR
    C["모바일·게임<br/>client"] <-->|"연결 (heartbeat·재연결 관리)"| SV["STREAM 서버"]
    SV --- SE["session<br/>(연결 1개 = 객체 1개)"]
```

상세는 [09-stream](09-stream.ko.md).

## 5. location — 주소 해석

앱 코드는 가능하면 channel 이름 같은 논리 이름만 알고, 실제 peer 주소(`host:port`)는
배포가 공유하는 **location store** 가 푼다. 각 서버는 시작할 때 자기 위치(descriptor row)를
store에 자동 등록하고, client는 channel 이름만으로 store에서 상대를 찾아 연결한다.
서버가 늘고 줄면 연결도 따라간다 — 사용법은 [10-location](10-location.ko.md), 계약은
[공통 스펙](../../spec/server/40-location-runtime.ko.md)이 다룬다.

store 없이 endpoint를 역할 등록에 직접 적는 수동 연결도 그대로 지원한다(개발·테스트·
소규모 고정 배포, [05-channel-messaging §6](05-channel-messaging.ko.md)). 같은 역할에서
두 방식을 섞을 수는 없다.
이름 기반 자동 연결은 location runtime 설계가 정식 공개 계약으로 확정된 뒤 별도 guide에서
다룬다.

> **샘플에서 보기 — [TicTacToe](../../common/sample/tictactoe/README.ko.md).** 다섯 개념이
> 한 샘플에 전부 나오는 가장 작은 예다. Play 서버의 등록 코드 한 곳에서 다섯이 만난다.
>
> | 개념 | TicTacToe에서 |
> |---|---|
> | channel | Api 서버와 Play 서버가 `AddClientServerChannel`로 방 생성·배정을 주고받는다 |
> | spot | 대국 한 판이 `TicTacToeGame` spot 하나 — 두 플레이어의 수가 이 안에서 직렬 처리된다 |
> | actor | 플레이어가 actor이고, 재접속해도 같은 actor로 이어져 두던 판을 계속한다 |
> | stream | client가 Play 서버의 STREAM endpoint에 직접 붙어 수를 두고 push를 받는다 |
> | location | Redis location store가 Api↔Play 연결을 자동으로 잇는다 — 주소가 코드에 없다 |
>
> 다섯 개념이 각각 어떤 문제를 푸는지는 위에서 봤고, **함께 놓이면 어떤 모양인지**는
> 이 샘플이 보여 준다.

## 6. 보조 — 실행·구성 모델

위 다섯 개념을 받치는 공통 동작이다. 여기서 한 번 짚고, 상세는 각 챕터가 소유한다.

### 6.1 핸들러 모델 — 채널/HTTP 핸들러 vs SPOT 핸들러

핸들러는 실행 컨텍스트에 따라 두 종류로 나뉘고, 구조와 수명이 완전히 다르다.

- **채널/HTTP 핸들러** — 독립 class. interface 기반
  (`IZLinkRequestHandler<TRequest, TReply>` 등)이나 attribute 기반
  (`[ZLinkHandlerGroup]` + `[ZLinkRequest]`/`[ZLinkSend]`/`[ZLinkPublish]` 메서드)으로
  작성하고, 의존성은 **생성자 주입**으로 받는다. 수명은 **transient**(요청마다 새로),
  실행은 채널별 **async 수신 루프**에서(HTTP 핸들러는 `ASP.NET Core` 요청 파이프라인)
  실행된다. 그래서 가변 도메인 상태를 핸들러 멤버에 두지 않는다.
- **SPOT 핸들러** — spot 클래스(`IZLinkSpot`)의 메서드가 아니라, 그 spot에
  **바인딩된 별도 핸들러 class** 다. spot 용 핸들러 인터페이스를 구현하고, 그 spot의
  `Configure()`에서 등록한다(어떤 인터페이스·API가 무엇을 맡는지는 아래 예제 주석 참고).
  같은 SPOT 안에서는 **전체 직렬 실행**이라 상태에 lock이 필요 없다.

```csharp
// SPOT 핸들러는 대상 spot 타입을 첫 제네릭 인자로 받는 별도 class 다.
public sealed class GetStateHandler
    : IZLinkSpotRequestHandler<RoomSpot, GetStateRequest, GetStateReply>  // request 핸들러: <대상 spot, 요청, 응답>
{
    public ValueTask<GetStateReply> HandleAsync(RoomSpot spot, GetStateRequest req, CancellationToken ct)
        => ValueTask.FromResult(new GetStateReply(spot.Occupants));       // 첫 인자로 대상 spot 인스턴스를 받는다
}
// (이 밖에 IZLinkSpotPacketHandler<TSpot, TMessage> = send packet, IZLinkSpotTimerHandler<TSpot> = timer)

public void Configure()   // 등록은 그 spot의 Configure() 안에서 한다
{
    Context.Handlers.AddPacket<GetStateHandler>();              // send/request packet 핸들러 등록
    Context.Handlers.AddActorPacket<MoveHandler, RoomActor>(); // actor가 보낸 packet 핸들러 등록 (actor 사용 시)
}
```

| | 채널/HTTP 핸들러 | entry spot | room spot |
|---|---|---|---|
| 기반 | 독립 class (interface/attribute) | `IZLinkSpot` 구현 | `IZLinkSpot` 구현 |
| 수명 | transient (요청마다) | `SpotNode`와 동일 (영속) | 상태 단위와 동일 (영속) |
| 실행 | 비동기 (채널별 수신 루프·HTTP 파이프라인) | **전체 직렬** — 단일 큐 | **전체 직렬** — 단일 큐 |
| 공유 상태 | 핸들러에 두지 않음 | 큐 안에서 안전 | 락 없이 안전 |
| 역할 | 요청 처리·위임 | 배정·매칭·할당 | 도메인 상태 소유·처리 |
| 계약 | interface 구현 / attribute 메서드 | `Configure()` + `Context.Handlers` 등록 | `Configure()` + `Context.Handlers` 등록 |
| DI | 생성자 주입 | `IZLinkSpotContext`로 채널 client 연결 | `IZLinkSpotContext`로 채널 client 연결 |

**실행 모델 비교** — 같은 3개 요청이 두 핸들러에서 어떻게 도는가:

```mermaid
%%{init: {'themeVariables': {'edgeLabelBackground':'transparent'}}}%%
graph TB
    subgraph N ["채널/HTTP 핸들러 — 비동기 (수신 루프 · HTTP 파이프라인)"]
        direction LR
        NR1["req A"] --> NW1["핸들러 A ▶ 처리"]
        NR2["req B"] --> NW2["핸들러 B ▶ 처리"]
        NR3["req C"] --> NW3["핸들러 C ▶ 처리"]
    end
    subgraph S ["SPOT 핸들러 — 직렬 (단일 큐)"]
        direction LR
        SR1["req A"] --> SQ["단일 큐"]
        SR2["req B"] --> SQ
        SR3["req C"] --> SQ
        SQ --> SEX["A → B → C<br/>하나씩 순서대로"]
    end
```

위 그림은 **처리 순서**를 보여준다. 채널/HTTP 핸들러는 요청마다 독립 실행되고, SPOT
핸들러는 같은 SPOT 큐에 들어온 일을 하나씩 처리한다. 아래 그림은 같은 상황을
**스레드 점유** 관점에서 다시 본 것이다. 각 event는 async task가 되고, task가
`await`에 도달하면 스레드를 붙잡지 않은 채 응답을 기다린다.

```mermaid
%%{init: {'themeVariables': {'edgeLabelBackground':'transparent'}}}%%
graph LR
    subgraph EV ["SPOT event마다 async task 하나"]
        E1["message A"]
        E2["timer tick"]
        E3["message B"]
    end
    E1 --> T1["ValueTask A"]
    E2 --> T2["ValueTask T"]
    E3 --> T3["ValueTask B"]
    T1 --> POOL["worker 스레드 풀<br/>(소수)"]
    T2 --> POOL
    T3 --> POOL
    POOL -.->|"await 도달 → suspend"| WAIT["대기 중 task<br/>(스레드 점유 0)"]
    WAIT -.->|"응답 도착 → resume"| POOL
```

채널/HTTP 핸들러는 요청마다 새 인스턴스로 비동기 처리되니 핸들러 멤버에 가변 상태를 두면
경합이 난다. SPOT 핸들러는 단일 큐로 **한 번에 하나씩** 처리하니 상태에 lock이
필요 없다. 다만 직렬 실행은 "스레드 하나를 계속 점유한다"는 뜻이 아니다. SPOT의
event(message·timer)는 각각 task가 되어 소수의 worker 스레드에 다중화되고,
`await`에 걸린 task는 스레드를 **놓는다**(blocking 아님). 그래서 스레드 몇 개로
대기 중인 task 수천 개를 떠받칠 수 있다.

Entry Spot과 user/domain Spot은 둘 다 순서 보장을 제공하지만, 메시지가 들어오는 경로와
실행 기준이 다르다. user/domain Spot은 `spotRid`로 주소 지정되는 도메인 상태 단위이고,
Entry Spot의 actor packet은 대상 actor mailbox 기준으로 처리된다. 자세한 차이는
[06-spot](06-spot.ko.md)의 실행 직렬화 설명에서 다룬다.

가변 도메인 상태(게임 룸 등)는 **SPOT**, 불변 구성(topology)은 싱글톤 서비스, 공유
인프라(캐시·카운터)는 싱글톤 + 자체 동기화에 둔다. SPOT 핸들러 작성과 직렬 실행
보장은 [06-spot](06-spot.ko.md), 채널 핸들러 노출은 [05-channel-messaging](05-channel-messaging.ko.md).

**handler 노출은 명시적이다** — 두 방법 중 하나로 channel에 붙인다(방법별 코드는 아래 주석 참고).

```csharp
// 방법 A — attribute로 묶고 group 이름으로 붙인다
[ZLinkHandlerGroup("api")]                  // 이 class의 핸들러 메서드들을 "api" group으로 묶는다
public sealed class ApiHandlers { /* [ZLinkRequest] / [ZLinkSend] 메서드들 */ }

options.AddClientServerChannel("orders")
    .AddHandlerGroup("api");                // 위 group을 이 channel에 노출

// 방법 B — 핸들러 타입을 channel에 개별 등록
options.AddClientServerChannel("orders")
    .AddRequestHandler<GetOrderHandler>();  // 핸들러 하나씩 직접 등록
```

다음 구성 오류는 lazy first-call로 미루지 않고 **host startup에서
즉시** 예외로 막힌다: channel 이름 중복, 같은 channel 안 `kind + packet name` 중복,
client 역할에 자동 연결(store)·수동 연결 둘 다 없음, 허용되지 않는 handler 반환형.

### 6.2 실행 모델 — `async`/`await`, `ValueTask`

프레임워크 전반의 비동기 값은 `ValueTask` / `ValueTask<T>`로 표현된다. handler와
outbound 호출은 비동기 경계를 가진다. send/push는 one-way `Submit(...)` 호출로 표현하고,
송신 수락과 backpressure 처리는 framework 내부 책임으로 둔다. request의
`Async<TReply>(...)`는 **remote reply가 도착할 때까지 기다려** 그 reply를 돌려준다.
`RequestToChannel(...).Timeout(...)`은 그 **reply 대기 시간**의 상한을 정한다. 규칙은 하나다 —
**런타임(핸들러) 스레드에서는 `await`, blocking(`.Result`/`.GetAwaiter().GetResult()`)은
테스트·클라이언트 시나리오에서만.**

```csharp
public async ValueTask<CreateGameReply> HandleAsync (
    CreateGameRequest request, ZLinkRequestContext context, CancellationToken ct)
{
    // 런타임(핸들러) 스레드 — await로 비운다. blocking(.Result/.GetAwaiter().GetResult())은 금지.
    var room = await _client
        .RequestToChannel("tictactoe.play", new CreateRoomRequest(request.GameName))
        .Async<CreateRoomReply>(ct);   // request → remote reply가 도착할 때까지 await로 대기, 그 reply를 받는다
    return new CreateGameReply (room.RoomId, room.GameName);
}
```

채널 핸들러는 채널별 async 수신 루프에서, HTTP 핸들러는 `ASP.NET Core` 요청
파이프라인에서 실행된다. 핸들러가 `await`에 도달하면 async 상태 머신만 멈추고(suspend)
실행 스레드는 풀로 돌아가 다른 일을 처리한다. SPOT 핸들러는 §6.1처럼 단일 큐로 직렬
실행돼, 같은 SPOT 큐는 그 handler 완료 전까지 다음 callback을 시작하지 않는다.

아래 타임라인은 같은 흐름을 시간순으로 본 것이다. A가 `await`로 suspend 되면 같은
스레드가 즉시 B를 처리하고, A는 응답이 오면 resume 된다.

```mermaid
sequenceDiagram
    participant W as worker 스레드
    participant H1 as 핸들러 A (async)
    participant CH as Play 채널
    participant H2 as 핸들러 B (async)

    W->>H1: HandleAsync() 실행
    activate H1
    H1->>CH: await Request(...).Async()
    deactivate H1
    Note over H1: suspend — 응답 대기 (스레드 점유 없음)
    Note over W: 워커는 즉시 다음 일로
    W->>H2: HandleAsync() 실행
    activate H2
    H2-->>W: return (완료)
    deactivate H2
    CH-->>H1: 응답 도착 → resume
    activate H1
    H1-->>W: return (완료)
    deactivate H1
```

그래서 비동기 호출을 콜백 없이 **동기식 코드처럼 위에서 아래로** 쓰면서도, worker
몇 개로 수많은 동시 요청을 처리한다. 같은 코드를 `.Result`로 막으면 스레드 하나가
통째로 잠들기 때문에 핸들러 안에서 금지한다. 실패는 `await` 경로에서
예외로 던져진다.

### 6.3 host 수명주기

framework runtime은 `ASP.NET Core`의 **hosted service** 로 host 시작/종료에 묶인다.
channel·SPOT·STREAM runtime은 startup에서 등록한 역할을 보고 생성되어 shutdown에서
정리된다.

```mermaid
stateDiagram-v2
    direction LR
    state "구성 단계" as configure
    state "서비스 중" as serving
    state "종료" as stopping
    [*] --> configure: WebApplication.CreateBuilder()
    configure: Services / AddZLinkFramework
    configure: channel / SPOT / stream / registry
    configure --> serving: app.Run()
    serving: channel·SPOT·stream dispatch
    serving --> stopping: host shutdown
    stopping: hosted service stop → runtime 정리
    stopping --> [*]
```

- **구성 단계** — `app.Run()` 전에 모든 선언을 끝낸다. 잘못된 구성은 §6.1처럼 host
  startup에서 예외로 거부된다.
- **종료** — host shutdown 신호가 오면 hosted service `stop()` → channel/SPOT/STREAM
  runtime 정리 순으로 내려간다.
- 백그라운드 작업은 표준 `IHostedService`로 같은 수명주기에 편입시킨다.

### 6.4 구성: DI 컨테이너 · 구성 표면 지도

- **DI 컨테이너** — handler·client·filter는 모두 `ASP.NET Core`의 **동일한 DI
  컨테이너**에서 생성자 주입으로 만들어진다. 별도 컨테이너를 두지 않고
  `builder.Services`에 그대로 등록한다.
- **구성 표면 지도** — 어디서 무엇을 선언하는지:

  | 표면 | 역할 | 다루는 장 |
  |------|------|-----------|
  | `builder.Services.AddZLinkFramework(...)` | channel/SPOT/STREAM 선언 | 4~8장 |
  | `options.AddClientServerChannel(...)` / `AddFanoutChannel` | channel 종류·역할 선언 | [4장](05-channel-messaging.ko.md) |
  | runtime event handler | monitoring event 관찰 | [9장](11-monitoring.ko.md) |

## 7. 더 깊이

- request/send/pub-sub 전체 사용법: [05-channel-messaging](05-channel-messaging.ko.md)
- 전체 인터페이스/attribute/context: [spec/handler-interfaces](../../spec/server/languages/dotnet/02-handler-interfaces.ko.md)
- 기능 선택 기준: [04-feature-map](04-feature-map.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Getting Started](02-getting-started.ko.md) | [다음: 기능 맵 — 무엇을, 얼마나 쉽게, 언제](04-feature-map.ko.md)
<!-- framework-adapter-nav:bottom:end -->
