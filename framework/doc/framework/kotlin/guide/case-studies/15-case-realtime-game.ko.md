<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: 케이스 — 내부 마이크로서비스 mesh + 운영](14-case-microservice-mesh.ko.md) | [다음: 케이스 — 라이드헤일링 실시간 디스패치](16-case-ride-hailing.ko.md)
<!-- framework-adapter-nav:end -->

# 케이스 — 실시간 멀티플레이 게임

> [12-grpc-alternative](../12-grpc-alternative.ko.md)의 케이스 스터디 중 하나다.
> STREAM·SPOT·actor·session actor dispatch 가 한 도메인에 모두 맞는 사례. 등록
> 정식은 [05-spot](../05-spot.ko.md)·[06-actor-session](../06-actor-session.ko.md)·
> [07-stream](../07-stream.ko.md)이 다룬다. 실행 가능한 구현 학습은
> [TicTacToe 샘플](../samples/tictactoe-game-sample.ko.md)·
> [Bingo 샘플](../samples/bingo-game-sample.ko.md)·GameQuest 세 샘플이 각기 다른
> 각도로 맡고(§7), 이 문서는 게임 도메인에 ZLink 를 넣을지 판단하는 케이스 스터디다.

> **이 케이스에서 ZLink 이 좋은 지점**
> - STREAM 이 client 연결을 받고, session actor dispatch 가 재접속 이전성을 다룬다.
> - SPOT 이 room 상태를 단일 큐로 직렬 처리한다(lock 없음).
> - **그대로 남는 것**: DB 가 progression 저장을, ZLink 은 실시간 연결·room 실행 모델을 맡는다.

## 1. 도메인 — 실시간 게임 백엔드의 진짜 난제

- **권위적 방 상태의 직렬성.** 같은 room/match 안에서 두 입력이 동시에 board 를
  바꾸면 안 된다. 매칭 엔진처럼 **room 당 단일 실행 라인**이 필요하다 — 보통
  per-room lock 이나 actor mailbox(Akka/Orleans/커스텀)로 푼다. entity 는 메모리에
  깨워 권위적으로 다룬다.
- **stateful 연결 + sticky session.** 실시간 서버는 stateful 이라, LB 는 같은
  세션의 패킷을 **같은 노드**로 보내야 한다(sticky). gateway 는 stateless 로 두고
  연결만 받는다.
