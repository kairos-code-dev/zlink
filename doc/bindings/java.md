[English](java.md) | [한국어](java.ko.md)

# Java Binding

## Overview

The Java binding targets Java 22+ and uses the FFM API. The canonical surface
is aligned to the current `core/include/zlink.h` contract:

- `Socket` owns raw send/recv behavior
- `Message` owns payload conversion and copy/borrow boundaries
- `Received` aggregates recv results and message ownership
- `Discovery`, `Registry`, `SpotNode`, and `Spot` expose the current service model

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

Canonical raw socket surface:

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

Copy path:

- `Message.copyOf(byte[])`
- `Message.copyOfUtf8(String)`
- `Message.copyOf(ByteBuffer)`
- `Message.copyOf(io.netty.buffer.ByteBuf)`
- `Message.copyOf(ByteSpan)`

Borrow / zero-copy path:

- `Message.wrapDirect(ByteBuffer)`
- `Message.wrapNative(MemorySegment)`
- `Message.wrapDirect(io.netty.buffer.ByteBuf)`
- `Message.wrap(ByteSpan)`

Read path:

- `toByteArray()`
- `toUtf8String()`
- `dataSegment()`
- `dataBuffer()`
- `copyTo(...)`
- `size()`, `empty()`, `valid()`, `refCount()`, `property(...)`

`copyOf*` always copies. `wrap*` is reserved for borrowed/native-backed input.

Topic-aware receive path:

- `TopicMessage.topicId()`
- `TopicMessage.parts()`
- `TopicMessage.singlePartOrThrow()`
- `TopicMessage.close()`

## Service API

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

## Samples

The runnable samples live in `bindings/java/samples/Zlink.Samples`.

Tasks:

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

Notes:

- `PairRecvSample` demonstrates the explicit copy path with
  `Message.copyOfUtf8(...)`.
- `PairCallbackSample` demonstrates the borrow path with
  `Message.wrapDirect(...)` via `SampleSupport.wrapUtf8(...)`.
- `StreamRecvSample` and `StreamCallbackSample` follow the core STREAM contract:
  the zlink side is server-only and the client side is a raw TCP socket.

## Migration Notes

- `Receiver` was removed. Use `Socket` plus `Discovery`, then connect them with
  `socket.attachDiscovery(discovery)`.
- Split `spot_pub` / `spot_sub` style APIs are replaced by unified `Spot`.
- `Registry.setEndpoints()` / `start()` is replaced by `Registry.bind(pub, router)`.
- Prefer `Socket.send/recv` over legacy `Message.send/recv`.
- Prefer dedicated helpers and typed `SocketOptionKey` values over old
  `setSockOpt/getSockOpt` usage.

## Build

```groovy
dependencies {
    implementation files("path/to/zlink.jar")
    compileOnly "io.netty:netty-buffer:4.1.100.Final" // optional
}
```

The binding auto-loads platform libraries from `src/main/resources/native/`.
