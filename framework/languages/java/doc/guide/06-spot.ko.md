<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Channel Messaging](./05-channel-messaging.ko.md) | [다음: Actor/Session](./07-actor-session.ko.md)
<!-- framework-adapter-nav:end -->

# Java Spot Guide

## 1. 언제 쓰나

Spot은 room, stage, zone처럼 동적으로 생기고 사라지는 논리 단위가 필요할 때 쓴다.
같은 Spot 안의 상태 변경과 handler 실행은 framework가 정한 실행 문맥에서 다룬다.

## 2. 등록

```java
options.addSpotMesh("game.stage", mesh -> {
    mesh.addNode("play", node -> {
        node.enableRouter();
        node.enablePubSub();
        node.addEntrySpot(GameEntrySpot.class);
        node.addSpotFactory(GameRoomSpot.class);
    });
});
```

## 3. 생성과 조회

```java
spotManager.getOrCreate(GameRoomSpot.class, roomRid);
```

`getOrCreate(spotType, spotRid)`는 같은 `spotRid`가 이미 있으면 그 Spot을
재사용하고, 없으면 새로 만든다. 새 Spot의 시작 payload가 필요하면 별도
`create(spotType, spotRid)`로 만들고 lifecycle callback에서 받는다.

Spot factory는 Spot type 기준으로 등록한다. 같은 Spot type 중복 등록은 startup
validation 오류다.

## 4. Timer

Spot timer는 일반 scheduler helper가 아니라 Spot lifecycle에 묶인다. timer handler
exception은 monitoring event로 관찰된다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Channel Messaging](./05-channel-messaging.ko.md) | [다음: Actor/Session](./07-actor-session.ko.md)
<!-- framework-adapter-nav:bottom:end -->