- **재접속 이전성.** 끊겼다 다시 붙으면(다른 gateway 일 수 있음) 진행을 잃지 않아야
  한다. 보통 **Redis/DynamoDB 세션 스토어**에 위치·상태를 두고 재접속 시 복원한다.
  ([Metaplay](https://docs.metaplay.io/game-server-programming/introduction-to-the-game-server-architecture.html),
  [AWS multiplayer hosting](https://aws.amazon.com/solutions/guidance/multiplayer-session-based-game-hosting-on-aws/))

남는 난제: **장기 progression 영속(DB), 권위적 검증/안티치트, 매칭 알고리즘** 은
도메인 문제로 그대로 남는다. ZLink 가 줄이는 건 연결 수용·재접속 라우팅·방 직렬화
배선이다.

## 2. 기존 스택 — WebSocket gateway + Redis 세션 + 게임 노드

### 2.1 컴포넌트와 그 이유

| 컴포넌트 | 왜 필요한가 |
|----------|-------------|
| WS gateway fleet | 게임 client 의 영속 연결 수용(stateless 노드 여러 대) |
| sticky-session L7 LB | 같은 세션 패킷을 **같은 게임 노드**로 고정 |
| Redis 세션 스토어 | 재접속 시 "이 player 가 어느 노드/방에 있었나" 복원 |
| matchmaker | 새 player 를 방·노드에 배정 |
| 게임 노드(actor runtime) | room 상태를 **직렬 처리**(per-room lock 또는 grain) |
| 노드↔gateway 라우팅 | 프레임을 올바른 게임 노드로 relay |

### 2.2 연결 수용 + 방 처리

```java
@GetMapping("/game")
public void accept(WebSocketSession ws) {
    String playerId = authenticate(ws);
    String node = redis.get("sess:" + playerId).orElseGet(() -> matchmaker.assignNode(playerId));
    redis.set("sess:" + playerId, node, Duration.ofMinutes(10));
    pumpFramesTo(ws, node);
}
```

```java
public final class MatchRoom {
    private final Object gate = new Object();
    public PlaceMarkResult placeMark(String playerId, int cell) {
        synchronized (gate) { return updateBoardAndJudge(playerId, cell); }
    }
}
```

서 있어야 하는 것: WebSocket gateway fleet, sticky-session L7 LB, Redis 세션 스토어,
matchmaker, 게임 노드(actor runtime), 그리고 노드↔gateway 라우팅.

## 3. ZLink 스택 — STREAM + SPOT + actor

연결은 STREAM session 이, room 직렬성은 SPOT 이, 재접속 이전성은 session actor
dispatch 가 가져간다. **per-room lock 도, Redis 세션 라우팅 캐시도 응용 코드에서
사라진다.**

```kotlin
val stream = options.addStreamNode("session")
stream.bind("tcp://0.0.0.0:9100")
stream.attachActorGateway("play")
stream.registerSession(GameSession::class.java)

options.addActorFactory("player", PlayerActorFactory::class.java)

val node = options.addSpotMesh("play")
node.enableRouter("tcp://0.0.0.0:9200")
node.addEntrySpot(PlayerEntrySpot::class.java)
node.addSpotFactory(MatchSpot::class.java)
```

```kotlin
@Component
class PlaceMarkHandler :
    ZLinkSuspendingSpotActorRequestHandler<MatchSpot, PlayerActor, PlaceMark, PlaceMarkResult> {
    override suspend fun handle(
        spot: MatchSpot,
        actor: PlayerActor,
        context: ZLinkSpotActorRequestContext,
        req: PlaceMark,
        cancellationToken: CancellationToken,
    ): PlaceMarkResult = spot.placeMark(actor.actorId(), req.cell)
}
```

```kotlin
@Component
class GameSession(
    private val context: ZLinkSessionContext,
    private val actors: ZLinkActorManager,
) : ZLinkSuspendingSession() {
    private var actor: ZLinkSessionActor? = null

    override fun context(): ZLinkSessionContext = context

    override suspend fun onDispatchSuspending(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage) {
        if (dispatch.packetName() == "auth") {
            val req = payload.decode(AuthReq::class.java)
            val player = actors.getOrCreate(req.playerId, "player").await()
            actor = context.actors().bind(player).await()
            context.client().reply(AuthOk()).submit().await()
            return
        }
        val bound = actor ?: throw IllegalStateException("actor is not bound")
        bound.relay(payload).await()
    }
}
```

> **재접속 이전성은 framework 기본기.** 다른 Session 서버로 다시 붙어도
> `bind(actor, ...)` 가 actor id 기준으로 멱등하게 이어지며,
> actor 인스턴스와 spot membership 은 유지된다([6](../06-actor-session.ko.md) §4).
> 즉 **Redis 세션 라우팅 캐시가 응용에서 빠진다.**

> **다국어 배치.** 위 코드는 Java/Kotlin binding 예시지만, ZLink 은 언어 중립 wire
> protocol 위 다국어 binding 이라 한 시스템을 여러 언어로 섞을 수 있다. 예컨대
> **room/match 로직은 C++**(고성능 tick), **API·매치메이킹은 Java/Kotlin 또는 다른 ZLink binding** 로
> 두고 **같은 channel/spot/packet 계약**으로 상호 호출한다. 계약은 packet 이름 +
> codec DTO(교차 언어는 protobuf 권장)다([12 §2.1](../12-grpc-alternative.ko.md);
> 현재 Java/Kotlin 예시).

## 4. 양쪽 코드 비교 — "한 수 두기" 경로

| 축 | 기존(WS + Redis + 게임노드) | ZLink |
|----|------------------------------|-------|
| 연결 수용 | WebSocket gateway 직접 구현 | STREAM `ZLinkSession`(framework 소유) |
| 재접속 라우팅 | Redis 세션 스토어 get/set | `bind`(actorId 멱등) |
| 방 직렬성 | per-room `lock`/actor runtime | SPOT 단일 실행 큐(lock 없음) |
| 입력 처리 | `room.PlaceMark(...)` (lock 안) | `ZLinkSuspendingSpotActorRequestHandler.handle` (suspend) |
| 연결/로직 분리 | gateway↔게임노드 라우팅 직접 | session actor dispatch |

## 5. 아키텍처 비교 — 컴포넌트와 메시지 흐름

```text
[classic]  WebSocket gateway + Redis session + game node

  +----------------------+
  | WS gateway fleet     |   accept conn (stateless)
  +----------+-----------+
       sticky L7 LB
  +----------v-----------+   +------------------+
  | Redis session store  |   | matchmaker       |
  | (location/reconnect) |   +------------------+
  +----------+-----------+
  +----------v-----------+
  | game node            |   room serial state (lock/grain)
  | (actor runtime)      |
  +----------------------+
  + progression DB
```

```text
[ZLink]  STREAM + SPOT + actor

  +----------------------+
  | Session server       |   accept conn + actor bind/relay
  | (STREAM)             |
  +----------+-----------+
       relay by actorId
  +----------v-----------+
  | Play server          |   room = SPOT (serial), player = actor
  | (SpotMesh)           |
  +----------+-----------+
        +-----v------+
        | Registry   |   location resolve
        +------------+
  + progression DB        (unchanged)
```

- **빠지는 박스:** WS gateway fleet, sticky-session LB 설정, Redis 세션 라우팅
  스토어, gateway↔노드 라우팅 계층.
- **그대로인 박스:** progression DB, 매칭 알고리즘, 권위적 검증/안티치트.

### 메시지 흐름 — 시퀀스 비교

접속·한 수 두기 흐름이다.

```mermaid
sequenceDiagram
  autonumber
  participant C as client
  participant GW as WS gateway
  participant R as Redis session
  participant N as game node
  C->>GW: WS connect + auth
  GW->>R: 세션/노드 위치 조회·복원
  R-->>GW: node id
  C->>GW: PlaceMark
  GW->>N: relay frame
  N->>N: per-room lock 후 board 갱신
  N-->>GW: board update
  GW-->>C: push
```

```mermaid
sequenceDiagram
  autonumber
  participant C as client
  participant S as Session 서버
  participant P as Play 서버
  C->>S: STREAM connect + auth
  S->>P: BindActorHandle by actorId
  C->>S: PlaceMark
  S->>P: RelayToActor
  P->>P: room SPOT 단일 큐 직렬 처리
  P-->>S: BoundSession push
  S-->>C: STREAM push
```

Redis 세션 조회 hop 과 per-room lock 이 빠진다. 재접속은 actorId 기준 멱등이라
binding 만 새 stream 으로 옮겨 붙는다.

## 6. 줄어드는 것 / 그대로 남는 것

- **줄어드는 것:** WS gateway fleet, sticky-session LB, 재접속용 연결 라우팅 캐시,
  per-room lock 배선, 매칭 라우팅 mesh.
- **그대로 남는 것:** 장기 progression **DB**, 매칭 알고리즘, 권위적 검증. SPOT/actor
  의 인메모리 상태는 그 lifetime 동안만 유지된다. 공통 경계는
  [12-grpc-alternative](../12-grpc-alternative.ko.md)의 §4 경계 절 참고.

## 7. 실행 가능한 샘플 — 게임 백엔드 3선

이 케이스의 흐름은 세 샘플이 서로 다른 각도로 실행해 보여 준다. 셋은 같은 기능을
반복하지 않고, 게임 백엔드의 다른 난제를 하나씩 맡는다.

| 샘플 | 맡는 난제 | 핵심 요소 | codec |
|------|-----------|-----------|:-----:|
| [TicTacToe](../samples/tictactoe-game-sample.ko.md) | 가장 작은 실시간 game 흐름 | 수동 endpoint, STREAM auth, room Spot | MessagePack |
| [Bingo](../samples/bingo-game-sample.ko.md) | matching room + 분리 gateway | Registry/Discovery, Entry Spot, timer, bound push | Protobuf |
| ShoppingMall 류 GameQuest | gameplay event 누적 → quest 진행 | **fanout 구독 + event sourcing + projection** | JSON |

TicTacToe·Bingo 는 §3 의 STREAM·SPOT·actor 를 실시간 방 루프로 보여 주는 deep-dive
문서를 따로 가진다. 여기서는 본문이 다루지 않은 세 번째 각도, **GameQuest** 를 짚는다.

### GameQuest — fanout + event sourcing 으로 quest 진행

room 루프가 아니라, 여러 gameplay 영역(combat·inventory·mission·feature·world)에서
생긴 event 를 누적해 quest 진행을 결정하는 샘플이다. realtime 방 상태와는 다른 게임
백엔드 난제 — **흩어진 gameplay fact 의 player 단위 집계와 replay** — 를 맡는다.

- 구현 학습(deep-dive): [GameQuest Sample 문서](../samples/gamequest-sample.ko.md)
- 실행 코드: [Kotlin GameQuest 샘플](../../../../../languages/java/samples/kotlin/GameQuest)
- 공통 시나리오(언어 중립): [spec/sample/event/gamequest](../../../common/sample/event/gamequest.ko.md)

| 서버 | instance | 책임 |
|------|:--------:|------|
| `GameApi` | 2 | stateless action API, WebSocket session, gameplay event **publish** |
| `QuestMission` | 2 | gameplay event **구독**, `PlayerId` owner 로 `PlayerQuestSpot` route |
| stores | 1 set | `QuestStore`(event stream + projection) + GameApi 측 gameplay/subscription store |

- **fanout 으로 결합 분리**: `QuestMission` 은 event publisher 의 물리 endpoint 를 모른 채
  여러 gameplay event type 을 구독한다.
- **player 단위 owner**: 같은 `PlayerId` 의 event 는 항상 같은 `PlayerQuestSpot` owner
  흐름에서 처리·append·projection update 된다.
- **event sourcing**: owner 흐름이 progress/completion/reward event 를 append 하고(reward 는
  `SourceEventId` 기준 idempotent), `(PlayerId, QuestId)` stream replay 로 projection 을 다시
  만드는 경로는 self-check 가 검증한다.
- **누락 보정**: fanout event 누락 가능성은 gameplay snapshot 재동기화로 보정하고, 그
  결과도 quest domain event 로 남긴다.

## 8. 더 보기

- 케이스 허브: [12-grpc-alternative](../12-grpc-alternative.ko.md)
- 사용법: [05-spot](../05-spot.ko.md), [06-actor-session](../06-actor-session.ko.md), [07-stream](../07-stream.ko.md)
- 실행 예제: [tictactoe 샘플](../samples/tictactoe-game-sample.ko.md), [bingo 샘플](../samples/bingo-game-sample.ko.md), [GameQuest 샘플](../../../../../languages/java/samples/kotlin/GameQuest)
- 다음 케이스: [16-case-ride-hailing](16-case-ride-hailing.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: 케이스 — 내부 마이크로서비스 mesh + 운영](14-case-microservice-mesh.ko.md) | [다음: 케이스 — 라이드헤일링 실시간 디스패치](16-case-ride-hailing.ko.md)
<!-- framework-adapter-nav:bottom:end -->
