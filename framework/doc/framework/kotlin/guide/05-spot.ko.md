<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Channel Messaging](04-channel-messaging.ko.md) | [다음: Actor/Session](06-actor-session.ko.md)
<!-- framework-adapter-nav:end -->

# Kotlin Spot Guide

## 현재 구현 기준

외부 channel에서 특정 Spot으로 send/request를 보낼 때는 framework가 Java binding의
public `SpotRouteBridge`를 내부에서 사용한다. 사용자는
`acceptSpotRoutesFromChannel(...)`과 egress 설정 이름을 맞추면 되고, raw
`DEALER`, `ROUTER`, `PUB` socket을 `SpotNode`에 attach하지 않는다. Spot에서 외부
pub/sub channel로 publish할 때는 일반 channel publisher client를 주입해서 사용한다.

## 1. 언제 쓰나

Spot은 room, stage, zone처럼 동적으로 생기고 사라지는 논리 단위가 필요할 때 쓴다.
같은 Spot 안의 상태 변경과 handler 실행은 framework가 정한 단일 실행 큐(직렬)에서
다뤄지므로, Spot 상태는 lock 없이 만질 수 있다.

## 2. 등록

node builder는 entry/spot factory만 등록한다.

```kotlin
val node = options.addSpotMesh("game.stage").addNode("play")
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

Spot 베이스 클래스는 `ZLinkSuspendingSpot<TActor>`다. lifecycle 콜백을 `suspend`로
override한다(`onCreateSuspending`, `onInitializeSuspending`, `onClosingSuspending`,
`onActorJoinSuspending`). 기본 구현을 그대로 두면 Java 베이스와 같은 의미다.

```kotlin
import systems.zlink.framework.kotlin.ZLinkSuspendingSpot

class GameRoomSpot(
    private val context: ZLinkSpotContext,
) : ZLinkSuspendingSpot<PlayerActor>() {
    private var round = 0

    override fun context(): ZLinkSpotContext = context

    override suspend fun onCreateSuspending(request: Message): ZLinkSpotCreateResponse {
        // 시작 payload를 디코드해 초기 상태 구성(suspend 작업 가능)
        return ZLinkSpotCreateResponse.accept()
    }

    override suspend fun onInitializeSuspending() {
        round = 1
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

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Channel Messaging](04-channel-messaging.ko.md) | [다음: Actor/Session](06-actor-session.ko.md)
<!-- framework-adapter-nav:bottom:end -->
