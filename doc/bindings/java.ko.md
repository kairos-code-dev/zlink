[English](java.md) | [한국어](java.ko.md)

# Java 바인딩

## 개요

Java 바인딩은 Java 22+ FFM API를 사용하며 현재 `core/include/zlink.h`
공식 contract에 맞춘 canonical surface를 기준으로 한다.

- raw send/recv 행위는 `Socket`
- payload 변환과 copy/borrow 경계는 `Message`
- recv 결과와 message ownership은 `Received`
- 서비스 모델은 `Discovery`, `Registry`, `SpotNode`, `Spot`

## Core API

```java
try (var ctx = new Context();
     var server = new Socket(ctx, SocketType.PAIR);
     var client = new Socket(ctx, SocketType.PAIR);
     var outbound = Message.copyOfUtf8("hello")) {
    server.bind("inproc://pair-example");
    client.connect("inproc://pair-example");

    client.send(outbound);

    try (Received received = server.recv()) {
        System.out.println(received.singlePartOrThrow().toUtf8String());
    }
}
```

canonical raw socket 표면:

- `send(Message)` / `send(List<Message>)`
- `send(RoutingId, Message)` / `send(RoutingId, List<Message>)`
- `recv()` / `recv(ReceiveFlag)`
- `publish(String topic, Message|List<Message>)`
- `subscribe()` / `subscribe(ReceiveFlag)` returning `TopicMessage`
- `setRoutingId(...)`, `routingId()`
- `setSubscription(...)`, `unsetSubscription(...)`, `subscriptions()`
- `onReceive(...)`, `onSubscribe(...)`, `onSendReady(...)`
- `monitorOpen(...)`, `attachDiscovery(...)`

## Message API

copy path:

- `Message.copyOf(byte[])`
- `Message.copyOfUtf8(String)`
- `Message.copyOf(ByteBuffer)`
- `Message.copyOf(io.netty.buffer.ByteBuf)`
- `Message.copyOf(ByteSpan)`

borrow / zero-copy path:

- `Message.wrapDirect(ByteBuffer)`
- `Message.wrapNative(MemorySegment)`
- `Message.wrapDirect(io.netty.buffer.ByteBuf)`
- `Message.wrap(ByteSpan)`

read path:

- `toByteArray()`
- `toUtf8String()`
- `dataSegment()`
- `dataBuffer()`
- `copyTo(...)`
- `size()`, `empty()`, `valid()`, `refCount()`, `property(...)`

`copyOf*` 는 항상 복사하고 `wrap*` 는 borrowed/native-backed 입력만 허용한다.

topic-aware receive path:

- `TopicMessage.topicId()`
- `TopicMessage.parts()`
- `TopicMessage.singlePartOrThrow()`
- `TopicMessage.close()`

## 서비스 API

Discovery:

- `new Discovery(ctx, serviceType, serviceName)`
- `connectRegistry(...)`
- `setValue(...)`, `getValue()`
- `setMetadata(...)`, `getMetadata()`
- `memberPeers()`, `memberPeerMetadata(...)`
- `monitorOpen(...)`

Registry:

- `bind(pubEndpoint, routerEndpoint)`
- `statusSnapshot()`
- `serviceSummarySnapshot(...)`
- `topologySnapshot()`, `topologyQuery(...)`
- `memberPeers(...)`, `memberPeerMetadata(...)`
- `RegistryQueryClient`

Spot:

- `new Spot(node)`
- `publish(topic, Message|List<Message>)`
- `subscribe(...)`, `unsubscribe(...)`
- `recv()`
- `onSubscribe(...)`, `onSendReady(...)`
- `monitorOpen(...)`

SpotNode:

- `bind(...)`, `connectPeer(...)`, `disconnectPeer(...)`
- `attachDiscovery(...)`
- `statusSnapshot()`
- `peersSnapshot()`, `peersQuery(...)`
- `subjectsSnapshot(...)`
- `monitorOpen(...)`

## 샘플

runnable sample 은 `bindings/java/samples/Zlink.Samples` 아래에 있다.

실행 task:

- `./gradlew :samples:runPairRecv`
- `./gradlew :samples:runPairCallback`
- `./gradlew :samples:runPubSubRecv`
- `./gradlew :samples:runPubSubCallback`
- `./gradlew :samples:runDealerRouterRecv`
- `./gradlew :samples:runDealerRouterCallback`
- `./gradlew :samples:runStreamRecv`
- `./gradlew :samples:runStreamCallback`
- `./gradlew :samples:runSpotRecv`
- `./gradlew :samples:runSpotCallback`

메모:

- `PairRecvSample` 은 `Message.copyOfUtf8(...)` 로 명시적 copy path 를 보여준다.
- `PairCallbackSample` 은 `SampleSupport.wrapUtf8(...)` 를 통해
  `Message.wrapDirect(...)` borrow path 를 보여준다.
- `StreamRecvSample`, `StreamCallbackSample` 은 core STREAM 계약대로
  zlink 쪽은 server-only, client 쪽은 raw TCP socket 으로 동작한다.

## 마이그레이션 메모

- `Receiver` 는 제거됐다. `Socket` 과 `Discovery` 를 만들고
  `socket.attachDiscovery(discovery)` 로 연결한다.
- split `spot_pub` / `spot_sub` 계열은 unified `Spot` 으로 대체한다.
- `Registry.setEndpoints()` / `start()` 는 `Registry.bind(pub, router)` 로 대체한다.
- legacy `Message.send/recv` 대신 `Socket.send/recv` 를 사용한다.
- old `setSockOpt/getSockOpt` 대신 dedicated helper 와 typed
  `SocketOptionKey` 우선 경로를 사용한다.

## 빌드

```groovy
dependencies {
    implementation files("path/to/zlink.jar")
    compileOnly "io.netty:netty-buffer:4.1.100.Final" // optional
}
```

네이티브 라이브러리는 `src/main/resources/native/` 에서 자동 로드된다.
