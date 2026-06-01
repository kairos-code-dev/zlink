[← 메시징](./02-messaging.md) · [Java 가이드](./index.md) · [다음: 운영 →](./04-operations.md)

# 서비스 레이어

raw 소켓은 "주소를 아는 두 지점"을 잇습니다. 서비스 레이어는 그 위에 **동적
토폴로지**(이름으로 발견, 런타임에 생겼다 사라지는 단위)를 얹습니다. 개념의 정식
정의와 층별 멘탈 모델은
[서비스 개요 §멘탈 모델](../../07-0-services.md)이
소유하며, 이 문서는 **각 기능의 역할·언제** + **Java 사용 형태**를 다룹니다. 모든
서비스 객체는 컨텍스트 팩토리로 만들고 `AutoCloseable`입니다.

> 핵심: 서비스 레이어는 **상태 저장소가 아닙니다.** 룸·세션 데이터는 응용이
> 소유합니다. SPOT은 그 상태에 닿는 메시지를 **단일 실행 큐로 직렬 처리**(lock
> 불필요)하고, Actor는 세션이 어느 서버에 붙어 있든 **같은 엔티티로 이어 줍니다**.

## 언제 서비스 레이어가 필요한가

| 상황 | 권장 |
|------|------|
| 주소가 고정된 소수 노드 | raw 소켓([02 메시징](./02-messaging.md))으로 충분 |
| 노드가 동적으로 늘고 줄어 이름으로 찾아야 함 | **Registry + Discovery** |
| 방·스테이지·존처럼 런타임에 생기는 라우팅 단위 | **SpotNode / Spot** |
| 세션/플레이어처럼 정체성을 갖는 엔티티 | **Actor** |

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

메시 노드(SpotNode)와 그 위의 메시징 엔드포인트(Spot)입니다. "방·스테이지·존"
같은 동적 단위가 전형적인 Spot입니다. 개념: [SPOT](../../07-3-spot.md).

**왜 Spot인가 — 실행 직렬성.** 한 Spot으로 들어온 메시지는 **단일 실행 큐로 직렬
처리**됩니다. 룸 상태를 lock으로 보호할 필요 없이 동시성 문제가 사라집니다. 게임
룸·심볼 오더북·채팅방처럼 한 단위의 상태를 안전하게 갱신할 때 raw PUB/SUB 대신
Spot을 쓰는 이유입니다. 상태 데이터는 여전히 응용이 소유합니다.

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

Spot에 합류(join)해 그 Spot으로 들어온 메시지를 받는 **상태 보유 엔티티**입니다
(플레이어·세션·작업 큐). 개념: [Actor](../../07-4-actor.md).

**왜 Actor인가 — 재접속 이전성.** Actor는 actor id로 식별되며 세션 연결과 별개로
존재합니다. 클라이언트가 끊겼다 다른 연결 서버로 재접속해도 같은 Actor로 다시
묶입니다 — "어느 서버에 붙어 있었는지"를 외부 저장소로 관리하던 일을 라이브러리가
가져갑니다. Actor는 raw 소켓의 대안이 아니라 **Spot 위에 얹는 한 단계 더 높은
모델**이며, Actor 메시지도 결국 Spot routed 평면 위로 흐릅니다.

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
        .submit((result, parts) -> {
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
