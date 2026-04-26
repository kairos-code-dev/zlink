[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Java Binding Specification

This document defines the public contract surface of the Java binding.
Every exported API class, its purpose, and all public method signatures are
listed. Internal helpers and implementation details are omitted.

All types live in the `dev.kairoscode.zlink` package.
Service types live in `dev.kairoscode.zlink.service.registry`,
`dev.kairoscode.zlink.service.discovery`, and
`dev.kairoscode.zlink.service.spot`.
Netty buffer extension types live in `dev.kairoscode.zlink.netty`.

Only the packages and types listed in this document are public contract.
`dev.kairoscode.zlink.internal` and other implementation packages are internal
implementation detail. Public classes may exist in those packages for local
implementation use, but they are not contract and must not be exported as
public API packages. If the binding uses JPMS/module export control, only
documented public packages may be exported. Perf, samples, and tests must use
the public Java entrypoint only and must not import internal packages or helper
classes.

Implementation follow-up:
- internal 성격 타입이 public package에 남아 있지 않도록 정리해야 한다.
- `SocketCore`, `MessagePlane`, request/reply support helper 같은 구현 중심
  타입은 internal 또는 implementation package로 이동하는 방향이 맞다.
- JPMS를 도입하면 documented public package만 export 하도록 맞춰야 한다.

---

## Current Core Alignment Overrides

The sections below still contain some older signatures. When they conflict
with the rules here, this section wins.

- `PairSocket`, `DealerSocket`, and `RouterSocket` are recv-only on the data
  plane. Remove `onReceive(...)` from their public contract.
- `SubSocket` and `XSubSocket` are recv-only. Remove `onSubscribe(...)` from
  their public contract.
- `StreamSocket` keeps `recv(...)` and exposes a packet callback surface
  mapped to `zlink_stream_packet_handler()`. Recommended canonical name:
  `onPacket(...)`.
- `SpotNode` must expose channel-aware attachment APIs:
  `attachDiscovery(Discovery discovery)`,
  `attachChannelDealer(Discovery discovery, DealerSocket dealer)`,
  `attachChannelDealerManual(String channelName, DealerSocket dealer)`, and
  `attachPubIngress(PubSocket pub)`.
- `Spot` must expose channel-aware data-plane methods:
  `sendChannel(...)`, `sendToSpot(...)`, `requestChannel(...)`, and
  `publish(String serviceName, String topic, ...)`.
- `Spot.subscribe(...)` returns a service-aware `TopicMessage`.
  `TopicMessage` therefore needs `serviceName()` populated for SPOT subscribe
  results and empty for raw `SUB` / `XSUB`.
- `Spot` must not expose `onSubscribe(...)`.
- `SpotDispatchEvent.SUBSCRIBE_READABLE` and `.ROUTED_READABLE` are readiness
  notifications, not one-event-per-message delivery counters. Binding docs and
  samples must drain until the recv path reports `EAGAIN`.
- Peer weight is exposed only on `RouterSocket` and `DealerSocket` through typed option/property surfaces. The value range is `0..100`, default `100`; `0` drains new outbound selection. Submit attempts to a weight-`0` peer raise `SubmitException` with
  `getCode() == SubmitResult.NOT_ADMITTED`.
- `POLLOUT` is a send-recovery readiness signal, shared with
  `onSendReady(...)`. It is not a "transport writable" bit.
- ROUTER / PUB socket option defaults follow the core header: `mandatory =
  true`, `handover = false`, `nodrop = true`.
- Internal pairing rule: when auto-connect pairs two same-service ROUTERs
  via Discovery, the library picks one initiator per pair by a total order
  on `(routingId, advertiseEndpoint)`. Users do not configure this.

## Core

### Context

RAII-style context that manages IO threads and sockets.
Implements `AutoCloseable`.

```java
public final class Context implements AutoCloseable {
    Context();

    ContextOptions options();
    void shutdown();                                                 // @throws CloseException
    void close();                                                    // @throws CloseException
}
```

## Peer Disconnect by Routing ID

Java bindings expose `disconnectRid(routingId)` on raw sockets and
`disconnectPeerRid(targetNodeRid)` on `SpotNode`. The duplicate policy option
and `NOT_FOUND` / `CONFLICT` / `BUSY` connect errors mirror the C core. `Spot`
does not expose a peer-rid disconnect method.

### ContextOptions

Typed facade for context configuration options.

```java
public final class ContextOptions {
    int ioThreads();                                                 // @throws ConfigException
    void ioThreads(int count);                                       // @throws ConfigException
    int maxSockets();                                                // @throws ConfigException
    void maxSockets(int count);                                      // @throws ConfigException
    int socketLimit();                                               // @throws ConfigException
    int threadPriority();                                            // @throws ConfigException
    void threadPriority(int priority);                               // @throws ConfigException
    int threadSchedulingPolicy();                                    // @throws ConfigException
    void threadSchedulingPolicy(int policy);                         // @throws ConfigException
    int maxMsgSize();                                                // @throws ConfigException
    void maxMsgSize(int bytes);                                      // @throws ConfigException
    int msgTSize();                                                  // @throws ConfigException
    boolean blocky();                                                // @throws ConfigException
    void blocky(boolean enabled);                                    // @throws ConfigException
    boolean autoHwmEnabled();                                        // @throws ConfigException
    void autoHwmEnabled(boolean enabled);                            // @throws ConfigException
    int autoHwmTotalMemoryBudgetMb();                                // @throws ConfigException
    void autoHwmTotalMemoryBudgetMb(int value);                      // @throws ConfigException
    void addThreadAffinity(int cpu);                                 // @throws ConfigException
    void removeThreadAffinity(int cpu);                              // @throws ConfigException
}
```

---

## Socket Types

### Common base methods

All socket types inherit from `Socket` and expose these common operations.

```java
// Available on all socket types
void close();                                                    // @throws CloseException
MonitorSocket monitorOpen();                                    // @throws ConfigException
MonitorSocket monitorOpen(MonitorEventType... events);          // @throws ConfigException
// No common peer-weight accessor. Bindings expose weight only on
// RouterSocket and DealerSocket.

// Available on message-capable sockets
boolean send(Message part, SendFlags flags);                            // @throws SubmitException
boolean send(List<Message> parts, SendFlags flags);                     // @throws SubmitException
@Nullable Received recv(RecvFlags flags);                               // @throws RecvException

// Available on routed message sockets
boolean send(RoutingId rid, Message part, SendFlags flags);             // @throws SubmitException
boolean send(RoutingId rid, List<Message> parts, SendFlags flags);      // @throws SubmitException
```

`send(...)` and `publish(...)` return `false` only for temporary backpressure
when `SendFlags.DONTWAIT` is used. Blocking submit returns `true` on success.
Route-not-ready and other submit failures still raise `SubmitException`.
`recv(...)` and `subscribe(...)` return `null` when `RecvFlags.DONTWAIT`
finds no message and still raise `RecvException` for real recv failures.

### PairSocket

Bidirectional exclusive pair socket.

```java
public final class PairSocket extends Socket {
    PairSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException

    boolean send(Message part);                                      // @throws SubmitException
    boolean send(Message part, SendFlags flags);                     // @throws SubmitException
    boolean send(List<Message> parts);                               // @throws SubmitException
    boolean send(List<Message> parts, SendFlags flags);              // @throws SubmitException

    Received recv();                                                 // @throws RecvException
    @Nullable Received recv(RecvFlags flags);                        // @throws RecvException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException
}
```

### PubSocket

Publisher socket. Sends topic-prefixed messages to all matching subscribers.

```java
public final class PubSocket extends Socket {
    PubSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException
    void attachDiscovery(Discovery discovery);                       // @throws ConfigException

    boolean publish(String topicId, Message part);                   // @throws SubmitException
    boolean publish(String topicId, Message part, SendFlags flags);  // @throws SubmitException
    boolean publish(String topicId, List<Message> parts);            // @throws SubmitException
    boolean publish(String topicId, List<Message> parts, SendFlags flags); // @throws SubmitException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException

    PubSocketOptions options();
}
```

### SubSocket

Subscriber socket. Receives topic-filtered messages from publishers.

```java
public final class SubSocket extends Socket {
    SubSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException
    void attachDiscovery(Discovery discovery);                       // @throws ConfigException

    void setSubscription(String filter);                             // @throws ConfigException
    void unsetSubscription(String filter);                           // @throws ConfigException
    TopicMessage subscribe();                                        // @throws RecvException
    @Nullable TopicMessage subscribe(RecvFlags flags);               // @throws RecvException

    SubSocketOptions options();
}
```

### DealerSocket

Asynchronous client socket for fair-queued request distribution.

```java
public final class DealerSocket extends Socket {
    DealerSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException
    void attachDiscovery(Discovery discovery);                       // @throws ConfigException
    void setChannelName(String channelName);                         // @throws ConfigException
    String getChannelName();                                         // @throws ConfigException

    void setRoutingId(RoutingId rid);                                // @throws ConfigException
    RoutingId routingId();                                           // @throws ConfigException

    boolean send(Message part);                                      // @throws SubmitException
    boolean send(Message part, SendFlags flags);                     // @throws SubmitException
    boolean send(List<Message> parts);                               // @throws SubmitException
    boolean send(List<Message> parts, SendFlags flags);              // @throws SubmitException

    Received recv();                                                 // @throws RecvException
    @Nullable Received recv(RecvFlags flags);                        // @throws RecvException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException

    // --- request (async, no flags) ---
    CompletableFuture<List<Message>> request(Message part);                           // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> request(Message part, Duration timeout);         // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> request(List<Message> parts);                    // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> request(List<Message> parts, Duration timeout);  // @throws SubmitException; future completes with RequestException on failure

    // --- request (callback submit) ---
    boolean request(Message part,
                    BiConsumer<RequestResult, List<Message>> callback);                 // @throws SubmitException; callback receives RequestResult
    boolean request(Message part,
                    BiConsumer<RequestResult, List<Message>> callback,
                    Duration timeout);                                                  // @throws SubmitException; callback receives RequestResult
    boolean request(Message part,
                    BiConsumer<RequestResult, List<Message>> callback,
                    SendFlags flags);                                                   // @throws SubmitException; false only on temporary backpressure
    boolean request(Message part,
                    BiConsumer<RequestResult, List<Message>> callback,
                    SendFlags flags,
                    Duration timeout);                                                  // @throws SubmitException; false only on temporary backpressure
    boolean request(List<Message> parts,
                    BiConsumer<RequestResult, List<Message>> callback);                 // @throws SubmitException; callback receives RequestResult
    boolean request(List<Message> parts,
                    BiConsumer<RequestResult, List<Message>> callback,
                    Duration timeout);                                                  // @throws SubmitException; callback receives RequestResult
    boolean request(List<Message> parts,
                    BiConsumer<RequestResult, List<Message>> callback,
                    SendFlags flags);                                                   // @throws SubmitException; false only on temporary backpressure
    boolean request(List<Message> parts,
                    BiConsumer<RequestResult, List<Message>> callback,
                    SendFlags flags,
                    Duration timeout);                                                  // @throws SubmitException; false only on temporary backpressure

    DealerSocketOptions options();
}
```

### RouterSocket

Server socket that routes messages to specific peers by routing id.

```java
public final class RouterSocket extends Socket {
    RouterSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException
    void attachDiscovery(Discovery discovery);                       // @throws ConfigException

    void setRoutingId(RoutingId rid);                                // @throws ConfigException
    RoutingId routingId();                                           // @throws ConfigException

    boolean send(RoutingId rid, Message part);                       // @throws SubmitException
    boolean send(RoutingId rid, Message part, SendFlags flags);      // @throws SubmitException
    boolean send(RoutingId rid, List<Message> parts);                // @throws SubmitException
    boolean send(RoutingId rid, List<Message> parts, SendFlags flags); // @throws SubmitException

    Received recv();                                                 // @throws RecvException
    @Nullable Received recv(RecvFlags flags);                        // @throws RecvException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException

    // --- request to a specific peer (async, no flags) ---
    CompletableFuture<List<Message>> request(RoutingId rid, Message part);                          // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> request(RoutingId rid, Message part, Duration timeout);        // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> request(RoutingId rid, List<Message> parts);                   // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> request(RoutingId rid, List<Message> parts, Duration timeout); // @throws SubmitException; future completes with RequestException on failure

    // --- request to a specific peer (callback submit) ---
    boolean request(RoutingId rid, Message part,
                    BiConsumer<RequestResult, List<Message>> callback);                 // @throws SubmitException; callback receives RequestResult
    boolean request(RoutingId rid, Message part,
                    BiConsumer<RequestResult, List<Message>> callback,
                    Duration timeout);                                                  // @throws SubmitException; callback receives RequestResult
    boolean request(RoutingId rid, Message part,
                    BiConsumer<RequestResult, List<Message>> callback,
                    SendFlags flags);                                                   // @throws SubmitException; false only on temporary backpressure
    boolean request(RoutingId rid, Message part,
                    BiConsumer<RequestResult, List<Message>> callback,
                    SendFlags flags,
                    Duration timeout);                                                  // @throws SubmitException; false only on temporary backpressure
    boolean request(RoutingId rid, List<Message> parts,
                    BiConsumer<RequestResult, List<Message>> callback);                 // @throws SubmitException; callback receives RequestResult
    boolean request(RoutingId rid, List<Message> parts,
                    BiConsumer<RequestResult, List<Message>> callback,
                    Duration timeout);                                                  // @throws SubmitException; callback receives RequestResult
    boolean request(RoutingId rid, List<Message> parts,
                    BiConsumer<RequestResult, List<Message>> callback,
                    SendFlags flags);                                                   // @throws SubmitException; false only on temporary backpressure
    boolean request(RoutingId rid, List<Message> parts,
                    BiConsumer<RequestResult, List<Message>> callback,
                    SendFlags flags,
                    Duration timeout);                                                  // @throws SubmitException; false only on temporary backpressure

    // --- reply to a received request ---
    void reply(RoutingId rid, long requestSeq, Message message);                        // @throws SubmitException
    void reply(RoutingId rid, long requestSeq, Message message, SendFlags flags);       // @throws SubmitException
    void reply(RoutingId rid, long requestSeq, List<Message> parts);                    // @throws SubmitException
    void reply(RoutingId rid, long requestSeq, List<Message> parts, SendFlags flags);   // @throws SubmitException

    // --- router -> spot routed send ---
    boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message part);      // @throws SubmitException
    boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message part,
                       SendFlags flags);                                                 // @throws SubmitException
    boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, List<Message> parts); // @throws SubmitException
    boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, List<Message> parts,
                       SendFlags flags);                                                 // @throws SubmitException

    // --- router -> spot routed request (async, no flags) ---
    CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              Message part);                             // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              Message part, Duration timeout);           // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              List<Message> parts);                      // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              List<Message> parts, Duration timeout);    // @throws SubmitException; future completes with RequestException on failure

    // --- router -> spot routed request (callback submit) ---
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          Message part,
                          BiConsumer<RequestResult, List<Message>> callback);            // @throws SubmitException; callback receives RequestResult
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          Message part,
                          BiConsumer<RequestResult, List<Message>> callback,
                          Duration timeout);                                             // @throws SubmitException; callback receives RequestResult
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          Message part,
                          BiConsumer<RequestResult, List<Message>> callback,
                          SendFlags flags);                                              // @throws SubmitException; false only on temporary backpressure
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          Message part,
                          BiConsumer<RequestResult, List<Message>> callback,
                          SendFlags flags,
                          Duration timeout);                                             // @throws SubmitException; false only on temporary backpressure
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          List<Message> parts,
                          BiConsumer<RequestResult, List<Message>> callback);            // @throws SubmitException; callback receives RequestResult
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          List<Message> parts,
                          BiConsumer<RequestResult, List<Message>> callback,
                          Duration timeout);                                             // @throws SubmitException; callback receives RequestResult
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          List<Message> parts,
                          BiConsumer<RequestResult, List<Message>> callback,
                          SendFlags flags);                                              // @throws SubmitException; false only on temporary backpressure
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          List<Message> parts,
                          BiConsumer<RequestResult, List<Message>> callback,
                          SendFlags flags,
                          Duration timeout);                                             // @throws SubmitException; false only on temporary backpressure

    // --- router -> spot routed reply ---
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, Message message);                                  // @throws SubmitException
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, Message message, SendFlags flags);                 // @throws SubmitException
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts);                              // @throws SubmitException
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts, SendFlags flags);             // @throws SubmitException

    RouterSocketOptions options();
}
```

### XPubSocket

Extended publisher. Like PubSocket but also receives subscription events.

```java
public final class XPubSocket extends Socket {
    XPubSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException

    boolean publish(String topicId, Message part);                   // @throws SubmitException
    boolean publish(String topicId, Message part, SendFlags flags);  // @throws SubmitException
    boolean publish(String topicId, List<Message> parts);            // @throws SubmitException
    boolean publish(String topicId, List<Message> parts, SendFlags flags); // @throws SubmitException

    SubscriptionEvent receiveSubscriptionEvent();                    // @throws RecvException
    SubscriptionEvent receiveSubscriptionEvent(RecvFlags flags);     // @throws RecvException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException

    PubSocketOptions options();
}
```

### XSubSocket

Extended subscriber. Like SubSocket with raw subscription forwarding.

```java
public final class XSubSocket extends Socket {
    XSubSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException

    void setSubscription(String filter);                             // @throws ConfigException
    void unsetSubscription(String filter);                           // @throws ConfigException
    TopicMessage subscribe();                                        // @throws RecvException
    @Nullable TopicMessage subscribe(RecvFlags flags);               // @throws RecvException

    SubSocketOptions options();
}
```

### StreamSocket

Raw TCP stream socket. Bind-only; does not support `connect`.

```java
public final class StreamSocket extends Socket {
    StreamSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void unbind(String endpoint);                                    // @throws ConnectException

    boolean send(int rid, Message part);                             // @throws SubmitException
    boolean send(int rid, Message part, SendFlags flags);            // @throws SubmitException
    boolean send(RoutingId rid, Message part);                       // @throws SubmitException
    boolean send(RoutingId rid, Message part, SendFlags flags);      // @throws SubmitException
    boolean send(RoutingId rid, List<Message> parts);                // @throws SubmitException
    boolean send(RoutingId rid, List<Message> parts, SendFlags flags); // @throws SubmitException

    Received recv();                                                 // @throws RecvException
    @Nullable Received recv(RecvFlags flags);                        // @throws RecvException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException

    // Two mutually-exclusive receive modes on the same StreamSocket:
    //   (1) recv(), (2) onPacket(handler). Second attach raises
    //   HandlerException(HandlerResult.BUSY).
    // Mode (3): framed packet callback mapped to
    //   zlink_stream_packet_handler(). Wire frame is big-endian u16
    //   header_size + u32 body_size + header + body. The handler receives
    //   the source routing id, a header Message, and a body Message; both
    //   messages transfer ownership to the handler.
    void onPacket(StreamPacketHandler handler);                      // @throws HandlerException
    void detachStream();                                             // @throws ConfigException

    StreamSocketOptions options();
}
```

---

## Message / Domain Types

### Message

Owns one native message frame. Implements `AutoCloseable`.

```java
public final class Message implements AutoCloseable {
    Message();                                                       // @throws ConfigException
    Message(int size);                                               // @throws ConfigException

    // --- factories (copy) ---
    // All public input adapters copy into an owned zlink message.
    // Public borrowed external-wrap APIs are intentionally not exposed.
    static Message copyOf(Message source);                          // @throws ConfigException
    static Message copyOf(byte[] data);                              // @throws ConfigException
    static Message copyOf(byte[] data, int offset, int length);      // @throws ConfigException
    static Message copyOfUtf8(String value);                         // @throws ConfigException
    static Message copyOf(ByteBuffer data);                          // @throws ConfigException
    static Message copyOf(ByteSpan span);                            // @throws ConfigException
    static Message copyOf(MemorySegment data);                       // @throws ConfigException
    static Message copyOf(MemorySegment data, long offset, long length); // @throws ConfigException

    // --- accessors ---
    int size();
    int refCount();
    boolean empty();
    boolean valid();
    byte[] data();
    byte[] toByteArray();
    String toUtf8String();
    ByteBuffer dataBuffer();
    int readIntLe(int offset);
    long readLongLe(int offset);
    String getProperty(String key);                                  // @throws ConfigException
    Message move();                                                  // @throws ConfigException

    // --- writable owned payload ---
    void fill(byte value);                                           // @throws ConfigException
    void fill(byte value, int offset, int length);                   // @throws ConfigException
    void writeByte(int offset, byte value);                          // @throws ConfigException
    void writeIntLe(int offset, int value);                          // @throws ConfigException
    void writeLongLe(int offset, long value);                        // @throws ConfigException

    // --- copy to destination ---
    int copyTo(byte[] destination);
    int copyTo(byte[] destination, int offset);
    int copyTo(ByteBuffer destination);
    boolean tryCopyTo(ByteBuffer destination);

    // --- batch close ---
    static void closeAll(Message[] parts);                           // @throws CloseException
    static void closeAll(Iterable<? extends Message> parts);         // @throws CloseException

    void close();                                                    // @throws CloseException
}
```

`Message` core contract stays on JDK-owned buffer types such as `byte[]`,
`ByteBuffer`, and `MemorySegment`. Third-party network buffer types such as
Netty `ByteBuf` are kept out of the core artifact so the base Java binding
does not take a mandatory Netty dependency.

### Codec Extensions

The binding exposes separate codec extension libraries. The exported public
distribution artifacts and public package names are fixed to:

- Maven `zlink-codec-protobuf`
- Maven `zlink-codec-json`
- Maven `zlink-codec-messagepack`

- `dev.kairoscode.zlink.codec.protobuf`
- `dev.kairoscode.zlink.codec.json`
- `dev.kairoscode.zlink.codec.messagepack`

These packages are separate public modules layered on top of the core binding.
The public codec API stays in these packages and is published as separate
artifacts. The core module may still keep supporting implementation
dependencies, but codec entrypoints are not part of `dev.kairoscode.zlink`.

JSON codec baseline: `Jackson`.
MessagePack codec baseline: `jackson-dataformat-msgpack`.

```java
package dev.kairoscode.zlink.codec.protobuf;

public final class ProtobufCodec {
    public static <T extends com.google.protobuf.MessageLite> T parseProto(
        dev.kairoscode.zlink.Message message,
        com.google.protobuf.Parser<T> parser);

    public static dev.kairoscode.zlink.Message toMessage(
        com.google.protobuf.MessageLite value);
}
```

```java
package dev.kairoscode.zlink.codec.json;

public final class JsonCodec {
    public static <T> T parseJson(
        dev.kairoscode.zlink.Message message,
        Class<T> type);

    public static dev.kairoscode.zlink.Message toMessage(Object value);
}
```

```java
package dev.kairoscode.zlink.codec.messagepack;

public final class MessagePackCodec {
    public static <T> T parseMessagePack(
        dev.kairoscode.zlink.Message message,
        Class<T> type);

    public static dev.kairoscode.zlink.Message toMessage(Object value);
}
```

### Netty Buffer Extension

Netty `ByteBuf` support is a separate public extension layered on top of the
core binding. The public adapter surface lives in `dev.kairoscode.zlink.netty`
and is published as `zlink-ext-netty`.

The exported public distribution artifact and public package name are fixed to:

- Maven `zlink-ext-netty`
- `dev.kairoscode.zlink.netty`

This extension exists because `ByteBuf` is valuable in Java network stacks,
but it is still a third-party dependency. The public `ByteBuf` adapter stays
separate so Netty-specific entrypoints do not become part of the core package.
Current core implementation may still use optional `netty-buffer`
dependencies for internal low-level fast paths; that internal detail is not
part of the public Java contract.

Ownership rules:

- `copyOf(ByteBuf)` copies the readable bytes between `readerIndex` and
  `writerIndex`.
- `copyOf(ByteBuf)` must not change `readerIndex` or `writerIndex`.
- the extension must not call `retain()` or `release()` on caller-owned
  `ByteBuf`.
- `copyTo(Message, ByteBuf)` copies the full message at the current
  `writerIndex`.
- `copyTo(Message, ByteBuf)` may advance `writerIndex` by the copied byte
  count, but must not change `readerIndex`.
- `copyTo(Message, ByteBuf)` must not call `retain()` or `release()`.

```java
package dev.kairoscode.zlink.netty;

public final class NettyMessages {
    public static dev.kairoscode.zlink.Message copyOf(
        io.netty.buffer.ByteBuf source);

    public static int copyTo(
        dev.kairoscode.zlink.Message message,
        io.netty.buffer.ByteBuf destination);
}
```

### RoutingId

Immutable binary-safe routing identity value object (1-255 bytes). The
canonical constructor is `fromBytes(byte[])`; no string-only constructor
is provided. `toHex()` is offered as a convenience only.

```java
public final class RoutingId {
    static final int MAX_LENGTH = 255;

    // --- factories (binary-safe) ---
    static RoutingId fromBytes(byte[] bytes);
    static RoutingId fromBytes(byte[] bytes, int offset, int length);

    // --- accessors ---
    byte[] toBytes();                       // defensive copy of raw bytes
    ByteBuffer asReadOnlyBuffer();
    int size();                             // 1-255
    boolean empty();

    // --- convenience (not canonical) ---
    String toHex();

    boolean equals(Object other);
    int hashCode();
}
```

### SendFlags

Flags that control send behavior (blocking vs. non-blocking).

```java
public enum SendFlags {
    NONE(0),
    DONT_WAIT(1);

    private final int value;
    SendFlags(int value) { this.value = value; }
    public int value() { return value; }
}
```

### RecvFlags

Flags that control receive behavior (blocking vs. non-blocking).

```java
public enum RecvFlags {
    NONE(0),
    DONT_WAIT(1);

    private final int value;
    RecvFlags(int value) { this.value = value; }
    public int value() { return value; }
}
```

### ZlinkException

Abstract unchecked parent of all zlink exceptions.
Every failing operation throws one of the eight concrete subclasses below,
each of which corresponds to a C API function-category result enum
(`SubmitException`, `RequestException`, `RecvException`, `HandlerException`,
`CloseException`, `BindException`, `ConnectException`, `ConfigException`).
All subclasses extend `RuntimeException` indirectly via `ZlinkException`, so
they are unchecked; callers do not need `throws` clauses. Catch
`ZlinkException` for the "catch-all" idiom, or a specific subclass when
finer-grained handling is required.

The `code` field is a globally unique `int` that spans all result enum
ranges (0-703). The code alone identifies the error without needing to
know which enum it belongs to. `internalErrno` carries the OS-level
errno when available (0 otherwise).

```java
public abstract class ZlinkException extends RuntimeException {
    protected ZlinkException(int code);
    protected ZlinkException(int code, int internalErrno);

    public int getCode();
    public int getInternalErrno();
    public String getMessage();
}
```

### SubmitException

Thrown by send / publish / reply / request (callback submit) operations.
Wraps a `SubmitResult`.

```java
public final class SubmitException extends ZlinkException {
    public SubmitException(SubmitResult result);
    public SubmitException(SubmitResult result, int internalErrno);
    public SubmitResult getResult();
}
```

### RequestException

Thrown by request completion paths (coroutine/future variants) and used as
the category for request-specific failures. Wraps a `RequestResult`.
Callback-style `request(...)` methods deliver `RequestResult` directly to
the callback rather than throwing this exception.

```java
public final class RequestException extends ZlinkException {
    public RequestException(RequestResult result);
    public RequestException(RequestResult result, int internalErrno);
    public RequestResult getResult();
}
```

### RecvException

Thrown by recv / subscribe / subscription-event / monitor recv / timer recv
operations. Wraps a `RecvResult`.

```java
public final class RecvException extends ZlinkException {
    public RecvException(RecvResult result);
    public RecvException(RecvResult result, int internalErrno);
    public RecvResult getResult();
}
```

### HandlerException

Thrown by handler registration methods (`onPacket`, `onSendReady`,
`onRoutedReceive`, `onDispatchEvent`, `onEvent`, etc.). Wraps a
`HandlerResult`.

```java
public final class HandlerException extends ZlinkException {
    public HandlerException(HandlerResult result);
    public HandlerException(HandlerResult result, int internalErrno);
    public HandlerResult getResult();
}
```

### CloseException

Thrown by `close()` / `destroy()` operations. Wraps a `CloseResult`.

```java
public final class CloseException extends ZlinkException {
    public CloseException(CloseResult result);
    public CloseException(CloseResult result, int internalErrno);
    public CloseResult getResult();
}
```

### BindException

Thrown by `bind(...)` operations. Wraps a `BindResult`.

```java
public final class BindException extends ZlinkException {
    public BindException(BindResult result);
    public BindException(BindResult result, int internalErrno);
    public BindResult getResult();
}
```

### ConnectException

Thrown by `connect(...)`, `disconnect(...)`, and `unbind(...)` operations.
Wraps a `ConnectResult`.

```java
public final class ConnectException extends ZlinkException {
    public ConnectException(ConnectResult result);
    public ConnectException(ConnectResult result, int internalErrno);
    public ConnectResult getResult();
}
```

### ConfigException

Thrown by option set/get, snapshot, poller mutation, proxy, timer
configuration, TLS setup, discovery attach, and message lifecycle
operations. Wraps a `ConfigResult`.

```java
public final class ConfigException extends ZlinkException {
    public ConfigException(ConfigResult result);
    public ConfigException(ConfigResult result, int internalErrno);
    public ConfigResult getResult();
}
```

### SubmitResult

Result code for send/request/reply/publish operations.
Maps 1-to-1 to the C API `zlink_submit_result_t`.

```java
public enum SubmitResult {
    OK(0),
    BACKPRESSURED(1),
    NOT_CONNECTED(2),
    NOT_FOUND(3),
    TERMINATED(4),
    INVALID_HANDLE(5),
    INVALID_ARGUMENT(6),
    NOT_SUPPORTED(7),
    INVALID_STATE(8),
    THREAD_VIOLATION(9),
    OUT_OF_MEMORY(10),
    SEQ_EXHAUSTED(11),
    INTERNAL_ERROR(12),
    NOT_ADMITTED(13);  // target peer has weight 0

    SubmitResult(int value);
    public int value();
    public static SubmitResult fromValue(int value);
}
```

### RequestResult

Result code delivered to request completion callbacks and futures.

```java
public enum RequestResult {
    OK(0),
    TIMED_OUT(101),
    NOT_FOUND(102),
    TERMINATED(103),
    PROTOCOL_ERROR(104);

    RequestResult(int value);
    public int value();
    public static RequestResult fromValue(int value);
}
```

### RecvResult

Result code for recv, subscribe, and subscription event operations.

```java
public enum RecvResult {
    OK(0),
    NO_DATA(201),
    BUSY(202),
    TERMINATED(203),
    INVALID_HANDLE(204),
    NOT_SUPPORTED(205);

    RecvResult(int value);
    public int value();
    public static RecvResult fromValue(int value);
}
```

### HandlerResult

Result code for handler registration operations (`onPacket`,
`onSendReady`, `onRoutedReceive`, `onDispatchEvent`, `onEvent`, etc.).

```java
public enum HandlerResult {
    OK(0),
    INVALID_ARGUMENT(301),
    BUSY(302),
    NOT_SUPPORTED(303),
    DEADLOCK(304),
    INVALID_HANDLE(305);

    HandlerResult(int value);
    public int value();
    public static HandlerResult fromValue(int value);
}
```

### CloseResult

Result code for close and destroy operations.

```java
public enum CloseResult {
    OK(0),
    BUSY(401),
    SHUTDOWN(402),
    INVALID_HANDLE(403);

    CloseResult(int value);
    public int value();
    public static CloseResult fromValue(int value);
}
```

### BindResult

Result code for bind operations.

```java
public enum BindResult {
    OK(0),
    INVALID_ARGUMENT(501),
    ADDR_IN_USE(502),
    NOT_SUPPORTED(503),
    INVALID_HANDLE(504);

    BindResult(int value);
    public int value();
    public static BindResult fromValue(int value);
}
```

### ConnectResult

Result code for connect, disconnect, and unbind operations.

```java
public enum ConnectResult {
    OK(0),
    INVALID_ARGUMENT(601),
    NOT_SUPPORTED(602),
    INVALID_HANDLE(603);

    ConnectResult(int value);
    public int value();
    public static ConnectResult fromValue(int value);
}
```

### ConfigResult

Result code for configuration, option, and snapshot operations.

```java
public enum ConfigResult {
    OK(0),
    INVALID_HANDLE(701),
    INVALID_ARGUMENT(702),
    NOT_SUPPORTED(703);

    ConfigResult(int value);
    public int value();
    public static ConfigResult fromValue(int value);
}
```

### Received

Aggregates one recv result with optional routing id, optional request
sequence, and message parts. Implements `AutoCloseable`.

```java
public final class Received implements AutoCloseable {
    Optional<RoutingId> routingId();               // peer_rid (Router) / source_node_rid (Spot)
    Optional<RoutingId> spotRid();                 // SPOT routed recv 에서만 값 있음
    Optional<Long> requestSeq();                   // 값이 있으면 request, 없으면 ordinary message
    List<Message> parts();
    boolean isSinglePart();
    Message firstPart();                                             // @throws RecvException
    Message singlePartOrThrow();                                     // @throws RecvException

    // reply — requestSeq 가 있을 때만 유효. 없거나 invalid reply context 이면 SubmitException.
    void reply(Message part);                                        // @throws SubmitException
    void reply(Message part, SendFlags flags);                       // @throws SubmitException
    void reply(List<Message> parts);                                 // @throws SubmitException
    void reply(List<Message> parts, SendFlags flags);                // @throws SubmitException

    void close();                                                    // @throws CloseException
}
```

Received 는 내부적으로 source socket 참조를 보유한다 (binding 이 recv /
handler 에서 만들 때 주입). `reply()` 호출 시 `routingId` + `spotRid` +
`requestSeq` 를 캡슐화해 원래 socket 으로 전달. socket 이 close 된 후
`reply()` 호출하면 `SubmitException(ZLINK_SUBMIT_TERMINATED)`.

### TopicMessage

Topic-aware recv result used by SUB and Spot subscribe paths.
Implements `AutoCloseable`.

```java
public final class TopicMessage implements AutoCloseable {
    TopicMessage(RoutingId routingId, String serviceName, String topic, Message[] parts);

    Optional<RoutingId> routingId();
    Optional<String> serviceName();          // empty for raw SUB / XSUB
    String topic();                          // UTF-8
    List<Message> parts();
    boolean isSinglePart();
    Message firstPart();
    Message singlePartOrThrow();

    void close();                                                    // @throws CloseException
}
```

### SubscriptionEvent

Reports a subscribe/unsubscribe event from XPub sockets and Spot
subscription event recv. Immutable value object; no lifecycle methods.

```java
public record SubscriptionEvent(Optional<RoutingId> routingId,
                                Optional<String> serviceName,
                                String topic,           // UTF-8
                                boolean subscribed) {}
```

---

## Monitoring

### MonitorSocket

Socket-level event monitor. Receives connect, disconnect, and handshake events.
Implements `AutoCloseable`.
Starts in recv model. `onEvent(...)` transitions one-way to callback-only
model; after that `recv()` raises busy and `snapshot()` still works.

```java
public final class MonitorSocket implements AutoCloseable {
    /** No-op callback for callback-only model. Pass to
     *  {@link #onEvent(SocketMonitorHandler)} to keep a valid handler symbol
     *  when the application does not care about events; once installed, the
     *  monitor is in callback-only model and {@link #recv()} raises busy
     *  ({@link #snapshot()} still works). To drive the monitor through
     *  {@code snapshot()} / {@code recv()} instead, leave the handler unset.
     *  Maps to zlink_monitor_ignore_handler. */
    public static final SocketMonitorHandler IGNORE_HANDLER = event -> {};

    void onEvent(SocketMonitorHandler handler);                      // @throws HandlerException
    MonitorEvent recv();                                             // @throws RecvException
    MonitorSnapshot snapshot();                                      // @throws ConfigException

    void close();                                                    // @throws CloseException
}
```

### ServiceMonitor

Service-level event monitor for discovery.
Implements `AutoCloseable`.
Starts in recv model. `onEvent(...)` transitions one-way to callback-only
model; after that `recv()` raises busy and `snapshot()` still works.

```java
public final class ServiceMonitor implements AutoCloseable {
    void onEvent(ServiceMonitorHandler handler);                    // @throws HandlerException
    ServiceEvent recv();                                             // @throws RecvException
    MonitorSnapshot snapshot();                                      // @throws ConfigException

    void close();                                                    // @throws CloseException
}
```

### MonitorEvent

Socket monitor event value object. Produced by `MonitorSocket.recv()`.

```java
public record MonitorEvent(MonitorEventType event,
                           long value,
                           Optional<RoutingId> routingId,
                           String localAddr,
                           String remoteAddr) {}
```

`MonitorEventType` includes `PEER_WEIGHT_CHANGED` (bit 15). When this
event fires, `value` carries the new `0..100` weight for the peer. Service
monitors surface the same change through
`ServiceMonitorEventMask.PEER_WEIGHT_CHANGED` (bit 8).

### MonitorSnapshot

Runtime state snapshot produced by `MonitorSocket.snapshot()` and
`ServiceMonitor.snapshot()`. Immutable value object.

```java
public record MonitorSnapshot(MonitorSourceKind sourceKind,
                              int stateFlags,
                              int detailFlags,
                              long sndPendingMsgs,
                              long rcvPendingMsgs,
                              boolean autoHwmEnabled,
                              int autoHwmRole,
                              int autoHwmScope,
                              int autoHwmScopeCount,
                              int autoHwmManagedConnections,
                              int autoHwmActiveHwmConnections,
                              int autoHwmPlanningTransportConnections,
                              int autoHwmBaseFloorPerConnection,
                              int autoHwmAppliedSndhwm,
                              int autoHwmAppliedRcvhwm,
                              int autoHwmRequestedSndbuf,
                              int autoHwmRequestedRcvbuf,
                              int autoHwmEffectiveSndbuf,
                              int autoHwmEffectiveRcvbuf,
                              long autoHwmTotalMemoryBudgetBytes,
                              long autoHwmQueueBudgetBytes,
                              long autoHwmTransportBudgetBytes,
                              long autoHwmRuntimeReserveBytes,
                              long autoHwmGroupBudgetBytes,
                              long autoHwmRoleGroupBudgetBytes,
                              long autoHwmScopeGroupBudgetBytes,
                              long autoHwmGroupMessageSlots,
                              long autoHwmEffectiveMessageBytes,
                              long autoHwmAutoBufferBytes,
                              long autoHwmManualBufferBytes,
                              int autoHwmBufferConnections,
                              long autoHwmControlBudgetBytes,
                              long autoHwmRoutedBudgetBytes,
                              long autoHwmFanoutBudgetBytes,
                              long autoHwmRecvIngressBudgetBytes,
                              int autoHwmControlActiveConnections,
                              int autoHwmRoutedActiveConnections,
                              int autoHwmFanoutActiveConnections,
                              int autoHwmRecvIngressActiveConnections,
                              long autoHwmEstimatedMaxMemoryBytes,
                              long autoHwmLastRecalcMs,
                              int autoHwmLastRecalcReason,
                              int autoHwmDeferredSndhwm,
                              int autoHwmDeferredRcvhwm,
                              int autoHwmSendBlockedRatioPpm) {
    // Raw socket monitor source에서만 ready 의미를 사용한다.
    boolean isReady();
}
```

### ServiceEvent

Discovery service monitor event value object. Produced by
`ServiceMonitor.recv()`.

```java
public record ServiceEvent(ServiceKind serviceKind,
                           ServiceEventType eventType,
                           int status,
                           int errorCode,
                           long value,
                           int detailFlags,
                           String serviceName,
                           String endpoint,
                           Optional<RoutingId> routingId,
                           String subject,
                           ServiceEventSubjectKind subjectKind) {}
```

---

## Services

### Registry

Registry service node. Manages service topology and membership broadcast.
Implements `AutoCloseable`.

```java
public final class Registry implements AutoCloseable {
    Registry(Context ctx);

    void bind(String pubEndpoint, String routerEndpoint);            // @throws BindException
    void setId(int id);                                              // @throws ConfigException
    void addPeer(String peerPubEndpoint);                            // @throws ConnectException
    void setHeartbeat(int intervalMs, int timeoutMs);                // @throws ConfigException
    void setBroadcastInterval(int intervalMs);                       // @throws ConfigException
    void setTlsServer(String certPem, String keyPem, boolean requireClientCert); // @throws ConfigException
    void setTlsClient(String caCertPem, String hostname, boolean trustSystem);   // @throws ConfigException

    RegistryStatus statusSnapshot();                                 // @throws ConfigException
    List<RegistryServiceSummaryEntry> serviceSummarySnapshot();      // @throws ConfigException
    List<RegistryServiceSummaryEntry> serviceSummarySnapshot(
        RegistryServiceSummaryFilter filter);                        // @throws ConfigException
    List<RegistryTopologyEntry> topologySnapshot();                  // @throws ConfigException
    List<RegistryTopologyEntry> topologyQuery(RegistryTopologyFilter filter); // @throws ConfigException
    List<MemberPeerEntry> memberPeers(ServiceType serviceType, String serviceName); // @throws ConfigException
    byte[] memberPeerMetadata(ServiceType serviceType, String serviceName,
                              ServiceRole serviceRole, String endpoint); // @throws ConfigException

    void close();                                                    // @throws CloseException
}
```

### Discovery

Fixed-service discovery view. Tracks one service type/name pair.
Implements `AutoCloseable`.

```java
public enum DiscoveryDealerPeerMode {
    ROUTER,
    DEALER
}

public final class Discovery implements AutoCloseable {
    Discovery(Context ctx, ServiceType serviceType, String serviceName);

    void connectRegistry(String registryEndpoint);                   // @throws ConnectException
    void setValue(long value);                                       // @throws ConfigException
    long getValue();                                                 // @throws ConfigException
    void setMetadata(byte[] metadata);                               // @throws ConfigException
    byte[] getMetadata();                                            // @throws ConfigException
    void setTlsClient(String caCertPem, String hostname, boolean trustSystem); // @throws ConfigException

    List<MemberPeerEntry> memberPeers();                             // @throws ConfigException
    byte[] memberPeerMetadata(ServiceRole serviceRole, String endpoint); // @throws ConfigException

    RoutingId resolveSpot(RoutingId spotRid);                        // @throws ConfigException
    void setDealerPeerMode(DiscoveryDealerPeerMode mode);            // @throws ConfigException

    ServiceMonitor monitorOpen();                                    // @throws ConfigException
    ServiceMonitor monitorOpen(ServiceMonitorEventMask... events);   // @throws ConfigException

    void close();                                                    // @throws CloseException
}
```

### SpotNode

Spot node lifecycle and topology facade.
Implements `AutoCloseable`.

```java
public final class SpotNode implements AutoCloseable {
    SpotNode(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connectPeer(String peerEndpoint);                           // @throws ConnectException
    void disconnectPeer(String peerEndpoint);                        // @throws ConnectException
    void attachDiscovery(Discovery discovery);                       // @throws ConfigException
    void attachChannelDealer(Discovery discovery, DealerSocket dealer); // @throws ConfigException
    void attachChannelDealerManual(String channelName, DealerSocket dealer); // @throws ConfigException
    void attachPubIngress(PubSocket pub);                            // @throws ConfigException
    void setTlsServer(String certPem, String keyPem, boolean requireClientCert); // @throws ConfigException
    void setTlsClient(String caCertPem, String hostname, boolean trustSystem);   // @throws ConfigException

    // --- identity / routing ---
    // SpotNode 의 logical address. zlink_set_routing_id(node, ...) /
    // zlink_get_routing_id(node, ...) 매핑.
    void setRoutingId(RoutingId rid);                                // @throws ConfigException
    RoutingId routingId();                                           // @throws ConfigException

    // --- factory: Spot 생성은 반드시 SpotNode 에서만 ---
    Spot createSpot();                                               // @throws ConfigException

    SpotNodeStatus statusSnapshot();                                 // @throws ConfigException
    List<SpotNodePeerEntry> peersSnapshot();                         // @throws ConfigException
    List<SpotNodePeerEntry> peersQuery(SpotNodePeerFilter filter);   // @throws ConfigException
    List<SpotNodeSubjectEntry> subjectsSnapshot();                   // @throws ConfigException
    List<SpotNodeSubjectEntry> subjectsSnapshot(SpotNodeSubjectFilter filter); // @throws ConfigException
    // close() cascades: 모든 live Spot 먼저 close 한 후 node 종료
    void close();                                                    // @throws CloseException
}
```

`SpotNode` 가 lifecycle 소유자. `Spot` 은 반드시 `SpotNode.createSpot()`
factory 로만 생성한다. 직접 `new Spot(node)` 호출은 internal 로 격하된다
(spec 상 public 생성자 아님).

### Spot

Spot messaging endpoint. Provides service-aware pub/sub and routed messaging.
Implements `AutoCloseable`. **`SpotNode.createSpot()` 로만 생성**.

```java
public final class Spot implements AutoCloseable {
    // Spot(SpotNode node) 는 internal. public 생성은 SpotNode.createSpot() 사용.

    // --- identity / routing ---
    // Spot 의 logical address / routed ownership key.
    // zlink_set_routing_id(spot, ...) / zlink_get_routing_id(spot, ...) 매핑.
    void setRoutingId(RoutingId rid);                                // @throws ConfigException
    RoutingId routingId();                                           // @throws ConfigException

    // --- channel-aware publish / request ---
    boolean publish(String serviceName, String topicId, Message part);                   // @throws SubmitException
    boolean publish(String serviceName, String topicId, Message part, SendFlags flags);  // @throws SubmitException
    boolean publish(String serviceName, String topicId, List<Message> parts);            // @throws SubmitException
    boolean publish(String serviceName, String topicId, List<Message> parts, SendFlags flags); // @throws SubmitException
    boolean sendChannel(String channelName, Message part);                               // @throws SubmitException
    boolean sendChannel(String channelName, Message part, SendFlags flags);              // @throws SubmitException
    boolean sendChannel(String channelName, List<Message> parts);                        // @throws SubmitException
    boolean sendChannel(String channelName, List<Message> parts, SendFlags flags);       // @throws SubmitException
    CompletableFuture<List<Message>> requestChannel(String channelName, Message part);   // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> requestChannel(String channelName, List<Message> parts); // @throws SubmitException; future completes with RequestException on failure
    boolean requestChannel(String channelName, Message part,
                           BiConsumer<RequestResult, List<Message>> callback);           // @throws SubmitException; callback receives RequestResult
    boolean requestChannel(String channelName, Message part,
                           BiConsumer<RequestResult, List<Message>> callback,
                           Duration timeout);                                            // @throws SubmitException; callback receives RequestResult
    boolean requestChannel(String channelName, Message part,
                           BiConsumer<RequestResult, List<Message>> callback,
                           SendFlags flags);                                             // @throws SubmitException; false only on temporary backpressure
    boolean requestChannel(String channelName, Message part,
                           BiConsumer<RequestResult, List<Message>> callback,
                           SendFlags flags,
                           Duration timeout);                                            // @throws SubmitException; false only on temporary backpressure
    boolean requestChannel(String channelName, List<Message> parts,
                           BiConsumer<RequestResult, List<Message>> callback);           // @throws SubmitException; callback receives RequestResult
    boolean requestChannel(String channelName, List<Message> parts,
                           BiConsumer<RequestResult, List<Message>> callback,
                           Duration timeout);                                            // @throws SubmitException; callback receives RequestResult
    boolean requestChannel(String channelName, List<Message> parts,
                           BiConsumer<RequestResult, List<Message>> callback,
                           SendFlags flags);                                             // @throws SubmitException; false only on temporary backpressure
    boolean requestChannel(String channelName, List<Message> parts,
                           BiConsumer<RequestResult, List<Message>> callback,
                           SendFlags flags,
                           Duration timeout);                                            // @throws SubmitException; false only on temporary backpressure

    // --- subscribe ---
    void setSubscription(String topicId);                            // @throws ConfigException
    void unsetSubscription(String topicIdOrPattern);                 // @throws ConfigException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException
    TopicMessage subscribe();                                        // @throws RecvException
    @Nullable TopicMessage subscribe(RecvFlags flags);               // @throws RecvException
    SubscriptionEvent receiveSubscriptionEvent();                    // @throws RecvException
    SubscriptionEvent receiveSubscriptionEvent(RecvFlags flags);     // @throws RecvException

    // --- routed request (spot -> spot, async, no flags) ---
    CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              Message part);                             // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              Message part, Duration timeout);           // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              List<Message> parts);                      // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              List<Message> parts, Duration timeout);    // @throws SubmitException; future completes with RequestException on failure

    // --- routed request (spot -> spot, callback submit) ---
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          Message part,
                          BiConsumer<RequestResult, List<Message>> callback);            // @throws SubmitException; callback receives RequestResult
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          Message part,
                          BiConsumer<RequestResult, List<Message>> callback,
                          Duration timeout);                                             // @throws SubmitException; callback receives RequestResult
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          Message part,
                          BiConsumer<RequestResult, List<Message>> callback,
                          SendFlags flags);                                              // @throws SubmitException; false only on temporary backpressure
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          Message part,
                          BiConsumer<RequestResult, List<Message>> callback,
                          SendFlags flags, Duration timeout);                            // @throws SubmitException; false only on temporary backpressure
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          List<Message> parts,
                          BiConsumer<RequestResult, List<Message>> callback);            // @throws SubmitException; callback receives RequestResult
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          List<Message> parts,
                          BiConsumer<RequestResult, List<Message>> callback,
                          Duration timeout);                                             // @throws SubmitException; callback receives RequestResult
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          List<Message> parts,
                          BiConsumer<RequestResult, List<Message>> callback,
                          SendFlags flags);                                              // @throws SubmitException; false only on temporary backpressure
    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          List<Message> parts,
                          BiConsumer<RequestResult, List<Message>> callback,
                          SendFlags flags, Duration timeout);                            // @throws SubmitException; false only on temporary backpressure

    // --- routed request (spot -> router, async, no flags) ---
    CompletableFuture<List<Message>> requestToRouter(RoutingId peerRid,
                                              Message part);                             // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> requestToRouter(RoutingId peerRid,
                                              Message part, Duration timeout);           // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> requestToRouter(RoutingId peerRid,
                                              List<Message> parts);                      // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<List<Message>> requestToRouter(RoutingId peerRid,
                                              List<Message> parts, Duration timeout);    // @throws SubmitException; future completes with RequestException on failure

    // --- routed request (spot -> router, callback submit) ---
    boolean requestToRouter(RoutingId peerRid,
                            Message part,
                            BiConsumer<RequestResult, List<Message>> callback);          // @throws SubmitException; callback receives RequestResult
    boolean requestToRouter(RoutingId peerRid,
                            Message part,
                            BiConsumer<RequestResult, List<Message>> callback,
                            Duration timeout);                                           // @throws SubmitException; callback receives RequestResult
    boolean requestToRouter(RoutingId peerRid,
                            Message part,
                            BiConsumer<RequestResult, List<Message>> callback,
                            SendFlags flags);                                            // @throws SubmitException; false only on temporary backpressure
    boolean requestToRouter(RoutingId peerRid,
                            Message part,
                            BiConsumer<RequestResult, List<Message>> callback,
                            SendFlags flags, Duration timeout);                          // @throws SubmitException; false only on temporary backpressure
    boolean requestToRouter(RoutingId peerRid,
                            List<Message> parts,
                            BiConsumer<RequestResult, List<Message>> callback);          // @throws SubmitException; callback receives RequestResult
    boolean requestToRouter(RoutingId peerRid,
                            List<Message> parts,
                            BiConsumer<RequestResult, List<Message>> callback,
                            Duration timeout);                                           // @throws SubmitException; callback receives RequestResult
    boolean requestToRouter(RoutingId peerRid,
                            List<Message> parts,
                            BiConsumer<RequestResult, List<Message>> callback,
                            SendFlags flags);                                            // @throws SubmitException; false only on temporary backpressure
    boolean requestToRouter(RoutingId peerRid,
                            List<Message> parts,
                            BiConsumer<RequestResult, List<Message>> callback,
                            SendFlags flags, Duration timeout);                          // @throws SubmitException; false only on temporary backpressure

    // --- routed reply (spot -> spot) ---
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, Message message);                                  // @throws SubmitException
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, Message message, SendFlags flags);                 // @throws SubmitException
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts);                              // @throws SubmitException
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts, SendFlags flags);             // @throws SubmitException

    // --- routed reply (spot -> router) ---
    void replyToRouter(RoutingId peerRid, long requestSeq, Message message);                  // @throws SubmitException
    void replyToRouter(RoutingId peerRid, long requestSeq, Message message, SendFlags flags); // @throws SubmitException
    void replyToRouter(RoutingId peerRid, long requestSeq, List<Message> parts);              // @throws SubmitException
    void replyToRouter(RoutingId peerRid, long requestSeq, List<Message> parts,
                       SendFlags flags);                                                      // @throws SubmitException

    // --- routed receive ---
    Received recvRouted();                                           // @throws RecvException
    Received recvRouted(RecvFlags flags);                            // @throws RecvException
    void onRoutedReceive(SpotRoutedHandler handler);                 // @throws HandlerException
    void onDispatchEvent(SpotDispatchEventHandler handler);          // @throws HandlerException
    void drainChannelReplyFrom(MemorySegment dealerSubject);         // @throws ConfigException

    void close();                                                    // @throws CloseException
}
```

`onDispatchEvent` delivers `SpotDispatchInfo`. `CHANNEL_REPLY_READABLE`
dispatches identify the attached DEALER source through
`SpotDispatchInfo.subjectKind()` and `SpotDispatchInfo.subject()`. Read the
logical channel name from the attached DEALER with `getChannelName()`.
For `SUBSCRIBE_READABLE` and `ROUTED_READABLE`, callers must keep draining
`subscribe(...)` / `recvRouted(...)` until the binding surfaces no data /
`EAGAIN`.

### RegistryQueryClient

Remote registry query client. Connects to a registry and fetches topology snapshots.
Implements `AutoCloseable`.

```java
public final class RegistryQueryClient implements AutoCloseable {
    RegistryQueryClient(Context ctx);

    void connect(String endpoint);                                   // @throws ConnectException
    List<RegistryTopologyEntry> snapshot();                          // @throws ConfigException
    List<RegistryTopologyEntry> snapshot(RegistryTopologyFilter filter); // @throws ConfigException

    void close();                                                    // @throws CloseException
}
```

Primary entry types used in the default service flow:

### MemberPeerEntry

Discovery / Registry member peer entry value object.

```java
public record MemberPeerEntry(ServiceType serviceType,
                              ServiceRole serviceRole,
                              String serviceName,
                              String endpoint,
                              RoutingId routingId,
                              long value,
                              int weight) {}
```

### RegistryTopologyEntry

Registry topology entry value object. Returned by
`Registry.topologySnapshot` / `topologyQuery` and
`RegistryQueryClient.snapshot`.

```java
public record RegistryTopologyEntry(RoutingId routingId,
                                    ServiceKind serviceKind,
                                    ServiceRole serviceRole,
                                    String serviceName,
                                    String endpoint,
                                    TopologySource source,
                                    TopologyState state,
                                    int desiredCount,
                                    int readyCount,
                                    int errorCode,
                                    long lastReportedMs) {}
```

### SpotNodeStatus

Spot node status snapshot returned by `SpotNode.statusSnapshot`.

```java
public record SpotNodeStatus(String serviceName,
                             String localEndpoint,
                             RoutingId nodeRoutingId,
                             SpotNodeState state,
                             int configuredPeerCount,
                             int activePeerCount,
                             int connectedPeerCount,
                             int subjectCount,
                             int readySubjectCount,
                             int lastError,
                             long lastChangedMs) {}
```

### SpotDispatchEvent

```java
public enum SpotDispatchEvent {
    SUBSCRIBE_READABLE,
    ROUTED_READABLE,
    TIMER_READABLE,
    CHANNEL_REPLY_READABLE
}
```

### SpotDispatchSubjectKind

```java
public enum SpotDispatchSubjectKind {
    SPOT,
    TIMER,
    CHANNEL_DEALER
}
```

### SpotDispatchInfo

```java
public record SpotDispatchInfo(SpotDispatchEvent event,
                               SpotDispatchSubjectKind subjectKind,
                               MemorySegment subject) {}
```

Advanced / Diagnostic entry types and filters:

### RegistryServiceSummaryEntry

Registry service summary entry value object. Returned by
`Registry.serviceSummarySnapshot`.

```java
public record RegistryServiceSummaryEntry(ServiceKind serviceKind,
                                          ServiceRole serviceRole,
                                          String serviceName,
                                          int totalCount,
                                          int connectingCount,
                                          int readyCount,
                                          int errorCount,
                                          int stoppedCount,
                                          long lastReportedMs) {}
```

### RegistryStatus

Registry status snapshot returned by `Registry.statusSnapshot`.

```java
public record RegistryStatus(int registryId,
                             String bindEndpoint,
                             RegistryState state,
                             int topologyEntryCount,
                             int peerRegistryCount,
                             int connectedPeerRegistryCount,
                             long listSeq,
                             int lastError,
                             long lastChangedMs) {}
```

### SpotNodePeerEntry

Spot node peer entry value object. Returned by
`SpotNode.peersSnapshot` / `peersQuery`.

```java
public record SpotNodePeerEntry(String serviceName,
                                String localEndpoint,
                                String peerEndpoint,
                                SpotPeerSource source,
                                SpotPeerState state,
                                int weight,
                                long connectedSinceMs,
                                long lastChangedMs) {}
```

### SpotNodeSubjectEntry

Spot node subject entry value object. Returned by
`SpotNode.subjectsSnapshot`.

```java
public record SpotNodeSubjectEntry(SpotRole role,
                                   String subject,
                                   ServiceEventSubjectKind subjectKind,
                                   int readyPeerCount,
                                   int activePeerCount,
                                   long lastChangedMs) {}


```

### SpotNodePeerFilter

Filter for `SpotNode.peersQuery`.

```java
public record SpotNodePeerFilter(String peerEndpoint,
                                 SpotPeerSource source,
                                 SpotPeerState state) {}
```

### SpotNodeSubjectFilter

Filter for `SpotNode.subjectsSnapshot`.

```java
public record SpotNodeSubjectFilter(SpotRole role,
                                    String subject,
                                    ServiceEventSubjectKind subjectKind) {}
```

### RegistryServiceSummaryFilter

Filter for `Registry.serviceSummarySnapshot`.

```java
public record RegistryServiceSummaryFilter(ServiceKind serviceKind,
                                           ServiceRole serviceRole,
                                           String serviceName) {}
```

### RegistryTopologyFilter

Filter for `Registry.topologyQuery` and `RegistryQueryClient.snapshot`.

```java
public record RegistryTopologyFilter(ServiceKind serviceKind,
                                     ServiceRole serviceRole,
                                     String serviceName,
                                     RoutingId routingId,
                                     TopologyState state,
                                     TopologySource source) {}
```

---

## Poller

### Poller

Event poller for multiplexing socket and file descriptor readiness.

The current public poller contract is still generic. It does not yet expose a
Spot-aware result carrying owner `Spot`, dispatch event kind, and drain
subject together.
Implements `AutoCloseable`.

```java
public final class Poller implements AutoCloseable {
    Poller();

    // --- socket registration ---
    void add(Socket socket, int events);                             // @throws ConfigException
    void add(Socket socket, int events, Object tag);                 // @throws ConfigException
    void add(Socket socket, PollEventType... events);                // @throws ConfigException
    void add(Socket socket, Object tag, PollEventType... events);    // @throws ConfigException
    void modify(Socket socket, int events);                          // @throws ConfigException
    void modify(Socket socket, PollEventType... events);             // @throws ConfigException
    boolean remove(Socket socket);                                   // @throws ConfigException

    // --- file descriptor registration ---
    void addFd(int fd, int events);                                  // @throws ConfigException
    void addFd(int fd, int events, Object tag);                      // @throws ConfigException
    void addFd(int fd, PollEventType... events);                     // @throws ConfigException
    void addFd(int fd, Object tag, PollEventType... events);         // @throws ConfigException
    void modifyFd(int fd, int events);                               // @throws ConfigException
    void modifyFd(int fd, PollEventType... events);                  // @throws ConfigException
    boolean removeFd(int fd);                                        // @throws ConfigException

    // --- poll ---
    int size();                                                      // @throws ConfigException
    int pollCount(int timeoutMs);                                    // @throws ConfigException
    boolean pollAny(int timeoutMs);                                  // @throws ConfigException
    List<PollEvent> poll(int timeoutMs);                             // @throws ConfigException

    // --- ready accessors ---
    int readyCount();
    Socket readySocket(int index);
    Object readyTag(int index);
    int readyFd(int index);
    int readyEvents(int index);
    short readyRevents(int index);

    void clear();                                                    // @throws ConfigException
    void close();                                                    // @throws CloseException
}
```

---

## Timer

### Timer

Interval timer with optional spot integration.
Implements `AutoCloseable`.

```java
public final class Timer implements AutoCloseable {
    Timer();

    static Timer fromSpot(Spot spot);                                // @throws ConfigException

    void start(long intervalNs, long repeatCount);                   // @throws ConfigException
    void stop();                                                     // @throws ConfigException
    long recv();                                                     // @throws RecvException
    long recv(int flags);                                            // @throws RecvException
    void onFire(TimerHandler handler);                               // @throws HandlerException

    void close();                                                    // @throws CloseException
}
```

### TimerHandler

```java
@FunctionalInterface
public interface TimerHandler {
    void onFire(Timer timer, long fireCount);
}
```

---

## Utilities

### Zlink

Static utility class for global library operations.

```java
public final class Zlink {
    private Zlink() {}

    // Zlink.errno() is NOT public. Access internal errno through
    // ZlinkException.getInternalErrno() on the caught exception.

    /// Return a human-readable string for the given error number.
    static String strerror(int errnum);

    /// Return the runtime library version as [major, minor, patch].
    static int[] version();

    /// Check if the library supports a given capability (e.g. "ipc", "tls").
    static boolean has(String capability);

    /// Start a built-in proxy between frontend and backend sockets.
    /// An optional capture socket receives copies of all messages.
    static void proxy(Socket frontend, Socket backend, Socket capture);         // @throws ConfigException

    /// Start a steerable proxy with an additional control socket.
    static void proxySteerable(Socket frontend, Socket backend,
                               Socket capture, Socket control);                 // @throws ConfigException

    /// Sleep for the given number of seconds.
    static void sleep(int seconds);

    /// Close all parts in a multipart message array.
    static void multipartClose(Message[] parts);                                // @throws CloseException
}
```

### Stopwatch

High-resolution stopwatch. Implements `AutoCloseable`.

```java
public final class Stopwatch implements AutoCloseable {
    Stopwatch();

    /// Return elapsed microseconds without stopping.
    long intermediate();

    /// Stop the stopwatch and return total elapsed microseconds.
    long stop();

    void close();                                                    // @throws CloseException
}
```

### ZlinkThread

Background thread managed by the C library. Implements `AutoCloseable`.

```java
public final class ZlinkThread implements AutoCloseable {
    ZlinkThread(Runnable task);

    /// Wait for the thread to finish and release its handle.
    void join();                                                     // @throws ConfigException

    void close();                                                    // @throws CloseException
}
```

### AtomicCounter

Lock-free atomic counter. Implements `AutoCloseable`.

```java
public final class AtomicCounter implements AutoCloseable {
    AtomicCounter();

    void set(int value);
    int increment();
    int decrement();
    int value();

    void close();                                                    // @throws CloseException
}
```
