<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework for .NET](../README.ko.md) | [다음: Getting Started](02-getting-started.ko.md)
<!-- framework-adapter-nav:end -->

# 1. ZLink Framework for .NET - 개요

> 이 문서는 `.NET` 가이드의 진입점이다. 가이드는 `ASP.NET Core` 개발자가
> ZLink Framework의 기능을 **읽고 바로 따라 쓸 수 있도록** 개념과 사용법을
> 직접 설명한다. 개념의 **언어 중립 정식 정의**는 [공통 스펙
> 개요](../../common/spec/overview.ko.md)가, `.NET` public API의 **정식 계약**은
> [spec/](../../common/spec/languages/dotnet/handler-interfaces.ko.md) 문서가 다룬다. 두 표기가 어긋나면
> spec이 우선이다.

## 1. 무엇인가 — 한 줄 정의

`ZLink Framework`는 **기존 메이저 프레임워크와 통합되는 실시간 메시징
프레임워크**다. Spring 위에 Spring MVC가 웹 계층으로 올라가듯, `ASP.NET Core` 위에
ZLink Framework가 **실시간 메시징 계층**으로 올라간다. 별도 런타임이나 전용 서버로
갈아타는 것이 아니라, 쓰던 DI·hosted service·설정·로깅 모델 안에 그대로 들어온다.

이 계층이 제공하는 것은 별도 **gateway나 전용 로드밸런서 없이** 쓰는 논리
`channel name` 기준의 서버 간 호출, pub/sub, 그리고 실시간 상태 단위인 `SPOT`(room ·
stage · zone)·actor·`STREAM`(외부 client 연결)이다(용어가 낯설면
[03-concepts §0](03-concepts.ko.md)의 한 줄 풀이를 먼저 본다). 개발자는 HTTP/gRPC를
쓰던 감각으로 **handler, client, filter**를 작성하고, 연결·위치 조회·라우팅·재연결·
correlation은 framework가 처리한다.

> **ZLink는 여러 언어에서 같은 계약으로 쓰는 framework다.** 같은 계층이 Spring
> (Java/Kotlin)과 NestJS(Node) 위에도 똑같이 올라가고, 호출 계약이 언어 중립 wire
> protocol(ZMP) + codec + 논리 channel/packet이라 서로 다른 언어로 구현된 서비스가
> 같은 channel 위에서 상호 호출한다(예: room 서버 C++, API 서버 .NET·Java). 이
> 가이드는 `.NET` 기준이며 `.NET` 구현을 reference implementation(기준 구현)으로
> 삼는다. 자세한 cross-language 모델은 [14-grpc-alternative §2.1](14-grpc-alternative.ko.md)이 다룬다.

## 2. 언제 필요한가

### 실시간 게임 서버를 만들 때

**무엇이 어려운가.** 게임 서버에는 웹의 `ASP.NET Core`/Spring 같은 정형화된
프레임워크가 없다. 우연이 아니라 이유가 있다.

- **장르마다 요구하는 네트워크 토폴로지가 다르다.** 웹은 어떤 서비스든 "client
  요청 → 서버 응답" 한 모양이라 프레임워크가 표준화될 수 있었다. 게임은 다르다 —
  보드게임은 방 단위 매칭과 턴 진행, MORPG는 room/stage 서버와 매칭·로비의 분리,
  MMORPG는 zone/field 서버 mesh와 대규모 브로드캐스트, FPS는 소규모 세션의 저지연
  tick 루프를 요구한다. **장르가 토폴로지를 결정하니 하나의 정형이 없고**, 팀마다
  소켓 위에서 자기 토폴로지를 다시 짠다.
- **상태가 메모리에 유지된다.** 웹은 상태를 DB에 두고 stateless로 scale-out하면
  되지만, 게임은 빠른 처리를 위해 room·참가자 상태를 **in-memory**에 두고 멀티
  스레드로 로직을 실행한다. 그 순간 lock, 경합, 데드락, "어느 스레드가 이 room을
  만지는가"라는 동기화 문제가 업무 로직 안으로 스며든다.
- **연결 자체가 관리 대상이다.** 유저는 장기 연결을 유지한다. 소켓 framing과
  세션 수명을 직접 다루고, 재접속하면 어느 서버의 어느 room에 있었는지 이어 줘야
  하고, 배포·축소 때 접속 유저와 진행 중인 게임 상태를 유지해야 한다.

그래서 지금까지 선택지는 둘이었다 — 이걸 전부 직접 만들거나, 게임 서버 엔진이라는
**자체 런타임의 섬**에 들어가 로직 작성 방식·설정·배포·운영을 엔진 방식으로 다시
배우거나.

**실제로는 어떻게 만들어 왔나.** 엔지니어링 블로그·커뮤니티·클라우드 벤더 문서에
공개된 게임 서버 구조도들을 묶으면 대략 네 갈래다. 어느 구조도든 login/auth,
gateway, DB cache 같은 상자가 반복해서 등장하지만 — 그걸 받쳐 주는 공통
프레임워크는 없어서, 팀은 자기 장르의 갈래를 골라 그 구조를 소켓부터 다시 만든다.

```mermaid
%%{init: {'themeVariables': {'edgeLabelBackground':'transparent'}}}%%
flowchart TB
  subgraph mmo["① zone 분할형 — MMORPG"]
    direction LR
    C1["client"] --> GW["gateway"] --> Z1["zone 서버 A"]
    GW --> Z2["zone 서버 B"]
    Z1 <-.->|"경계 이동 이관<br/>(자체 프로토콜)"| Z2
  end
  subgraph rm["② lobby + room형 — 캐주얼·MO·보드게임"]
    direction LR
    C2["client"] --> LO["lobby / 매칭"] --> R1["room 서버들<br/>(방 단위 상태)"]
  end
  subgraph ded["③ matchmaker + dedicated형 — 세션 기반"]
    direction LR
    C3["client"] --> MM["matchmaker<br/>(ticket 큐)"] --> FL["dedicated 서버 fleet<br/>(판마다 프로세스 할당)"]
  end
  subgraph act["④ 분산 actor 서비스형 — 메타·소셜 백엔드"]
    direction LR
    C4["client"] --> AP["API"] --> AC["분산 actor<br/>(플레이어·세션 단위 상태)"]
  end
  mmo ~~~ rm
  ded ~~~ act
```

