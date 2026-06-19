<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Channel Messaging](04-channel-messaging.ko.md) | [다음: Actor/Session](06-actor-session.ko.md)
<!-- framework-adapter-nav:end -->

# Java Spot Guide

## 1. 언제 쓰나

Spot은 room, stage, zone처럼 동적으로 생기고 사라지는 논리 단위가 필요할 때 쓴다.
같은 Spot 안의 상태 변경과 handler 실행은 framework가 정한 실행 문맥에서 다룬다.

## 2. 등록

```java
ZLinkSpotNodeBuilder node = options.addSpotMesh("game.stage")
    .addNode("play");
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
`create(spotType, request)` 또는 `getOrCreate(spotType, spotRid, request)`로 `Message`를
함께 넘기고, Spot은 `onCreate(...)`에서 그 payload를 받는다.

Spot factory는 Spot type 기준으로 등록한다. 같은 Spot type 중복 등록은 startup
validation 오류다.

## 4. Timer

Spot timer는 일반 scheduler helper가 아니라 Spot lifecycle에 묶인다. timer handler의
exception은 그 Spot의 dispatch 실패(stage completion)로 전달된다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Channel Messaging](04-channel-messaging.ko.md) | [다음: Actor/Session](06-actor-session.ko.md)
<!-- framework-adapter-nav:bottom:end -->
