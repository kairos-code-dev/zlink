<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Channel Messaging](04-channel-messaging.ko.md) | [다음: Actor/Session](06-actor-session.ko.md)
<!-- framework-adapter-nav:end -->

# Kotlin Spot Guide

## 현재 구현 기준

외부 channel에서 특정 Spot으로 send/request를 보낼 때는 framework가 Java binding의
public `SpotRouteBridge`를 내부에서 사용한다. 사용자는 SpotMesh와 RouteMesh channel만
등록하면 되고, raw `DEALER`, `ROUTER`, `PUB` socket을 `SpotNode`에 직접 attach하지 않는다. Spot에서 외부
pub/sub channel로 publish할 때는 일반 channel publisher client를 주입해서 사용한다.

## 1. 언제 쓰나

Spot은 room, stage, zone처럼 동적으로 생기고 사라지는 논리 단위가 필요할 때 쓴다.
같은 Spot 안의 상태 변경과 handler 실행은 framework가 정한 단일 실행 큐(직렬)에서
다뤄지므로, Spot 상태는 lock 없이 만질 수 있다.

## 2. 등록

node builder는 entry/spot factory만 등록한다.

```kotlin
val node = options.addSpotMesh("game.stage")
node.enableRouter("tcp://0.0.0.0:9001")
node.enablePubSub("tcp://0.0.0.0:9002")
node.addEntrySpot(GameEntrySpot::class.java)
node.addSpotFactory(GameRoomSpot::class.java)
```

> node builder는 entry/spot factory만 등록하고 **SPOT handler는 등록하지 않는다.**
> SPOT handler는 Spot/EntrySpot의 `configure()` context에서 등록한다 — `@ZLinkSpotActorRequest`·
> `@ZLinkSpotTimer` 등 annotation을 단 handler(또는 `ZLinkSuspendingSpotActorRequestHandler`
> 등 interface 구현)를 `addHandlersFromPackageOf(...)`로 **자동** 등록(기본)하거나,
> `configure()`에서 `context().handlers().addActorRequest(...)` / `addPacket(...)` /
> `addSubscribe(...)`와 `context().addTimer(...)`로 **수동** 등록한다.

## 3. Spot 작성 — `ZLinkSuspendingSpot`

user Spot의 베이스 클래스는 `ZLinkSuspendingSpot<TActor>`다. Entry Spot에서 같은 coroutine
표면이 필요하면 `ZLinkSuspendingEntrySpot<TActor>`를 사용한다. 생성과 초기화 콜백 외에도 actor
admission, joined, leave 콜백을 `suspend`로 override한다. admission은 actor instance가 아니라 actor id와
request만 받는다. accepted join의 membership 처리는 joined 콜백에서 하고, leave 콜백과 함께 두 콜백은
반드시 구현해야 한다.

```kotlin
import systems.zlink.framework.kotlin.ZLinkSuspendingSpot
import systems.zlink.framework.messaging.ZLinkMessage

class GameRoomSpot(
    private val context: ZLinkSpotContext,
) : ZLinkSuspendingSpot<PlayerActor>() {
    private var round = 0

    override fun context(): ZLinkSpotContext = context

    override suspend fun onCreateSuspending(request: ZLinkMessage): ZLinkSpotCreateResponse {
        // 시작 payload를 디코드해 초기 상태 구성(suspend 작업 가능)
        return ZLinkSpotCreateResponse.accept()
    }

    override suspend fun onInitializeSuspending() {
        round = 1
    }

    override suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse {
        // Admission에서는 actor id와 request만 검증하고 membership은 아직 바꾸지 않는다.
        return ZLinkSpotActorJoinResponse.accept()
    }

    override suspend fun onJoinedActorSuspending(
        actor: PlayerActor,
        cancellationToken: CancellationToken,
    ) {
        // 이동이 commit된 뒤에만 이 Spot의 membership을 확정한다.
        players.add(actor.actorId())
    }

    override suspend fun onLeaveActorSuspending(
        actor: PlayerActor,
        cancellationToken: CancellationToken,
    ) {
        // 다음 Spot의 joined 콜백보다 먼저 기존 membership을 제거한다.
        players.remove(actor.actorId())
    }
}
```

## 4. 생성과 조회

```kotlin
import kotlinx.coroutines.future.await

spotManager.getOrCreate(GameRoomSpot::class.java, roomRid).await()
```

`getOrCreate(spotType, spotRid)`는 같은 `spotRid`가 이미 있으면 그 Spot을
재사용하고, 없으면 새로 만든다. 새 Spot의 시작 payload가 필요하면 `create(spotType, request)`
(자동 `spotRid`) 또는 `getOrCreate(spotType, spotRid, request)`(고정 `spotRid`)로 만들고
lifecycle callback(`onCreateSuspending`)에서 받는다. `create(spotType, spotRid)`는 payload
없이 고정 rid로만 만든다. 이 호출들은 `CompletionStage`를 반환하므로 `suspend` 문맥에서 `.await()`로 기다린다.

Spot factory는 Spot type 기준으로 등록한다. 같은 Spot type 중복 등록은 startup
validation 오류다.

## 5. Timer

Spot timer는 일반 scheduler helper가 아니라 Spot lifecycle에 묶인다. timer handler는
`ZLinkSuspendingSpotTimerHandler<TSpot>`로 작성하며, `suspend fun handle(spot, tick)`
안에서 Spot 상태를 직접 만질 수 있다(같은 실행 큐). timer handler exception은
monitoring event로 관찰된다([09-monitoring](09-monitoring.ko.md)).

## 6. yield dispatch

기본 `submit(...).await()`는 Java core의 기본 serial 의미를 따른다. user Spot handler가
기다리는 동안 같은 Spot 실행 큐의 다음 작업은 시작되지 않는다. 공용 상태를 await 전후로
이어 쓰는 handler는 이 기본 동작을 사용한다.

user Spot handler처럼 반납할 Spot turn이 있는 흐름에서는 await 전후에 actor-local 값과
reply 값만 쓰는 경우 `yield(call, ReplyType::class.java)` helper를 사용할 수 있다. 이 helper는
Java call object의 yield terminator를 호출한다. coroutine context를 turn, mailbox,
actor 상태의 소유권 저장소로 쓰지 않는다.

Entry Spot actor handler에는 반납할 Entry Spot 전체 실행 turn이 없으므로 `yield(...)`를
사용하지 않는다. API channel request와 room `joinSpot(...)` 대기는 `await(...)`로 표현한다.
room list, match queue, lobby state 같은 공용 가변 상태를 await 전후로 이어서 판단하는 handler에도
`yield(...)`를 쓰지 않는다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Channel Messaging](04-channel-messaging.ko.md) | [다음: Actor/Session](06-actor-session.ko.md)
<!-- framework-adapter-nav:bottom:end -->
