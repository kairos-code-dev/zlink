<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Channel Messaging](04-channel-messaging.ko.md) | [다음: Actor/Session](06-actor-session.ko.md)
<!-- framework-adapter-nav:end -->

# Java Spot Guide

## 현재 구현 기준

외부 channel에서 특정 Spot으로 send/request를 보낼 때는 framework가 core
`SpotRouteBridge`를 내부에서 사용한다. 사용자는 SpotMesh와 RouteMesh channel만
등록하면 되고, raw `DEALER`, `ROUTER`, `PUB` socket을 `SpotNode`에 직접 attach하지
않는다. Spot에서 외부 pub/sub channel로 publish할 때는 일반 channel
publisher client를 주입해서 사용한다.

## 1. 언제 쓰나

Spot은 room, stage, zone처럼 동적으로 생기고 사라지는 논리 단위가 필요할 때 쓴다.
같은 Spot 안의 상태 변경과 handler 실행은 framework가 정한 실행 문맥에서 다룬다.

## 2. 등록

```java
ZLinkSpotMeshBuilder node = options.addSpotMesh("game.stage");
node.enableRouter("tcp://0.0.0.0:9001");
node.enablePubSub("tcp://0.0.0.0:9002");
node.addEntrySpot(GameEntrySpot.class);
node.addSpotFactory(GameRoomSpot.class);
```

> node builder는 entry/spot factory만 등록하고 **SPOT handler는 등록하지 않는다.**
> SPOT handler는 Spot/EntrySpot의 `configure()` context에서 등록한다 — annotation
> (`@ZLinkSpotActorRequest`·`@ZLinkSpotTimer` 등, handler 는 `ZLinkSpotActorRequestHandler`
> 등 interface 구현)을 단 handler를 `addHandlersFromPackageOf(...)`로
> **자동** 등록(기본)하거나, `configure()`에서 `context().handlers().addActorRequest(...)` /
> `addPacket(...)` / `addSubscribe(...)`와 `context().addTimer(...)`로 **수동** 등록한다.

## 3. 생성과 조회

```java
spotManager.getOrCreate(GameRoomSpot.class, roomRid);
```

`getOrCreate(spotType, spotRid)`는 같은 `spotRid`가 이미 있으면 그 Spot을
재사용하고, 없으면 새로 만든다. 새 Spot의 시작 payload가 필요하면
`create(spotType, request)` 또는 `getOrCreate(spotType, spotRid, request)`로 DTO나
`ZLinkMessage`를 함께 넘기고, Spot은 `onCreate(...)`에서 framework `ZLinkMessage`로
그 payload를 받는다.

Spot factory는 Spot type 기준으로 등록한다. 같은 Spot type 중복 등록은 startup
validation 오류다.

## 4. Timer

Spot timer는 일반 scheduler helper가 아니라 Spot lifecycle에 묶인다. timer handler의
exception은 그 Spot의 dispatch 실패(stage completion)로 전달된다.

## 5. yield dispatch

Spot/Entry Spot handler에서 기본 `submit(...)`/`await(...)` 경로를 쓰면 handler가
완료될 때까지 같은 Spot 실행 큐의 다음 작업은 시작되지 않는다. 공용 상태를 await 전후로
이어 쓰는 handler는 이 기본 동작을 사용한다.

player 한 명의 admission/preflight처럼 await 전후에 actor-local 값과 reply 값만 쓰는
흐름에서는 `yield(...)`를 사용한다. `yield(...)`는 CompletionStage 체인 스타일로
handler를 바꾸라는 뜻이 아니다. 기존 동기식 handler 코드 모양을 유지하되, 현재 mailbox
turn을 반납하고 completion 뒤 같은 mailbox continuation으로 돌아오게 한다.

Bingo sample의 `MatchBingoActorHandler`는 API channel request와 room `joinSpot(...)` 대기에
`yield(...)`를 사용한다. room list, match queue, lobby state 같은 공용 mutable state를
await 전후로 이어서 판단하는 handler에는 `yield(...)`를 쓰지 않는다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Channel Messaging](04-channel-messaging.ko.md) | [다음: Actor/Session](06-actor-session.ko.md)
<!-- framework-adapter-nav:bottom:end -->
