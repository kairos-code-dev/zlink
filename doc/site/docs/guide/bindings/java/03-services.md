[← 메시징](./02-messaging.md) · [Java 가이드](./index.md) · [다음: 운영 →](./04-operations.md)

# 서비스

Registry, Discovery, SpotNode/Spot, Actor의 Java API 사용법을 설명합니다.

---

## Registry

서비스 토폴로지를 유지하는 레지스트리 서버입니다.
([코어 참고](../../07-4-registry.md))

```java
try (Context ctx = Zlink.createContext();
     Registry registry = ctx.createRegistry()) {

    // PUB(상태 브로드캐스트)와 ROUTER(쿼리) 두 엔드포인트에 바인드
    registry.bind("tcp://127.0.0.1:7400", "tcp://127.0.0.1:7401");

    // 토폴로지 조회
    List<RegistryTopologyEntry> entries = registry.topology();
    for (var e : entries) {
        System.out.printf("role=%s channel=%s state=%s%n",
            e.serviceRole(), e.channelName(), e.state());
    }
}
```

---

## Discovery

레지스트리에서 피어를 배우고 자동으로 연결합니다.
([코어 참고](../../07-1-discovery.md))

```java
try (Context ctx = Zlink.createContext();
     Discovery discovery = ctx.createDiscovery(AutoConnectType.FANOUT, "prices");
     PubSocket pub = ctx.createPubSocket()) {

    discovery.connectRegistry("tcp://127.0.0.1:7401");

    // 소켓에 어태치 — 이후 해당 채널의 피어에 자동 연결
    pub.attachDiscovery(discovery);
    pub.bind("tcp://127.0.0.1:5600");
}
```

자동 연결 방식:

| 상수 | 설명 |
|------|------|
| `AutoConnectType.FANOUT` | PUB→SUB 팬아웃 |
| `AutoConnectType.ROUTE_MESH` | ROUTER 풀 메시 |
| `AutoConnectType.CLIENT_SERVER` | 클라이언트가 서버에 연결 |
| `AutoConnectType.DEALER_MESH` | DEALER 풀 메시 |
| `AutoConnectType.SPOT_MESH` | SpotNode 메시 |

---

## SpotNode / Spot

메시 노드(SpotNode)와 그 위에서 동작하는 메시징 엔드포인트(Spot)입니다.
([코어 참고](../../07-3-spot.md))

### SpotNode 설정

```java
try (Context ctx = Zlink.createContext();
     SpotNode node = ctx.createSpotNode();
     Spot spot = node.createSpot()) {

    // 노드 라우팅 ID
    node.setRoutingId(RoutingId.from("node-1".getBytes(UTF_8)));

    // PUB/SUB 메시지를 위한 엔드포인트 바인드
    node.setPubBind("tcp://127.0.0.1:5700");

    // 다른 노드에 연결
    node.connectPeer("tcp://10.0.0.2:5700");
    // ...
}
```

### Spot 발행/구독

```java
spot.setRoutingId(RoutingId.from("spot-pub".getBytes(UTF_8)));
spot.setSubscription("market:BTC"); // 구독 토픽 등록

// 발행
try (Message msg = Message.from("67000.00")) {
    spot.publish("market:BTC").message(msg).submit();
}

// 구독 수신
try (TopicMessage topic = new TopicMessage()) {
    if (spot.subscribe(topic, RecvFlags.NONE)) {
        System.out.printf("%s: %s%n",
            topic.topic(),
            topic.singlePartOrThrow().toUtf8String());
    }
}
```

### Spot 비동기 요청

```java
try (Message req = Message.from("{\"symbol\":\"BTC\"}")) {
    CompletableFuture<List<Message>> future = spot.requestToChannel("quotes")
        .message(req)
        .timeout(Duration.ofSeconds(5))
        .submitAsync();

    List<Message> reply = future.get(6, TimeUnit.SECONDS);
    try {
        System.out.println(reply.get(0).toUtf8String());
    } finally {
        Message.closeAll(reply);
    }
}
```

### 디스패치 핸들러

이벤트 루프 방식으로 Spot 이벤트를 처리합니다:

```java
spot.setDispatchHandler(info -> {
    switch (info.event()) {
        case SUBSCRIBE_READABLE -> {
            try (TopicMessage msg = new TopicMessage()) {
                spot.subscribe(msg, RecvFlags.DONT_WAIT);
                processMessage(msg);
            }
        }
        case ACTOR_JOIN_READABLE -> {
            try (ActorJoinRequest req = spot.recvActorJoin(RecvFlags.DONT_WAIT)) {
                if (req != null) {
                    try (Message reply = Message.from("welcome")) {
                        spot.replyActorJoin(req, 0).message(reply).submit();
                    }
                }
            }
        }
    }
});
```

---

## Actor

상태를 가진 엔티티(플레이어, 세션, 에이전트)입니다.
([코어 참고](../../07-4-actor.md))

### 액터 생성

```java
try (Context ctx = Zlink.createContext();
     SpotNode node = ctx.createSpotNode();
     Spot spot = node.createSpot()) {

    Actor actor = node.createActor("player-42");
    ActorRef ref = actor.ref(); // 다른 노드에서 참조할 때 사용
    // 액터는 노드가 닫히면 함께 소멸됩니다
}
```

### 스팟 조인

```java
CountDownLatch joinDone = new CountDownLatch(1);

try (Message payload = Message.from("join-request")) {
    actor.join(spot)
        .message(payload)
        .timeout(Duration.ofSeconds(5))
        .submit(actor.ref(), (result, parts) -> {
            try {
                if (result.result() == RequestResult.OK) {
                    joinDone.countDown();
                }
            } finally {
                Message.closeAll(parts);
            }
        });
}

// 스팟에서 조인 요청 수락 (디스패치 핸들러 또는 직접 처리)
try (ActorJoinRequest req = spot.recvActorJoin(RecvFlags.NONE)) {
    try (Message reply = Message.from("welcome")) {
        spot.replyActorJoin(req, 0).message(reply).submit();
    }
}

joinDone.await(6, TimeUnit.SECONDS);
```

### 액터 메시지 수신

```java
try (ActorReceived received = actor.recv(RecvFlags.DONT_WAIT)) {
    if (received != null) {
        System.out.printf("from=%s msg=%s%n",
            received.info().actor().actorId(),
            received.message().toUtf8String());
    }
}
```

### 스팟 떠나기

```java
List<Message> reply = actor.leave(spot)
    .timeout(Duration.ofSeconds(3))
    .submitAsync()
    .get(4, TimeUnit.SECONDS);
Message.closeAll(reply);
```
