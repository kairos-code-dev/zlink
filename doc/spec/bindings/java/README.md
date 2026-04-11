[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Java Binding Specification

This document defines the complete public API surface of the Java binding.
Every class, its purpose, and all public method signatures are listed.
Internal helpers and implementation details are omitted.

All types live in the `dev.kairoscode.zlink` package.
Service types live in `dev.kairoscode.zlink.service.registry`,
`dev.kairoscode.zlink.service.discovery`, and
`dev.kairoscode.zlink.service.spot`.

---

## Core

### Context

RAII-style context that manages IO threads and sockets.
Implements `AutoCloseable`.

```java
public final class Context implements AutoCloseable {
    Context();

    ContextOptions options();
    void shutdown();
    void close();
}
```

### ContextOptions

Typed facade for context configuration options.

```java
public final class ContextOptions {
    int ioThreads();
    void ioThreads(int count);
    int maxSockets();
    void maxSockets(int count);
    int socketLimit();
    int threadPriority();
    void threadPriority(int priority);
    int threadSchedulingPolicy();
    void threadSchedulingPolicy(int policy);
    int maxMsgSize();
    void maxMsgSize(int bytes);
    int msgTSize();
    boolean blocky();
    void blocky(boolean enabled);
    void addThreadAffinity(int cpu);
    void removeThreadAffinity(int cpu);
}
```

---

## Socket Types

### PairSocket

Bidirectional exclusive pair socket.

```java
public final class PairSocket extends Socket {
    PairSocket(Context ctx);

    void bind(String endpoint);
    void connect(String endpoint);
    void unbind(String endpoint);
    void disconnect(String endpoint);

    void send(Message part);
    void send(List<Message> parts);
    SendResult trySend(Message part);
    SendResult trySend(List<Message> parts);

    Received recv();
    Optional<Received> tryRecv();
    void onReceive(SocketMessageHandler handler);
    void onSendReady(SendReadyHandler handler);
}
```

### PubSocket

Publisher socket. Sends topic-prefixed messages to all matching subscribers.

```java
public final class PubSocket extends Socket {
    PubSocket(Context ctx);

    void bind(String endpoint);
    void connect(String endpoint);
    void unbind(String endpoint);
    void disconnect(String endpoint);
    void attachDiscovery(Discovery discovery);

    void publish(String topicId, Message part);
    void publish(String topicId, List<Message> parts);
    SendResult tryPublish(String topicId, Message part);
    SendResult tryPublish(String topicId, List<Message> parts);
    void onSendReady(SendReadyHandler handler);

    PubSocketOptions options();
}
```

### SubSocket

Subscriber socket. Receives topic-filtered messages from publishers.

```java
public final class SubSocket extends Socket {
    SubSocket(Context ctx);

    void bind(String endpoint);
    void connect(String endpoint);
    void unbind(String endpoint);
    void disconnect(String endpoint);
    void attachDiscovery(Discovery discovery);

    void setSubscription(String filter);
    void unsetSubscription(String filter);
    TopicMessage subscribe();
    Optional<TopicMessage> trySubscribe();
    void onSubscribe(SubscribeHandler handler);

    SubSocketOptions options();
}
```

### DealerSocket

Asynchronous client socket for fair-queued request distribution.

```java
public final class DealerSocket extends Socket {
    DealerSocket(Context ctx);

    void bind(String endpoint);
    void connect(String endpoint);
    void unbind(String endpoint);
    void disconnect(String endpoint);
    void attachDiscovery(Discovery discovery);

    void setRoutingId(RoutingId rid);
    RoutingId routingId();

    void send(Message part);
    void send(List<Message> parts);
    SendResult trySend(Message part);
    SendResult trySend(List<Message> parts);

    Received recv();
    Optional<Received> tryRecv();
    void onReceive(SocketMessageHandler handler);
    void onSendReady(SendReadyHandler handler);

    DealerSocketOptions options();
}
```

### RouterSocket

Server socket that routes messages to specific peers by routing id.

```java
public final class RouterSocket extends Socket {
    RouterSocket(Context ctx);

    void bind(String endpoint);
    void connect(String endpoint);
    void unbind(String endpoint);
    void disconnect(String endpoint);
    void attachDiscovery(Discovery discovery);

    void setRoutingId(RoutingId rid);
    RoutingId routingId();

    void send(RoutingId rid, Message part);
    void send(RoutingId rid, List<Message> parts);
    SendResult trySend(RoutingId rid, Message part);
    SendResult trySend(RoutingId rid, List<Message> parts);

    Received recv();
    Optional<Received> tryRecv();
    void onReceive(SocketMessageHandler handler);
    void onSendReady(SendReadyHandler handler);

    // --- router → spot routed send ---
    void sendSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message part);
    void sendSpot(RoutingId destNodeRid, RoutingId destSpotRid, List<Message> parts);
    SendResult trySendSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message part);
    SendResult trySendSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                           List<Message> parts);

    // --- router → spot routed request (async) ---
    CompletableFuture<Received> requestSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                                            Message part, Duration timeout);
    CompletableFuture<Received> requestSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                                            List<Message> parts, Duration timeout);
    void requestSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     List<Message> parts, RequestReplyCallback callback,
                     Duration timeout);

    // --- router → spot routed reply ---
    void replySpot(RoutingId destNodeRid, RoutingId destSpotRid,
                   long requestSeq, Message message);
    void replySpot(RoutingId destNodeRid, RoutingId destSpotRid,
                   long requestSeq, List<Message> parts);

    // --- router spot receive ---
    Received recvSpot();
    Optional<Received> tryRecvSpot();
    void onSpotReceive(RouterSpotHandler handler);

    RouterSocketOptions options();
}
```

### XPubSocket

Extended publisher. Like PubSocket but also receives subscription events.

```java
public final class XPubSocket extends Socket {
    XPubSocket(Context ctx);

    void bind(String endpoint);
    void connect(String endpoint);
    void unbind(String endpoint);
    void disconnect(String endpoint);

    void publish(String topicId, Message part);
    void publish(String topicId, List<Message> parts);
    SendResult tryPublish(String topicId, Message part);
    SendResult tryPublish(String topicId, List<Message> parts);

    SubscriptionEvent receiveSubscriptionEvent();
    Optional<SubscriptionEvent> tryReceiveSubscriptionEvent();
    void onSendReady(SendReadyHandler handler);

    PubSocketOptions options();
}
```

### XSubSocket

Extended subscriber. Like SubSocket with raw subscription forwarding.

```java
public final class XSubSocket extends Socket {
    XSubSocket(Context ctx);

    void bind(String endpoint);
    void connect(String endpoint);
    void unbind(String endpoint);
    void disconnect(String endpoint);

    void setSubscription(String filter);
    void unsetSubscription(String filter);
    TopicMessage subscribe();
    Optional<TopicMessage> trySubscribe();
    void onSubscribe(SubscribeHandler handler);

    SubSocketOptions options();
}
```

### StreamSocket

Raw TCP stream socket. Bind-only; does not support `connect`.

```java
public final class StreamSocket extends Socket {
    StreamSocket(Context ctx);

    void bind(String endpoint);
    void unbind(String endpoint);

    void send(RoutingId rid, Message part);
    void send(RoutingId rid, List<Message> parts);
    SendResult trySend(RoutingId rid, Message part);
    SendResult trySend(RoutingId rid, List<Message> parts);

    Received recv();
    Optional<Received> tryRecv();
    void onReceive(SocketMessageHandler handler);
    void onSendReady(SendReadyHandler handler);

    void attachStreamRaw(StreamPacketHandler handler);
    void detachStream();

    StreamSocketOptions options();
}
```

---

## Message / Domain Types

### Message

Owns one native message frame. Implements `AutoCloseable`.

```java
public final class Message implements AutoCloseable {
    Message();
    Message(int size);

    // --- factories (copy) ---
    static Message copyOf(byte[] data);
    static Message copyOf(byte[] data, int offset, int length);
    static Message copyOfUtf8(String value);
    static Message copyOf(ByteBuffer data);
    static Message copyOf(ByteBuf buf);
    static Message copyOf(ByteSpan span);
    static Message copyOf(MemorySegment data);
    static Message copyOf(MemorySegment data, long offset, long length);

    // --- factories (zero-copy borrow) ---
    static Message wrapDirect(ByteBuffer data);
    static Message wrapDirect(ByteBuf buf);
    static Message wrapNative(MemorySegment data);
    static Message wrapNative(MemorySegment data, long offset, long length);
    static Message wrap(ByteSpan span);

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
    String getProperty(String key);

    // --- copy to destination ---
    int copyTo(byte[] destination);
    int copyTo(byte[] destination, int offset);
    int copyTo(ByteBuffer destination);
    int copyTo(ByteBuf destination);
    boolean tryCopyTo(ByteBuffer destination);

    // --- batch close ---
    static void closeAll(Message[] parts);
    static void closeAll(Iterable<? extends Message> parts);

    void close();
}
```

### RoutingId

Immutable binary-safe routing identity value object (max 255 bytes).

```java
public final class RoutingId {
    static final int MAX_LENGTH = 255;

    static RoutingId copyOf(byte[] value);
    static RoutingId copyOf(byte[] value, int offset, int length);

    byte[] toByteArray();
    ByteBuffer asReadOnlyBuffer();
    int size();
    boolean empty();

    boolean equals(Object other);
    int hashCode();
}
```

### Received

Aggregates one recv result with optional routing id and message parts.
Implements `AutoCloseable`.

```java
public final class Received implements AutoCloseable {
    Received(RoutingId routingId, Message[] parts);
    Received(RoutingId routingId, Message[] parts,
             long requestSequence, boolean hasRequestSequence);

    RoutingId routingId();
    boolean hasRoutingId();
    List<Message> parts();
    long requestSequence();
    boolean hasRequestSequence();
    boolean isSinglePart();
    Message firstPart();
    Message singlePartOrThrow();

    void close();
}
```

### TopicMessage

Topic-aware recv result used by SUB and Spot subscribe paths.
Implements `AutoCloseable`.

```java
public final class TopicMessage implements AutoCloseable {
    TopicMessage(RoutingId routingId, String topicId, Message[] parts);

    boolean hasRoutingId();
    RoutingId routingId();
    String topicId();
    List<Message> parts();
    boolean isSinglePart();
    Message firstPart();
    Message singlePartOrThrow();

    void close();
}
```

### SubscriptionEvent

Reports a subscribe/unsubscribe event from XPub sockets.

```java
public record SubscriptionEvent(RoutingId routingId, boolean subscribed,
                                String topic) {}
```

### SendResult

```java
public enum SendResult {
    SENT,
    BACKPRESSURED,
    NOT_CONNECTED,
    NOT_FOUND;

    int nativeValue();
    static SendResult fromNativeValue(int value);
}
```

---

## Request-Reply

### RequestRouter

Request-reply layer on top of a RouterSocket. Manages request correlation
and reply dispatch. Implements `AutoCloseable`.

```java
public final class RequestRouter implements AutoCloseable {
    RequestRouter(RouterSocket socket);

    RouterSocket socket();

    // --- request (async) ---
    CompletableFuture<Received> request(RoutingId routingId, Message part,
                                        Duration timeout);
    CompletableFuture<Received> request(RoutingId routingId, List<Message> parts,
                                        Duration timeout);

    // --- tryRequest (async) ---
    CompletableFuture<Received> tryRequest(RoutingId routingId, Message part,
                                           Duration timeout);

    // --- request (callback) ---
    void request(RoutingId routingId, List<Message> parts,
                 RequestReplyCallback callback, Duration timeout);

    // --- reply ---
    CompletableFuture<Void> reply(RoutingId routingId, long requestSeq, Message message);
    CompletableFuture<Void> reply(RoutingId routingId, long requestSeq,
                                  List<Message> parts);
    SendResult tryReply(RoutingId routingId, long requestSeq, Message message);

    // --- receive ---
    Received recv();
    Optional<Received> tryRecv();
    void onReceive(SocketMessageHandler handler);

    void close();
}
```

### RequestDealer

Request-reply layer on top of a DealerSocket. Implements `AutoCloseable`.

```java
public final class RequestDealer implements AutoCloseable {
    RequestDealer(DealerSocket socket);

    DealerSocket socket();

    // --- request (async) ---
    CompletableFuture<Received> request(Message part, Duration timeout);
    CompletableFuture<Received> request(List<Message> parts, Duration timeout);

    // --- tryRequest (async) ---
    CompletableFuture<Received> tryRequest(Message part, Duration timeout);
    CompletableFuture<Received> tryRequest(List<Message> parts, Duration timeout);

    // --- request (callback) ---
    void request(List<Message> parts, RequestReplyCallback callback,
                 Duration timeout);

    // --- receive ---
    Received recv();
    Optional<Received> tryRecv();
    void onReceive(SocketMessageHandler handler);

    void close();
}
```

---

## Monitoring

### MonitorSocket

Socket-level event monitor. Receives connect, disconnect, and handshake events.
Implements `AutoCloseable`.

```java
public final class MonitorSocket implements AutoCloseable {
    MonitorEvent recv();
    Optional<MonitorEvent> tryRecv();
    MonitorSnapshot snapshot();

    void close();
}
```

### ServiceMonitor

Service-level event monitor for discovery, registry, and spot.
Implements `AutoCloseable`.

```java
public final class ServiceMonitor implements AutoCloseable {
    void onEvent(ServiceMonitorHandler handler);
    ServiceEvent recv();
    Optional<ServiceEvent> tryRecv();
    MonitorSnapshot snapshot();

    void close();
}
```

---

## Services

### Registry

Registry service node. Manages service topology and membership broadcast.
Implements `AutoCloseable`.

```java
public final class Registry implements AutoCloseable {
    Registry(Context ctx);

    void bind(String pubEndpoint, String routerEndpoint);
    void setId(int id);
    void addPeer(String peerPubEndpoint);
    void setHeartbeat(int intervalMs, int timeoutMs);
    void setBroadcastInterval(int intervalMs);
    void setTlsServer(String certPem, String keyPem, boolean requireClientCert);
    void setTlsClient(String caCertPem, String hostname, boolean trustSystem);

    RegistryStatus statusSnapshot();
    List<RegistryServiceSummaryEntry> serviceSummarySnapshot();
    List<RegistryServiceSummaryEntry> serviceSummarySnapshot(
        RegistryServiceSummaryFilter filter);
    List<RegistryTopologyEntry> topologySnapshot();
    List<RegistryTopologyEntry> topologyQuery(RegistryTopologyFilter filter);
    List<MemberPeerEntry> memberPeers(ServiceType serviceType, String serviceName);
    byte[] memberPeerMetadata(ServiceType serviceType, String serviceName,
                              ServiceRole serviceRole, String endpoint);

    void close();
}
```

### Discovery

Fixed-service discovery view. Tracks one service type/name pair.
Implements `AutoCloseable`.

```java
public final class Discovery implements AutoCloseable {
    Discovery(Context ctx, ServiceType serviceType, String serviceName);

    void connectRegistry(String registryEndpoint);
    void setValue(long value);
    long getValue();
    void setMetadata(byte[] metadata);
    byte[] getMetadata();
    void setTlsClient(String caCertPem, String hostname, boolean trustSystem);

    List<MemberPeerEntry> memberPeers();
    byte[] memberPeerMetadata(ServiceRole serviceRole, String endpoint);

    ServiceMonitor monitorOpen();
    ServiceMonitor monitorOpen(ServiceMonitorEventMask... events);

    void close();
}
```

### SpotNode

Spot node lifecycle and topology facade.
Implements `AutoCloseable`.

```java
public final class SpotNode implements AutoCloseable {
    SpotNode(Context ctx);

    void bind(String endpoint);
    void connectPeer(String peerEndpoint);
    void disconnectPeer(String peerEndpoint);
    void attachDiscovery(Discovery discovery);
    void setTlsServer(String certPem, String keyPem, boolean requireClientCert);
    void setTlsClient(String caCertPem, String hostname, boolean trustSystem);

    SpotNodeStatus statusSnapshot();
    List<SpotNodePeerEntry> peersSnapshot();
    List<SpotNodePeerEntry> peersQuery(SpotNodePeerFilter filter);
    List<SpotNodeSubjectEntry> subjectsSnapshot();
    List<SpotNodeSubjectEntry> subjectsSnapshot(SpotNodeSubjectFilter filter);

    void close();
}
```

### Spot

Spot messaging endpoint. Provides pub/sub and subscription management.
Implements `AutoCloseable`.

```java
public final class Spot implements AutoCloseable {
    Spot(SpotNode node);

    // --- publish ---
    void publish(String topicId, Message part);
    SendResult tryPublish(String topicId, Message part);
    void publish(String topicId, List<Message> parts);
    SendResult tryPublish(String topicId, List<Message> parts);

    // --- subscribe ---
    void setSubscription(String topicId);
    void unsetSubscription(String topicIdOrPattern);
    void onSubscribe(SubscribeHandler handler);
    void onSendReady(SendReadyHandler handler);
    TopicMessage subscribe();
    Optional<TopicMessage> trySubscribe();

    // --- routed send (spot → spot) ---
    void sendSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message part);
    void sendSpot(RoutingId destNodeRid, RoutingId destSpotRid, List<Message> parts);
    SendResult trySendSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message part);
    SendResult trySendSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                           List<Message> parts);

    // --- routed request (spot → spot, async) ---
    CompletableFuture<Received> requestSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                                            Message part, Duration timeout);
    CompletableFuture<Received> requestSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                                            List<Message> parts, Duration timeout);
    void requestSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     List<Message> parts, RequestReplyCallback callback,
                     Duration timeout);

    // --- routed reply (spot → spot) ---
    void replySpot(RoutingId destNodeRid, RoutingId destSpotRid,
                   long requestSeq, Message message);
    void replySpot(RoutingId destNodeRid, RoutingId destSpotRid,
                   long requestSeq, List<Message> parts);

    // --- routed send (spot → router) ---
    void sendRouter(RoutingId peerRid, Message part);
    void sendRouter(RoutingId peerRid, List<Message> parts);
    SendResult trySendRouter(RoutingId peerRid, Message part);
    SendResult trySendRouter(RoutingId peerRid, List<Message> parts);

    // --- routed request (spot → router, async) ---
    CompletableFuture<Received> requestRouter(RoutingId peerRid, Message part,
                                              Duration timeout);
    CompletableFuture<Received> requestRouter(RoutingId peerRid, List<Message> parts,
                                              Duration timeout);
    void requestRouter(RoutingId peerRid, List<Message> parts,
                       RequestReplyCallback callback, Duration timeout);

    // --- routed reply (spot → router) ---
    void replyRouter(RoutingId peerRid, long requestSeq, Message message);
    void replyRouter(RoutingId peerRid, long requestSeq, List<Message> parts);

    // --- routed receive ---
    Received recvRouted();
    Optional<Received> tryRecvRouted();
    void onRoutedReceive(SpotRoutedHandler handler);
    void onDispatchEvent(SpotDispatchEventHandler handler);

    void close();
}
```

### RegistryQueryClient

Remote registry query client. Connects to a registry and fetches topology snapshots.
Implements `AutoCloseable`.

```java
public final class RegistryQueryClient implements AutoCloseable {
    RegistryQueryClient(Context ctx);

    void connect(String endpoint);
    List<RegistryTopologyEntry> snapshot();
    List<RegistryTopologyEntry> snapshot(RegistryTopologyFilter filter);

    void close();
}
```

---

## Poller

### Poller

Event poller for multiplexing socket and file descriptor readiness.
Implements `AutoCloseable`.

```java
public final class Poller implements AutoCloseable {
    Poller();

    // --- socket registration ---
    void add(Socket socket, int events);
    void add(Socket socket, int events, Object tag);
    void add(Socket socket, PollEventType... events);
    void add(Socket socket, Object tag, PollEventType... events);
    void modify(Socket socket, int events);
    void modify(Socket socket, PollEventType... events);
    boolean remove(Socket socket);

    // --- file descriptor registration ---
    void addFd(int fd, int events);
    void addFd(int fd, int events, Object tag);
    void addFd(int fd, PollEventType... events);
    void addFd(int fd, Object tag, PollEventType... events);
    void modifyFd(int fd, int events);
    void modifyFd(int fd, PollEventType... events);
    boolean removeFd(int fd);

    // --- poll ---
    int size();
    int pollCount(int timeoutMs);
    boolean pollAny(int timeoutMs);
    List<PollEvent> poll(int timeoutMs);

    // --- ready accessors ---
    int readyCount();
    Socket readySocket(int index);
    Object readyTag(int index);
    int readyFd(int index);
    int readyEvents(int index);
    short readyRevents(int index);

    void clear();
    void close();
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

    static Timer fromSpot(Spot spot);

    void start(long intervalNs, long repeatCount);
    void stop();
    long recv();
    long recv(int flags);
    void onFire(TimerHandler handler);

    void close();
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

    /// Return the errno for the current thread.
    static int errno();

    /// Return a human-readable string for the given error number.
    static String strerror(int errnum);

    /// Return the runtime library version as [major, minor, patch].
    static int[] version();

    /// Check if the library supports a given capability (e.g. "ipc", "tls").
    static boolean has(String capability);

    /// Start a built-in proxy between frontend and backend sockets.
    /// An optional capture socket receives copies of all messages.
    static void proxy(Socket frontend, Socket backend, Socket capture);

    /// Start a steerable proxy with an additional control socket.
    static void proxySteerable(Socket frontend, Socket backend,
                               Socket capture, Socket control);

    /// Sleep for the given number of seconds.
    static void sleep(int seconds);

    /// Close all parts in a multipart message array.
    static void multipartClose(Message[] parts);
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

    void close();
}
```

### ZlinkThread

Background thread managed by the C library. Implements `AutoCloseable`.

```java
public final class ZlinkThread implements AutoCloseable {
    ZlinkThread(Runnable task);

    /// Wait for the thread to finish and release its handle.
    void join();

    void close();
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

    void close();
}
```
