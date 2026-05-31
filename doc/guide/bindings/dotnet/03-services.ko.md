[English](./03-services.md) | [한국어](./03-services.ko.md)

[← 메시징](./02-messaging.ko.md) · [.NET 가이드](./index.ko.md) · [다음: 운영 →](./04-operations.ko.md)

# 서비스 레이어

`02 메시징`의 raw 소켓은 "주소를 알고 있는 두 지점"을 잇습니다. 서비스 레이어는 그
위에 **동적 토폴로지**를 얹습니다 — 노드가 들어오고 나가고, 이름으로 서로를 찾고,
방·세션·플레이어 같은 단위가 런타임에 생겼다 사라지는 환경입니다. 개념의 정식
정의와 층별 멘탈 모델(raw 소켓 / Discovery / SPOT / Actor)은
[서비스 개요](../../07-0-services.ko.md#12-멘탈-모델--어느-층을-언제-쓰나)가
소유하며, 이 문서는 **각 기능이 무슨 역할이고 언제 쓰는지** + **.NET 사용 형태**를
다룹니다.

> 핵심: 서비스 레이어는 **상태 저장소가 아닙니다.** 룸·세션 데이터는 여전히
> 응용이 소유합니다. SPOT이 주는 것은 그 상태에 닿는 메시지를 **한 줄로 직렬
> 처리**하는 실행 모델(lock 불필요)이고, Actor가 주는 것은 세션이 어느 서버에
> 붙어 있든 **같은 엔티티로 이어 주는 binding**입니다.

## 언제 서비스 레이어가 필요한가

| 상황 | 권장 |
|------|------|
| 주소가 고정된 소수 노드 (예: 클라이언트↔단일 서버) | raw 소켓([02 메시징](./02-messaging.ko.md))으로 충분. 서비스 레이어 불필요 |
| 노드가 동적으로 늘고 줄어 이름으로 찾아야 함 | **Registry + Discovery** |
| 방·스테이지·존처럼 런타임에 생기는 라우팅 단위 | **SpotNode / Spot** |
| 세션/플레이어처럼 상태를 갖고 메시지를 받는 엔티티 | **Actor** |

모든 서비스 객체는 컨텍스트에서 만들고 `IDisposable`입니다. 만든 역순으로
dispose 하세요(소켓·spot·actor → node → discovery/registry → context).

---

## Registry (레지스트리)

**역할** — 클러스터의 중앙 서비스 카탈로그. "어떤 채널이 어디에 떠 있는가"를 모아
두고, 노드들이 여기에 등록·조회합니다. Discovery가 자동 연결을 할 수 있는 것은
Registry가 토폴로지를 들고 있기 때문입니다.

**언제** — 노드 주소가 배포마다 바뀌거나 수가 변동할 때. 주소가 고정된 소규모
토폴로지라면 Registry 없이 `ConnectPeer(...)` 수동 연결이 더 단순합니다. 개념·배포
패턴(임베디드 vs 독립)은 [Registry](../../07-4-registry.ko.md).

```csharp
using var registry = ctx.CreateRegistry();
registry.Bind("tcp://127.0.0.1:6000",   // pub 엔드포인트 — 토폴로지 브로드캐스트
              "tcp://127.0.0.1:6001");   // router 엔드포인트 — 등록/질의 수신

RegistryTopologyEntry[] topology = registry.Topology();   // 등록된 서비스 스냅샷
```

등록 없이 **조회만** 하려면 가벼운 `IRegistryQueryClient`를 씁니다(운영 대시보드,
헬스 체크 등):

```csharp
using var query = ctx.CreateRegistryQueryClient();
query.Connect("tcp://127.0.0.1:6001");
RegistryTopologyEntry[] snapshot = query.Topology();
```

> 보통 클러스터당 Registry 하나(또는 HA 클러스터)를 운영합니다. Registry를 먼저
> 띄운 뒤 노드들이 Discovery로 붙는 순서입니다.

---

## Discovery (디스커버리)

**역할** — Registry에 붙어 **채널 이름으로 피어를 찾고 자동 연결**합니다. 소켓이나
노드에 `AttachDiscovery(...)` 하면, 같은 채널의 피어가 등록/이탈할 때 연결이
자동으로 맺어지고 끊깁니다. 라우트 해석(`ResolveSpot`/`ResolveActor`)도 제공합니다.

**언제** — 수동 `Connect(...)` 대신 "이 채널에 속한 모두와 연결" 의미가 필요할 때.

**`AutoConnectType` 고르기** — 채널이 맺을 연결 모양을 정합니다(개념:
[Discovery](../../07-1-discovery.ko.md)).

| 값 | 의미 | 전형적 용도 |
|----|------|------------|
| `Fanout` | 발견된 모두에게 fan-out | pub/sub 공급자 |
| `ClientServer` | 클라이언트↔서버(DEALER↔ROUTER) | 요청/응답 서비스 |
| `RouteMesh` / `DealerMesh` / `SpotMesh` | 노드 간 메시 | SpotNode 메시, 노드 간 라우팅 |

```csharp
using var discovery = ctx.CreateDiscovery(AutoConnectType.Fanout, "sample");
discovery.ConnectRegistry("tcp://127.0.0.1:6001");

using var provider = ctx.CreatePubSocket();
provider.AttachDiscovery(discovery);             // 이 서비스를 디스커버리에 등록
provider.Bind("tcp://127.0.0.1:6002");

SpotRoute  route = discovery.ResolveSpot(spotRid);    // 라우트 해석
ActorRoute actor = discovery.ResolveActor("player-1");
```

---

## SpotNode / Spot

**역할** — `SpotNode`는 클러스터의 **메시 노드**(피어 연결·디스패치를 소유), `Spot`은
그 위의 **메시징 엔드포인트**입니다. Spot 하나가 토픽 발행/구독과 라우팅된
송신/요청을 동시에 다룹니다. "방(room)·스테이지·존" 같은 동적 단위가 전형적인
Spot입니다. 개념: [SPOT](../../07-3-spot.ko.md).

**왜 Spot인가 — 실행 직렬성.** Spot의 핵심 가치는 라우팅만이 아닙니다. 한 Spot으로
들어온 메시지는 **단일 실행 큐로 한 줄씩 직렬 처리**됩니다. 그래서 룸 상태(점수,
참가자 목록 등)를 lock으로 보호할 필요 없이 동시성 문제 자체가 사라집니다. 게임
룸, 심볼 오더북, 채팅방처럼 "한 단위의 상태를 안전하게 갱신"해야 할 때 raw PUB/SUB
대신 Spot을 쓰는 이유입니다. 상태 데이터 자체는 여전히 응용이 들고 있습니다.

**SpotNode 모드 고르기** — 노드가 켤 기능을 정합니다. 꺼진 기능은 내부 소켓을 아예
만들지 않으므로 숨은 비용이 없습니다(상세: [07-3 §SpotNode 모드](../../07-3-spot.ko.md)).

| `SpotNodeMode` | 켜지는 것 | 언제 |
|----------------|-----------|------|
| `PubSub` | 토픽 발행/구독만 | 이벤트 fan-out만 필요 |
| `Routed` | 스팟 간/채널 라우팅만 | 직접 송신·요청만 필요 |
| `All` (기본) | 둘 다 | 혼합 워크로드 |

**Spot의 세 가지 outbound 표면** — 무엇을 호출할지는 통신 상대로 정합니다.

| 하려는 것 | 메서드 | 언제 |
|-----------|--------|------|
| 토픽 구독자에게 이벤트 | `Publish(topic)` / `SetSubscription` + `Subscribe` | 1:N 이벤트 fan-out |
| 다른 채널(서비스)로 송신/요청 | `SendToChannel` / `RequestToChannel` | 다른 서비스 호출 |
| 특정 노드의 특정 Spot으로 | `SendToSpot` / `RequestToSpot` (+ `ReplyToSpot`) | 라우트가 해석된 스팟 직통 |

### 토픽 발행/구독

```csharp
// 두 노드를 서로 연결 (수동 토폴로지; 운영에선 AttachDiscovery로 대체)
using var pubNode = ctx.CreateSpotNode();
using var subNode = ctx.CreateSpotNode();
pubNode.SetRoutingId(RoutingId.From("node-pub"));
subNode.SetRoutingId(RoutingId.From("node-sub"));
pubNode.SetPubBind("tcp://127.0.0.1:6100");
subNode.SetPubBind("tcp://127.0.0.1:6101");
pubNode.ConnectPeer("tcp://127.0.0.1:6101");
subNode.ConnectPeer("tcp://127.0.0.1:6100");

using var publisher  = pubNode.CreateSpot();
using var subscriber = subNode.CreateSpot();

subscriber.SetSubscription("room:lobby");
using (var msg = Message.From("hello"))
    publisher.Publish("room:lobby").Message(msg).Submit();

using var topicMsg = new TopicMessage();             // 재사용 가능한 수신 봉투
if (subscriber.Subscribe(topicMsg, RecvFlags.DontWait))
    Console.WriteLine($"{topicMsg.Topic}: {topicMsg.SinglePartOrThrow().GetString()}");
```

> 토픽 구독은 발행자에 전파되기 전 메시지를 놓치는 slow-joiner 특성이 있습니다
> ([PUB/SUB](../../03-2-pubsub.ko.md) 참고). `TopicMessage`는 `Received`처럼
> 호출자 소유 봉투이므로 루프에서 재사용하세요.

### 채널 기반 요청/응답

요청 전에 노드에 채널용 dealer를 연결해 둡니다(또는 `AttachDiscovery`로 자동 발견).

```csharp
using var requesterNode   = ctx.CreateSpotNode();
using var requesterDealer = ctx.CreateDealerSocket();
using var requester       = requesterNode.CreateSpot();
requesterDealer.Connect("tcp://127.0.0.1:6001");          // 응답자 ROUTER 엔드포인트
requesterNode.AttachChannelDealerManual("orders", requesterDealer);

using var request = Message.From("spot-ping");
IReadOnlyList<Message> reply = await requester.RequestToChannel("orders")
    .Message(request)
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync();
using Message part = reply[0];                            // 응답 파트는 호출자 소유
Console.WriteLine(part.GetString());                     // spot-pong
```

### Entry Spot vs 일반 Spot

`node.CreateSpot()`은 일반 Spot을, `node.EntrySpot()`은 노드당 하나뿐인 **진입
Spot**을 줍니다. Entry Spot은 액터가 처음 합류하는 잘 알려진 지점으로 쓰입니다(아래
Actor 절, 개념: [07-3](../../07-3-spot.ko.md)). 기존 Spot을 routing id로 다시 얻으려면
`GetOrCreateSpot(spotRid, out created)` / `SpotLookup(spotRid)`를 씁니다.

---

## Actor (액터)

**역할** — Spot에 **합류(join)** 해서 그 Spot으로 들어온 메시지를 받는 **상태 보유
엔티티**입니다. 세션·게임 플레이어·작업 큐처럼 "고유 정체성을 갖고, 자신에게 향한
메시지를 순서대로 처리하는 단위"가 Actor입니다. 핵심은 **세션 위치와 처리 단위의
분리** — 클라이언트가 어느 연결 서버에 붙어 있든, 메시지는 그와 묶인 Actor로
전달됩니다. 개념·상태 전이: [Actor](../../07-4-actor.ko.md).

**왜 Actor인가 — 재접속 이전성.** Actor는 actor id로 식별되며 세션 연결과 별개로
존재합니다. 그래서 클라이언트가 끊겼다 다른 연결 서버로 재접속해도 **같은 Actor로
다시 묶입니다** — "이 클라이언트가 어느 서버에 붙어 있었는지"를 외부 저장소(예:
Redis)로 따로 관리하던 일을 framework가 가져갑니다. 연결 서버(STREAM 게이트웨이)와
로직 서버를 분리하는 구성의 토대입니다.

Actor는 raw 소켓의 대안이 아니라 **Spot 위에 얹는 한 단계 더 높은 모델**입니다.
Actor 메시지도 결국 Spot routed 평면 위로 흐르며(core는 별도의 router→actor
직통 API를 두지 않습니다 — [07-4](../../07-4-actor.ko.md)), Actor는 "그 Spot에
도착한 메시지를 어느 세션/엔티티에게 줄지" 구분하는 장치입니다.

**Actor를 쓸 때 vs plain Spot으로 충분할 때** (둘 다 SPOT 위의 선택)

| Spot에 들어온 메시지를… | 방법 |
|------|------|
| 디스패치 핸들러에서 그대로 처리 (세션별 정체성 불필요) | plain Spot — `RecvRouted` / `SetDispatchHandler` |
| 세션/엔티티 단위로 모아, 위치와 무관하게 묶어서 처리 | **Actor** (Spot에 join) |
| 연결 서버와 로직 서버를 분리(외부 세션을 엔티티에 묶음) | **Actor** + STREAM 게이트웨이 |

> raw 소켓을 쓸지 SPOT 서비스 층을 쓸지는 이 문서 맨 위
> [언제 서비스 레이어가 필요한가](#언제-서비스-레이어가-필요한가)가 다루는 별개
> 결정입니다. 여기서는 이미 SPOT을 쓰기로 한 뒤의 선택만 다룹니다.

**lifecycle** — `CreateActor` → `Join`(Spot에 합류) → 메시지 수신 → `Leave` →
`Close`. 합류 대기 중에는 새 합류/탈퇴/소멸이 busy 오류로 실패합니다(상태 전이 충돌).
활성 경로는 **join 성공 시점**에 게시됩니다.

```csharp
// node/spot은 위 'SpotNode / Spot' 절에서 만든 것입니다.
// 액터 측: 스팟에 합류
using var actor = node.CreateActor("room-player-1");
using var joinMsg = Message.From("join:lobby");
var joinTask = actor.Join(spot)
    .Message(joinMsg)
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync();

// 스팟 측: 합류 요청 수신·수락
ActorJoinRequest? req = spot.RecvActorJoin(RecvFlags.DontWait);
if (req != null)
{
    using var ok = Message.From("accepted:lobby");
    spot.ReplyActorJoin(req, joinResultCode: 0).Message(ok).Submit();
}

var (result, parts) = await joinTask;            // ActorJoinResult + 응답 파트
using (parts[0]) Console.WriteLine(parts[0].GetString());   // accepted:lobby

// 떠나기 (반환된 응답 파트는 정리)
Zlink.MultipartClose(
    await actor.Leave(spot).Timeout(TimeSpan.FromSeconds(2)).SubmitAsync());
```

**수신 방식 두 가지** — 폴링(`actor.Recv(...)`)이나 디스패치 콜백
(`spot.SetDispatchHandler(info => { ... })`)으로 받습니다. 콜백은 여러 이벤트
종류(구독/라우트/타이머/액터/합류/lifecycle)를 한 핸들러로 모아 처리할 때
유용합니다(콜백 규칙은 [운영 — 폴러/타이머](./04-operations.ko.md#폴러--타이머)와
[STREAM 콜백](./02-messaging.ko.md#stream) 참고).

**세 가지 전형 패턴** — 샘플로 바로 확인할 수 있습니다.

| 패턴 | 무엇 | 샘플 |
|------|------|------|
| 방 디스패치(broadcast) | 한 방의 여러 참가자에게 분배 | `samples/ActorRoomServer` |
| 직렬 큐(serialize) | 한 엔티티의 메시지를 순서대로 처리 | `samples/ActorSinglePlayerQueue` |
| 게이트웨이 릴레이 | 외부 TCP 클라이언트를 Actor로 중계 | `samples/ActorGatewayRelay` |

> STREAM 게이트웨이와 연동하려면 `stream.AttachActorGateway(node)` +
> `stream.BindActor(...)`를 씁니다. 외부 클라이언트의 세션을 Actor에 묶어, 연결
> 서버와 로직 서버를 분리하는 구성입니다. 상세 흐름은 코어
> [Actor](../../07-4-actor.ko.md) 챕터를 참고하세요.

---

다음: [운영 — 옵션 · TLS · 모니터링 · 폴러/타이머 · 스레딩 →](./04-operations.ko.md)