- ① zone 분할: gateway가 packet을 zone(scene) 서버로 넘기고, 경계를 넘는
  플레이어를 zone 서버끼리 이관한다([Alibaba Cloud MMO 아키텍처](https://alibaba-cloud.medium.com/alibaba-cloud-mmo-gaming-solution-architecture-8f771567f63f)류).
- ② lobby+room: 유저를 zone/lobby로 받고 room 단위로 묶는다
  ([SmartFoxServer zones & rooms](https://docs2x.smartfoxserver.com/Overview/zones-room-architecture)·Photon류).
- ③ matchmaker+dedicated: 매칭 ticket이 모이면 fleet에서 서버 하나를 할당하고
  client에 접속 정보(IP/port)를 줘 직접 붙게 한다([GameLift Realtime
  구조](https://docs.aws.amazon.com/gamelift/latest/developerguide/realtime-architecture.html)·[Open Match + Agones](https://aws.amazon.com/solutions/guidance/game-server-hosting-using-agones-and-open-match-on-amazon-eks/)류).
- ④ actor 서비스: 플레이어·세션 상태를 분산 actor로 둔다(Halo 4/5의
  [Orleans](https://www.infoq.com/presentations/halo-4-orleans/) 사례).

**ZLink가 제공하는 것.** 어려움 하나하나에 기능이 대응한다.

| 어려움 | ZLink 기능 | 자세히 |
|--------|------------|--------|
| 장르별 토폴로지를 소켓부터 자작 | **channel 조합으로 토폴로지 선언** — 1:N 요청/응답, fan-out, 노드 지목 route mesh, room 단위 spot mesh를 등록 몇 줄로 조합, 연결은 location store가 자동 유지 | [§3 아키텍처](#아키텍처--어디에-올라가고-무엇을-선언하나) · [05](05-channel-messaging.ko.md)·[06](06-spot.ko.md)·[10](10-location.ko.md) |
| in-memory 상태의 lock·경합 | **SPOT 직렬 실행** — 한 room의 모든 메시지를 하나의 실행 줄로 세워 순서대로 실행. lock이 업무 로직에서 사라진다 | 아래 코드 · [06](06-spot.ko.md) |
| 소켓 framing·세션 수명 자작 | **STREAM** — 연결 수명·framing·packet codec을 framework가 소유(TCP/TLS/WS/WSS) | [09](09-stream.ko.md) |
| 재접속 유저 위치 추적 | **actor binding** — 재접속한 새 연결이 같은 actor로 이어진다 | [08](08-actor-session.ko.md) |
| 배포 때 유저 튕김 | **graceful drain** — 신규 차단, actor handoff, 진행 중 마무리 후 종료. 앱 코드 0줄 | [12](12-operations.ko.md) |

그리고 위의 **네 갈래가 전부 같은 선언 모델 위의 조합**이 된다. 갈래마다 소켓부터
다시 만들 필요가 없다.

| 갈래 | ZLink로는 |
|------|-----------|
| ① zone 분할 | zone = `AddSpotMesh` + 노드 지목 route mesh. 경계를 넘는 플레이어는 **actor 크로스노드 transfer**가 이관을 대신한다([07](07-actor-spot.ko.md)) |
| ② lobby + room | 입장·매칭 = Entry Spot, 방 = room spot을 `GetOrCreate`로 — [Bingo](samples/bingo-game-sample.ko.md)가 이 갈래 그대로다 |
| ③ matchmaker + dedicated | 매칭 = channel handler, 판 = 아무 노드에나 `GetOrCreate`되는 room spot. client는 STREAM으로 직접 접속하고, fleet 증설 자체는 K8s가 그대로 맡는다 |
| ④ actor 서비스 | ZLink actor — 같은 virtual actor 모델을 .NET 전용이 아니라 **폴리글랏 + 메이저 프레임워크 통합**으로 |

> 경계 하나는 그대로다: 트위치 FPS의 **초저지연 UDP snapshot netcode** 자체는
> ZLink 범위 밖이다(STREAM은 TCP/TLS/WS/WSS). 그 게임에서도 매칭·로비·메타·소셜은
> 위 갈래들로 덮인다. 경계 전체는 [14장](14-grpc-alternative.ko.md) §4가 다룬다.

**게임 서버 엔진·서비스와는 어떻게 다른가.** 직접 만들지 않는 길로는 엔진과
관리형 서비스가 있다. 이들이 제공하는 것을 영역별로 놓고 보면 ZLink의 자리가
분명해진다.

| 제공 영역 | 대표 제품 | 제공 형태 |
|-----------|-----------|-----------|
| 연결·전송 최적화 — 소켓/세션 관리, 암호화·압축, TCP/UDP 병행, 네트워크 I/O와 로직 스레드 분리 | [ProudNet](https://docs.proudnet.com/proudnet.eng) | 전용 서버 모듈 + client SDK |
| room·lobby·매칭 — room 생성/조회, lobby, 매치 초대 | [Photon](https://www.photonengine.com/)·[SmartFoxServer](https://docs2x.smartfoxserver.com/Overview/zones-room-architecture) | 자체 런타임 위의 room 모델 |
| 호스팅·fleet — dedicated 서버 할당, autoscaling, 매치메이킹 규칙 엔진(FlexMatch) | [AWS GameLift](https://aws.amazon.com/gamelift/servers/)·Agones | 클라우드 관리형 서비스 |
| 소셜·메타 배터리 — 친구, 리더보드, 그룹, 채팅 | [Nakama](https://heroiclabs.com/nakama-gamelift/) | 백엔드 서버 제품 |

ZLink는 이 중 **연결·세션(STREAM), room·상태 단위(SPOT), 서버 간 메시징(channel),
참가자 상태(actor), 무중단 종료(drain)** 를 제공한다 — 단, 전용 런타임이나 관리형
서비스가 아니라 **쓰던 메이저 프레임워크 위의 라이브러리 계층**으로.

- **호스팅·fleet은 ZLink의 몫이 아니다.** K8s든 GameLift든 그 위에서 ZLink 서버가
  돌면 된다 — 호스팅 서비스와 경쟁하지 않고 조합된다.
- **매치메이킹 규칙과 소셜 기능은 제품 기능이 아니라 앱 로직이다.** channel
  handler와 spot으로 직접 작성한다. 배터리는 적지만, 로직의 소유권과 자유도가
  앱에 남는다.

그리고 이 전부가 쓰던 프레임워크 안이다 — 엔진의 섬과 정반대 방향이다.

```mermaid
%%{init: {'themeVariables': {'edgeLabelBackground':'transparent'}}}%%
flowchart LR
  subgraph engine["게임 서버 엔진 방식 — 자체 런타임의 섬"]
    direction TB
    E1["<b>엔진 전용 런타임</b>"]
    E2["엔진 방식으로 로직 작성<br/>자체 설정 · 배포 · 운영"]
    E3["별도 학습 · 별도 스택<br/>기존 백엔드·채용 풀과 분리"]
    E1 ~~~ E2 ~~~ E3
  end
  subgraph zl["ZLink 방식 — 쓰던 프레임워크 위의 계층"]
    direction TB
    Z1["<b>ASP.NET Core / Spring / NestJS</b><br/>DI · 설정 · 로깅 · 배포 그대로"]
    Z2["<b>ZLink Framework</b><br/>SPOT · actor · STREAM · drain"]
    Z1 ~~~ Z2
  end
  engine ~~~ zl
  classDef island fill:#fff3e0,stroke:#e65100,stroke-width:2px,color:#bf360c;
  classDef keep fill:#e8f5e9,stroke:#2e7d32,stroke-width:2px,color:#1b5e20;
  classDef layer fill:#e3f2fd,stroke:#1565c0,stroke-width:2px,color:#0d47a1;
  class E1,E2,E3 island;
  class Z1 keep;
  class Z2 layer;
  style engine stroke:#e65100,stroke-width:2px,stroke-dasharray:6 4
  style zl stroke:#1565c0,stroke-width:3px
```

**코드로 보면.** room 하나를 선언하고, 그 room의 진행 로직을 쓴다.

```csharp
// 등록 — room mesh 하나와 room 타입
var node = options.AddSpotMesh("game.room");
node.EnableRouter("tcp://0.0.0.0:9001");
node.AddSpotFactory<BingoRoomSpot>();
```

```csharp
// bingo room의 진행 코드 — 이 안에서 동시성은 존재하지 않는다.
public sealed class MarkNumberHandler
    : IZLinkSpotRequestHandler<BingoRoomSpot, MarkNumber, MarkResult>
{
    public ValueTask<MarkResult> HandleAsync(
        BingoRoomSpot room, MarkNumber request, CancellationToken ct)
    {
        room.Board.Mark(request.Number);        // lock 없음
        room.LastActivity = DateTimeOffset.UtcNow;
        return ValueTask.FromResult(new MarkResult(room.Board.HasBingo()));
    }
}
```

여러 플레이어가 동시에 요청을 보내고 timer가 도는 room인데 `lock`도,
`Interlocked`도, Redis 분산 락도 없다. framework가 한 room의 모든 메시지(요청,
구독 이벤트, timer tick, actor packet)를 **하나의 실행 줄에 세워 순서대로**
실행하기 때문이다. 여기서 직렬은 codec 직렬화가 아니라 **실행 순서의
직렬화**다([06 §3](06-spot.ko.md)).

실행되는 근거 샘플: [TicTacToe](samples/tictactoe-game-sample.ko.md) ·
[Bingo](samples/bingo-game-sample.ko.md) · [GameQuest](samples/gamequest-sample.ko.md)

### 기존 웹 서비스에 실시간 기능을 붙일 때

**왜 복잡도가 올라가는가.** 대규모 웹 서비스의 표준 구성 — Spring/`ASP.NET Core` +
Redis(캐시) + Kafka(이벤트) + LB/K8s — 은 **stateless 요청/응답**에 최적화되어
있다. 여기에 채팅·알림·주문 추적 같은 실시간 기능을 붙이는 순간 그 전제가 하나씩
무너진다.

- **연결이 상태가 된다.** HTTP 요청은 아무 인스턴스가 받아도 되지만, WebSocket
  연결은 특정 인스턴스에 설정되어 있다. 그래서 연결을 고정하는 sticky LB가 생기고,
  "이 사용자가 지금 어느 인스턴스에 연결돼 있지?"를 앱이 Redis로 관리하기 시작한다.
- **서버 사이 실시간 전달이 우회한다.** 연결이 인스턴스마다 흩어져 있으니 서버 간
  전달은 브로커(Redis pub/sub, 또는 replay가 필요 없는데도 Kafka)를 경유한다 —
  운영할 인프라가 또 하나 늘어난다.
- **순서가 중요한 단위가 생긴다.** 주문·대화는 이벤트 처리 순서가 곧 정합성이다.
  여러 인스턴스가 같은 주문을 동시에 만질 수 있으니 분산 락으로 직렬화한다.

기능 하나 붙였는데 WebSocket 서버, sticky LB, 브로커 경유, 분산 락 — 조립 세트
한 벌과 그 운영 부담이 늘어난다.

**ZLink가 제공하는 것.** 조립 세트의 조각마다 기능이 대응한다.

| 조립하던 것 | ZLink 기능 | 자세히 |
|-------------|------------|--------|
| WebSocket 서버 + sticky LB | **STREAM** — 앱 서버가 client 연결을 직접 받는다 | [09](09-stream.ko.md) |
| 분산 락으로 순서 보장 | **SPOT owner routing** — 같은 주문·대화는 항상 자기 Spot 한 곳에서 직렬 실행 | [06](06-spot.ko.md) |
| 브로커 경유 실시간 전달 | **channel·fanout** — 서버 간 전달과 fan-out을 transport가 직접 | [05](05-channel-messaging.ko.md) |
| "누가 어디 연결돼 있지" 관리 | **actor binding + location store** — 재접속 이전성과 위치 조회를 framework가 소유 | [08](08-actor-session.ko.md)·[10](10-location.ko.md) |

같은 시스템 — 웹 API + 실시간 기능(채팅·주문 추적) — 을 두 방식으로 그리면 차이가
그림에서 바로 보인다.

**기존 방식** — 실시간 기능을 위한 구성 요소(주황)가 본체만큼 추가된다.

```mermaid
%%{init: {'themeVariables': {'edgeLabelBackground':'transparent'}}}%%
flowchart LR
    Client["클라이언트 앱"]
    LB["L7 LB / gateway"]:::infra
    Api["API 서버들 ×N<br/>(ASP.NET Core, stateless)"]:::app
    Dom["도메인 서버들 ×N<br/>(gRPC server)"]:::app
    SD["service discovery<br/>(xDS / Consul)"]:::infra
    SLB["sticky LB"]:::extra
    WS["WebSocket 서버 ×N"]:::extra
    RP["Redis pub/sub<br/>(실시간 fan-out 경유)"]:::extra
    RL["Redis 분산 락<br/>(주문·대화 순서 보장)"]:::extra

    Client -- "HTTP" --> LB --> Api
    Api -- "gRPC + mesh sidecar" --> Dom
    Api -.->|"위치 조회"| SD
    Dom -.->|"등록"| SD
    Client -- "실시간 연결" --> SLB --> WS
    WS <--> RP
    RP <--> Api
    Api -.-> RL
    Dom -.-> RL

    classDef app fill:#e3f2fd,stroke:#1565c0,color:#000000
    classDef infra fill:#eceff1,stroke:#546e7a,color:#000000
    classDef extra fill:#fff3e0,stroke:#e65100,stroke-width:2px,color:#bf360c
```

**ZLink 방식** — 주황 조각이 전부 사라지고, 위치 교환용 location store 하나가 남는다.

```mermaid
%%{init: {'themeVariables': {'edgeLabelBackground':'transparent'}}}%%
flowchart LR
    Client2["클라이언트 앱"]
    LB2["L7 LB / gateway<br/>(HTTP는 그대로)"]:::infra
    Api2["API 서버들 ×N<br/>ASP.NET Core + ZLink<br/>channel client"]:::app
    Dom2["도메인 서버들 ×N<br/>ASP.NET Core + ZLink<br/>SPOT(주문·대화) · STREAM"]:::app
    Store["location store<br/>(peer rows)"]:::infra

    Client2 -- "HTTP" --> LB2 --> Api2
    Client2 -- "STREAM 직접 접속" --> Dom2
    Api2 -- "channel request/send (직접)" --> Dom2
    Api2 -.->|"주소 해석"| Store
    Dom2 -.->|"등록"| Store

    classDef app fill:#e3f2fd,stroke:#1565c0,color:#000000
    classDef infra fill:#eceff1,stroke:#546e7a,color:#000000
```

sticky LB, WebSocket 서버, pub/sub 경유, 분산 락, mesh/discovery — 다섯 조각이
**location store 하나**로 줄었다. 서버 간 호출과 실시간 전달은 전부 runtime끼리
직접 이어진다.

**기존 스택을 대체하는 것이 아니다.** Kafka는 내구성 있는 이벤트 스트림으로, Redis는
캐시/영속 보조로 양쪽 그림 모두에 그대로 남는다(그래서 그림에서 뺐다). ZLink가
줄이는 것은 그 사이에서 실시간 전달을 위해 직접 조립하던 **연결·라우팅·상태 관리의
복잡도**다.

**코드로 보면.** 분산 락과 sticky 라우팅이 있던 자리에 두 줄이 남는다.

```csharp
// HTTP handler 안 — 주문 이벤트를 그 주문의 workflow Spot으로.
// OrderId가 spotRid라서 같은 주문은 항상 한 곳에서 순서대로 처리된다(분산 락 없음).
await spots.GetOrCreateAsync<OrderWorkflowSpot>(
    RoutingId.From(request.OrderId), new OrderSpotCreate(request.OrderId), ct);

// actor handler 안 — 재접속해도 같은 actor로 이어진 client에 push(sticky LB 없음).
actor.Context.BoundSession.Send(new OrderStatusChanged(orderId, status)).Submit(ct);
```

실행되는 근거 샘플: [SupportChat](samples/supportchat-sample.ko.md) ·
[DeliveryDispatch](samples/deliverydispatch-sample.ko.md) ·
[ShoppingMall](samples/shoppingmall-sample.ko.md)

두 상황의 차이는 진입점일 뿐, 쓰는 표면은 같다. 기능 하나씩 제공하는 제품은
있어도 — RPC는 gRPC가, actor는 Orleans가, 연결은 게임 엔진이 — **메이저
프레임워크 통합 + 직렬 실행 상태 단위 + 자동 연결 토폴로지를 한 몸에 담은
조합**이 ZLink의 자리다.

## 3. 표면과 구조 — 조금 더 들여다보기

### 호출 단위는 channel 이름 하나

ZLink Framework는 위 문제들의 모든 호출 단위를 **논리 `channel name` 하나**로 좁힌다.
application에서는 "`order` channel로 요청을 보낸다"처럼 사용하면 된다. 그 channel이
어느 서버에서 몇 개 실행 중인지는 location store에 등록된 peer 정보를 framework가 읽어
숨긴다.

서버 하나를 만들 때 직접 작성해야 했던 것들을 framework가 처리한다.

| 직접 만들어야 했던 것 | framework가 처리하는 방식 |
|-----------------------|----------------------------|
| socket 생성·bind·connect 관리 | channel/stream 이름과 역할로 선언하면 hosted service가 연결 |
| 메시지 직렬화·역직렬화 | codec 등록과 handler 계약에 맞춰 DTO를 그대로 주고받음 |
| 요청 routing·dispatch | handler group 또는 typed handler 등록으로 메시지가 알맞은 handler에 도착 |
| 로깅·검증·권한 확인 같은 공통 처리 반복 | HTTP route는 middleware, ZLink handler는 `IZLinkHandlerFilter`로 분리 |
| 동시 요청의 상태 보호 | SPOT의 직렬 실행으로 lock 없이 상태 관리 |
| 서비스 생성·의존성 관리 | ASP.NET Core DI에서 handler, client, filter를 생성 |
| 서버 주소 관리·연결 결정 | location store를 통해 현재 활성 endpoint 추적 |
| 설정·로그·모니터링 | ASP.NET Core 설정·logging·hosted service와 통합 |

### 기존 방식 대비 (체감 난이도)

같은 "서버 간 요청/응답"을 붙이는 코드량 차이다.

**raw 바인딩으로 직접 (개념적):**

```csharp
// 위치 저장소 조회, dealer socket 생성, endpoint 연결, 재연결 관리,
// correlation id 매칭, 직렬화, 수신 루프 ... 수십 줄의 연결·설정 코드
```

**ZLink Framework:**

```csharp
// 서버: handler 하나
public sealed class GetPriceHandler
    : IZLinkRequestHandler<PriceRequest, PriceReply>
{
    public ValueTask<PriceReply> HandleAsync(
        PriceRequest request, ZLinkRequestContext context, CancellationToken ct)
        => ValueTask.FromResult(new PriceReply(request.Symbol, 187.42m));   // 187.42m은 데모용 고정값(실제론 조회 결과)
}

// 등록 — 채널 선언 → 서버 활성화 → handler 등록이 한 channel 빌더에 묶인다.
builder.Services.AddZLinkFramework(options =>
{
    var channel = options.AddClientServerChannel("price"); // channel 이름 선언
    channel.EnableServer("tcp://0.0.0.0:7301");            // 이 channel의 server 역할 활성화(수신 endpoint)
    channel.AddRequestHandler<GetPriceHandler>();          // 그 server가 부를 handler 등록
});

// 클라이언트: IZLinkChannelClient를 주입받아 호출한다. builder(RequestToChannel = 요청 구성) +
// 종결자(.Async<TReply> = 실제 송신하고 reply 도착까지 대기)의 2단계다.
var reply = await client
    .RequestToChannel("price", new PriceRequest("AAPL"))   // builder: 어느 channel에 무슨 요청을 보낼지 지정
    .Async<PriceReply>(ct);                                // 종결자: reply 타입 지정 + 송신·대기
```

연결·설정 코드가 사라지고 남는 것은 handler와 channel 등록 몇 줄뿐이다.

### 아키텍처 — 어디에 올라가고, 무엇을 선언하나

```
+-----------------------------------------------------------+
|  ASP.NET Core app (DI, hosted service, handler)           |
+-----------------------------------------------------------+
|  ZLink Framework for .NET (adapter surface)               |
|   - channel messaging  - SPOT/actor  - STREAM session     |
|   - location/monitoring integration                       |
+-----------------------------------------------------------+
|  zlink .NET binding (DealerSocket, RouterSocket, Spot,    |
|   SpotNode ...)                                           |
+-----------------------------------------------------------+
|  zlink core (C API) - transport, ZMP, I/O threads         |
+-----------------------------------------------------------+
```

Framework는 새 transport나 새 socket 의미를 만들지 않는다. 기존 바인딩
기능을 **DI · hosted service · handler · attribute** 모델로 감싸 application에서 쓰기 쉽게 노출한다.
backend 의존 기준은
[internals/backend-dependency-policy](../internals/backend-dependency-policy.ko.md)
가 소유한다.

application이 이 스택과 만나는 지점은 **등록 코드 한 곳**이다. 여기서 네트워크
토폴로지를 선언한다 — 한 줄이 역할 하나다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddLocationStore(new ZLinkRedisLocationStore(...));  // 위치 교환 — 이후 연결은 자동

    options.AddClientServerChannel("order").EnableServer("tcp://0.0.0.0:7301"); // 1:N 요청/응답
    options.AddFanoutChannel("events").EnablePublisher("tcp://0.0.0.0:7302");   // 이벤트 fan-out
    options.AddRouteMeshChannel("play.route")                                    // 노드 지목 라우팅 mesh
        .EnableServer("tcp://0.0.0.0:7303").SetRoutingId(RoutingId.From("play-a"));
    options.AddSpotMesh("game.room").EnableRouter("tcp://0.0.0.0:7304");         // room mesh
    options.AddStreamNode("gateway").Bind("tcp://0.0.0.0:7400");                 // 외부 client 수용
});
```

gRPC+LB, broker, WebSocket 서버로 각각 조립하던 토폴로지들이 **같은 선언 모델
하나**로 내려온다. location store를 등록했으므로 서버가 늘어나거나 줄어들 때
connection도 자동으로 새로 연결되거나 정리된다 — 설정 파일을 고치거나 LB를
재구성할 일이 없다.
([05](05-channel-messaging.ko.md)·[06](06-spot.ko.md)·[09](09-stream.ko.md)·[10](10-location.ko.md))

## 4. 개념 요약

이 framework의 기능 단위들이다. 각각 전용 장에서 상세히 다룬다.

### DI 컨테이너 — ASP.NET Core 서비스 의존성 사용

`AddZLinkFramework(...)` 안에서 등록한 handler, client, filter는 ASP.NET Core DI와
같은 컨테이너에서 만들어진다. application 코드는 handler 생성자에 필요한 서비스를 선언하고,
framework는 dispatch 시점에 필요한 객체를 DI 컨테이너에서 가져온다.

[3장 →](03-concepts.ko.md)

### Configuration — ASP.NET Core 설정과 함께 사용

Framework 설정은 `AddZLinkFramework(options => ...)`에서 channel, location store,
codec, SPOT, STREAM 역할을 선언하는 방식으로 묶는다. 주소나 환경별 값은 일반
ASP.NET Core configuration에서 읽어 options에 넘긴다.

[2장 →](02-getting-started.ko.md)

### ASP.NET Core HTTP 연동 — HTTP pipeline과 분리

외부 REST API는 ASP.NET Core endpoint와 middleware가 담당한다. HTTP middleware는
HTTP 요청에만 적용되므로, ZLink channel handler의 공통 처리는 `IZLinkHandlerFilter`로
분리한다.

[5장 →](05-channel-messaging.ko.md)

### 채널 (Channel) — 서버 간 메시징, 기본 빌딩 블록

채널은 서버 사이의 통신 경로에 이름을 붙인 것이다. 보내는 쪽이 이름으로 channel을
찾아 typed 요청을 보내고, 받는 쪽의 handler가 처리해 응답한다. 직렬화·연결·재시도는
runtime이 처리한다.

채널 handler 서버는 SPOT·actor 없이도 완전한 서비스다. 요청을 받고, DB나 외부 API를
호출하고, 응답하는 일반 마이크로서비스를 channel handler만으로 구현한다. SPOT·actor는
실시간 상태가 필요할 때 선택적으로 추가하는 기능이다.

채널 패턴은 다음과 같다.

- **client/server** — ROUTER 서버에 DEALER 클라이언트가 연결되는 request-reply 또는 단방향 send.
- **fanout (pub/sub)** — publisher가 보내면 여러 subscriber에게 전달.
- **route mesh** — ROUTER끼리 연결하고, routing id로 특정 서버나 상태 단위에 고정 라우팅한다.

[5장 →](05-channel-messaging.ko.md)

### zlink core와 기본 소켓 패턴

위 레이어 그림처럼 framework는 새 소켓 의미를 만들지 않는다. zlink core(C API)가 소켓
패턴을 제공하고, `.NET` 바인딩이 이를 typed 클래스로 노출하며, framework runtime이
channel·spot 선언에 맞춰 생성·bind·connect 한다. 그래서 가이드 곳곳에
`DEALER`·`ROUTER`·`PUB/SUB` 이름이 보인다. **어떤 소켓 위에서 도는지** 알면 channel 종류
선택이 쉬워진다.

| framework 구성 | 하부 소켓 | 쓰임 |
|----------------|-----------|------|
| client-server channel | `DEALER → ROUTER` | 1:1 request/response·단방향 send |
| fanout channel | `PUB → SUB` | 이벤트 fan-out (여러 구독자) |
| route mesh channel | `ROUTER ↔ ROUTER` | routing id 기반 엔티티 라우팅 |
| STREAM session | `STREAM` | 외부 client(raw TCP/WS) 연결 |

각 소켓의 메시징 패턴·라우팅 전략·호환성 매트릭스·코드 예제는 zlink core 가이드가
자세히 다룬다:
[소켓 패턴 개요](../../../../../core/doc/guide/03-0-socket-patterns.ko.md) ·
[DEALER](../../../../../core/doc/guide/03-3-dealer.ko.md) ·
[ROUTER](../../../../../core/doc/guide/03-4-router.ko.md) ·
[PUB/SUB](../../../../../core/doc/guide/03-2-pubsub.ko.md) ·
[STREAM](../../../../../core/doc/guide/03-5-stream.ko.md)

### SPOT — 상태 단위를 lock 없이 관리

SPOT은 game room, stage, zone, 주문 처리 단위처럼 하나의 상태 영역과 참여자를 묶는
실행 단위다. 같은 SPOT에 들어오는 packet, timer, actor callback은 하나의 실행 흐름에서
처리되므로 SPOT이 소유한 상태에 lock 없이 접근할 수 있다.

[6장 →](06-spot.ko.md)

### Actor · Session — 클라이언트 세션

Actor는 연결 하나 또는 사용자 하나를 대표하는 서버 쪽 객체다. STREAM session이 외부
client 연결을 받고, actor는 SPOT에 입장해 상태 처리에 참여한다. 서버 간 actor relay도
가능하므로 session 서버와 domain 서버를 나눌 수 있다. actor lifecycle·호스팅은 6장이,
session↔actor binding·dispatch·client push는 7장이 다룬다.

[7장 →](07-actor-spot.ko.md) · [8장 →](08-actor-session.ko.md)

### Stream — 클라이언트 실시간 연결

게임 클라이언트, 채팅 앱, 배송원 앱처럼 외부에서 접속하는 양방향 연결이다. stream node가
접속을 받고, 연결마다 session 인스턴스를 생성한다. client 측 접속은 별도 산출물인
Stream Connector가 담당한다.

[9장 →](09-stream.ko.md)

### Location store — 주소 자동 연결

서버가 여러 인스턴스로 확장될 때 어느 주소로 연결할지를 코드에 하드코딩하지 않는다.
location store가 등록된 서버 목록을 관리하고, client 역할의 서버가 store에서 현재
활성 서버의 위치 정보를 읽어 동적으로 연결한다.

[10장 →](10-location.ko.md)

### 통합 4축 한눈에

```mermaid
flowchart LR
  App[ASP.NET Core 앱] --> FW[ZLink Framework]
  FW --> CM[channel messaging<br/>request · send]
  FW --> PS[PUB / SUB<br/>event fan-out]
  FW --> SP[SPOT<br/>room·stage·zone·actor]
  FW --> ST[STREAM<br/>외부 client connector]
  CM & PS & SP & ST --> ZB[zlink .NET 바인딩]
```

| 축 | 사용자에게 보이는 것 | 가이드 챕터 |
|----|----------------------|-------------|
| channel messaging | `[ZLinkRequest]`/`[ZLinkSend]` handler, `IZLinkChannelClient`, `IZLinkHandlerFilter` | [05-channel-messaging](05-channel-messaging.ko.md) |
| PUB/SUB | `[ZLinkPublish]`, `EnableSubscriber()`, `IZLinkFanoutClient` | [05-channel-messaging](05-channel-messaging.ko.md) |
| SPOT | typed spot factory, Spot context outbound, timer | [06-spot](06-spot.ko.md) |
| actor / session | actor factory, Entry Spot, `IZLinkBoundSession`, session actor dispatch | [07-actor-spot](07-actor-spot.ko.md) · [08-actor-session](08-actor-session.ko.md) |
| STREAM | framework session packet, Stream Connector | [09-stream](09-stream.ko.md) |
| 인프라 | Location 기반 자동 연결·운영 조회, runtime monitoring | [10-location](10-location.ko.md), [11-monitoring](11-monitoring.ko.md) |
| 운영 | 런타임 메트릭(`AddMeter` 한 줄), graceful drain, readiness probe | [12-operations](12-operations.ko.md) |

## 5. 전체 topology

각 기능이 어떻게 맞물리는지 보여주는 예시다. 이 지도를 각 기능 장이 확대해 들어간다.

```mermaid
flowchart LR
    Client["클라이언트 앱"]
    subgraph Api["진입 서버 (예: Api)"]
        HTTP["ASP.NET Core HTTP<br/>POST /games"]:::infra
        ApiC["channel client"]:::channel
    end
    subgraph Core["도메인 서버 (예: Play)"]
        CoreS["channel server"]:::channel
        SpotN["SPOT node<br/>(entry + room spots)"]:::spot
        StreamN["stream node"]:::stream
        ActorG["session relay"]:::actor
    end
    Store["Location store<br/>(peer rows)"]:::infra

    Client -- "1 HTTP 요청" --> HTTP
    HTTP --> ApiC
    ApiC -- "2 channel request" --> CoreS
    CoreS --> SpotN
    Client -- "3 stream 실시간 접속" --> StreamN
    StreamN -- "relay" --> ActorG --> SpotN
    ApiC -.->|"주소 해석"| Store
    CoreS -.->|등록| Store

    classDef channel fill:#e3f2fd,stroke:#1565c0,color:#000000
    classDef spot fill:#e8f5e9,stroke:#2e7d32,color:#000000
    classDef actor fill:#fff8e1,stroke:#f9a825,color:#000000
    classDef stream fill:#f3e5f5,stroke:#6a1b9a,color:#000000
    classDef infra fill:#eceff1,stroke:#546e7a,color:#000000
```

- **진입 서버** - ASP.NET Core HTTP로 외부 요청을 받아 domain 서버에 위임한다.
- **도메인 서버** - channel server + SPOT(상태 단위) + session relay + stream node.
- **Location store** - 서버 주소 정보를 관리한다. 점선은 store 조회를 통해 endpoint를 찾는 연결이다.
- **클라이언트 앱** - HTTP로 요청을 보내고, stream으로 실시간 상태를 받는다.

## 6. 산출물

| 항목 | 값 |
|------|-----|
| assembly | `Zlink.Framework`(계약·runtime), `Zlink.Framework.AspNetCore`(DI·hosted service 통합) |
| namespace | `Zlink.Framework` / `Zlink.Framework.Contracts.*`, 등록 확장(`AddZLinkFramework`)은 `Zlink.Framework.AspNetCore` |
| public 계약 | `Zlink.Framework.Contracts.*`와 `spec/handler-interfaces.ko.md` |
| 등록 진입점 | `builder.Services.AddZLinkFramework(...)` |

client 측 Stream Connector는 별도 산출물 `Systems.Zlink.Stream.Connector`다. 서버
framework와 독립적으로 배포되며, client application에서 TCP/TLS/WS/WSS 접속과 packet codec을
담당한다.

## 7. 누구를 위한 가이드인가 — 그리고 다루지 않는 것

이 가이드는 ZLink의 내부 구현을 고치려는 사람보다, `ASP.NET Core` 서비스에 ZLink를
붙여 실제 시스템을 만들려는 application 개발자를 먼저 대상으로 한다. 하부 소켓을
직접 다루는 법보다 channel, handler, SPOT, STREAM, location store를 언제 골라 쓰는지에
초점을 둔다.

주요 독자는 다음과 같다.

- **백엔드 API 개발자**: HTTP endpoint 안에서 다른 내부 서비스로 요청을 보내거나,
  기존 gRPC 호출을 논리 `channel name` 기반 request / response로 바꾸려는 사람.
- **마이크로서비스 운영 개발자**: 서버 instance가 늘고 줄어도 주소를 코드에 박지 않고,
  location store가 관리하는 현재 서버 목록으로 자동 연결하려는 사람.
- **실시간 서비스 개발자**: game room, stage, zone, 주문 workflow처럼 상태를 가진
  단위를 SPOT으로 묶고, 같은 상태에 들어오는 packet을 한 실행 흐름에서 처리하려는 사람.
- **gateway / connector 개발자**: 외부 client는 TCP, TLS, WebSocket 같은 STREAM으로
  받고, 내부 처리는 channel이나 actor로 넘기려는 사람.
- **기술 리더와 리뷰어**: ZLink를 도입할 만한 문제인지 판단하고, 어떤 책임은 ZLink가
  맡고 어떤 책임은 DB, broker, domain service에 남겨야 하는지 확인하려는 사람.

`guide/samples/` 아래 문서는 ZLink의 용도를 가장 구체적으로 보여 준다. 먼저
[channel messaging 샘플](samples/channel-messaging-samples.ko.md)에서 서비스 간
request / response, HTTP handler 안에서의 outbound 호출, publish / subscribe 흐름을
본다. 상태 소유와 라우팅은 [SPOT 샘플](samples/spot-samples.ko.md), 외부 client 연결은
[STREAM 샘플](samples/stream-samples.ko.md)과 [Stream Connector](samples/streaming-client.ko.md),
실시간 game server 구조는 [TicTacToe](samples/tictactoe-game-sample.ko.md)와
[Bingo](samples/bingo-game-sample.ko.md)에서 확인한다.
[ShoppingMall](samples/shoppingmall-sample.ko.md),
[DeliveryDispatch](samples/deliverydispatch-sample.ko.md),
[GameQuest](samples/gamequest-sample.ko.md),
[SupportChat](samples/supportchat-sample.ko.md)은 주문 workflow, 배정·상태 추적,
게임 진행, 상담·채팅처럼 업무 도메인까지 붙인 end-to-end 샘플이다.

**이 계층이 하지 않는 것도 분명하다.** ZLink Framework는 새 transport나 새 socket 의미를 만드는 계층이
아니다. 기존 `.NET` 바인딩(`DealerSocket`, `SpotNode` 등)을 그대로 쓰되, application
개발자가 DI, hosted service, handler, location store 모델로 다룰 수 있게 감싼다. 정식
public API 계약을 검토하는 사람은 [spec/](../../common/spec/languages/dotnet/handler-interfaces.ko.md)을, runtime
내부 구조를 고치는 사람은 [internals/](../internals/backend-dependency-policy.ko.md)를
같이 봐야 한다.

## 8. 이름 표기 규칙 (혼동 주의)

가이드 전체에서 다음 표기를 일관되게 쓴다.

- **framework adapter가 노출하는 모든 public 타입**(interface, record, enum,
  attribute, exception, DI 확장 메서드)은 `ZLink` prefix(대문자 `L`)를 쓴다. 예:
  `IZLinkChannelClient`, `ZLinkRequestContext`, `[ZLinkRequest]`, `AddZLinkFramework`,
  `ZLinkFrameworkException`.
- **단, client 측 Stream Connector 패키지**(`Systems.Zlink.Stream.Connector`)의
  타입은 `Zlink` prefix(소문자 `l`)를 쓴다. 예: `IZlinkStreamConnector`,
  `ZlinkStreamConnectorOptions`, `ZlinkStreamMessage`. 이는 connector가 서버
  framework 패키지에 의존하지 않는 독립 client 라이브러리이기 때문이다.
- **하부 zlink core C API**는 `zlink_*` snake_case다.
- NuGet package id와 namespace 단어는 역순 도메인 규칙을 따라
  `Systems.Zlink.*`다(예: `Systems.Zlink.Framework`).

> 정리하면: **서버 framework = `ZLink`, client connector = `Zlink`.** 한 코드에
> 두 표기가 같이 보이면 오타가 아니라 위 규칙 때문이다.

## 9. 현재 상태

이 가이드가 설명하는 public API는 [spec/](../../common/spec/languages/dotnet/handler-interfaces.ko.md)의 계약
카탈로그를 따른다. 구현이 진행되는 동안에도 인터페이스의 모양과 동사(`Request`,
`Submit`, `Bind`, `AddHandlerGroup` 등)는 spec 문서를 기준으로 확인한다. 세부
필드까지 정확한 정식 정의가 필요하면 항상 spec 문서를 교차 참조한다.

## 10. 이 가이드 읽는 순서

1. [02-getting-started](02-getting-started.ko.md) — 패키지부터 첫 동작 확인까지
2. [03-concepts](03-concepts.ko.md) — 핵심 개념 (channel, 역할, DI)
3. [05-channel-messaging](05-channel-messaging.ko.md) — request/send/pub-sub 상세
4. [06-spot](06-spot.ko.md) — room/stage/zone, timer, routed Spot 호출
5. [07-actor-spot](07-actor-spot.ko.md) — actor lifecycle, Spot 호스팅·콜백
6. [08-actor-session](08-actor-session.ko.md) — session↔actor binding·dispatch, client push
7. [09-stream](09-stream.ko.md) — 외부 client(STREAM) 서버 + Stream Connector
8. [10-location](10-location.ko.md) — location store 기반 자동 연결과 운영 조회
9. [11-monitoring](11-monitoring.ko.md) — runtime 이벤트 관찰
10. [04-feature-map](04-feature-map.ko.md) — 무엇을·얼마나 쉽게·언제 쓰나
11. [13-interface-catalog](13-interface-catalog.ko.md) — 모든 계약 인터페이스를 코드로(ContractTests 검증)
12. [14-grpc-alternative](14-grpc-alternative.ko.md) — **ZLink를 어디에 쓰나**(사용처·문제 신호·기술 선택 경계)
13. [guide/samples](samples/channel-messaging-samples.ko.md) — 정본 업무 시나리오의 등록 코드와 실행 흐름
14. [spec/](../../common/spec/languages/dotnet/handler-interfaces.ko.md) — 정식 계약(인터페이스 카탈로그)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework for .NET](../README.ko.md) | [다음: Getting Started](02-getting-started.ko.md)
<!-- framework-adapter-nav:bottom:end -->
