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
    void shutdown();                                                 // @throws CloseException
    void close();                                                    // @throws CloseException
}
```

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
    void addThreadAffinity(int cpu);                                 // @throws ConfigException
    void removeThreadAffinity(int cpu);                              // @throws ConfigException
}
```

---

## Socket Types

### PairSocket

Bidirectional exclusive pair socket.

```java
public final class PairSocket extends Socket {
    PairSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException

    void send(Message part);                                         // @throws SubmitException
    void send(Message part, SendFlags flags);                        // @throws SubmitException
    void send(List<Message> parts);                                  // @throws SubmitException
    void send(List<Message> parts, SendFlags flags);                 // @throws SubmitException

    Received recv();                                                 // @throws RecvException
    Received recv(RecvFlags flags);                                  // @throws RecvException
    void onReceive(SocketMessageHandler handler);                    // @throws HandlerException
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

    void publish(String topicId, Message part);                      // @throws SubmitException
    void publish(String topicId, Message part, SendFlags flags);     // @throws SubmitException
    void publish(String topicId, List<Message> parts);               // @throws SubmitException
    void publish(String topicId, List<Message> parts, SendFlags flags); // @throws SubmitException
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
    TopicMessage subscribe(RecvFlags flags);                         // @throws RecvException
    void onSubscribe(SubscribeHandler handler);                      // @throws HandlerException

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

    void setRoutingId(RoutingId rid);                                // @throws ConfigException
    RoutingId routingId();                                           // @throws ConfigException

    void send(Message part);                                         // @throws SubmitException
    void send(Message part, SendFlags flags);                        // @throws SubmitException
    void send(List<Message> parts);                                  // @throws SubmitException
    void send(List<Message> parts, SendFlags flags);                 // @throws SubmitException

    Received recv();                                                 // @throws RecvException
    Received recv(RecvFlags flags);                                  // @throws RecvException
    void onReceive(SocketMessageHandler handler);                    // @throws HandlerException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException

    // --- request (async, no flags) ---
    CompletableFuture<Received> request(Message part);                           // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> request(Message part, Duration timeout);         // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> request(List<Message> parts);                    // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> request(List<Message> parts, Duration timeout);  // @throws SubmitException; future completes with RequestException on failure

    // --- request (callback, has flags, throws on submit failure) ---
    void request(Message part,
                 BiConsumer<RequestResult, Received> callback);                         // @throws SubmitException; callback receives RequestResult
    void request(Message part,
                 BiConsumer<RequestResult, Received> callback, SendFlags flags);        // @throws SubmitException; callback receives RequestResult
    void request(Message part,
                 BiConsumer<RequestResult, Received> callback,
                 SendFlags flags, Duration timeout);                                    // @throws SubmitException; callback receives RequestResult
    void request(List<Message> parts,
                 BiConsumer<RequestResult, Received> callback);                         // @throws SubmitException; callback receives RequestResult
    void request(List<Message> parts,
                 BiConsumer<RequestResult, Received> callback, SendFlags flags);        // @throws SubmitException; callback receives RequestResult
    void request(List<Message> parts,
                 BiConsumer<RequestResult, Received> callback,
                 SendFlags flags, Duration timeout);                                    // @throws SubmitException; callback receives RequestResult

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

    void send(RoutingId rid, Message part);                          // @throws SubmitException
    void send(RoutingId rid, Message part, SendFlags flags);         // @throws SubmitException
    void send(RoutingId rid, List<Message> parts);                   // @throws SubmitException
    void send(RoutingId rid, List<Message> parts, SendFlags flags);  // @throws SubmitException

    Received recv();                                                 // @throws RecvException
    Received recv(RecvFlags flags);                                  // @throws RecvException
    void onReceive(SocketMessageHandler handler);                    // @throws HandlerException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException

    // --- request to a specific peer (async, no flags) ---
    CompletableFuture<Received> request(RoutingId rid, Message part);                          // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> request(RoutingId rid, Message part, Duration timeout);        // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> request(RoutingId rid, List<Message> parts);                   // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> request(RoutingId rid, List<Message> parts, Duration timeout); // @throws SubmitException; future completes with RequestException on failure

    // --- request to a specific peer (callback, has flags, throws on submit failure) ---
    void request(RoutingId rid, Message part,
                 BiConsumer<RequestResult, Received> callback);                         // @throws SubmitException; callback receives RequestResult
    void request(RoutingId rid, Message part,
                 BiConsumer<RequestResult, Received> callback, SendFlags flags);        // @throws SubmitException; callback receives RequestResult
    void request(RoutingId rid, Message part,
                 BiConsumer<RequestResult, Received> callback,
                 SendFlags flags, Duration timeout);                                    // @throws SubmitException; callback receives RequestResult
    void request(RoutingId rid, List<Message> parts,
                 BiConsumer<RequestResult, Received> callback);                         // @throws SubmitException; callback receives RequestResult
    void request(RoutingId rid, List<Message> parts,
                 BiConsumer<RequestResult, Received> callback, SendFlags flags);        // @throws SubmitException; callback receives RequestResult
    void request(RoutingId rid, List<Message> parts,
                 BiConsumer<RequestResult, Received> callback,
                 SendFlags flags, Duration timeout);                                    // @throws SubmitException; callback receives RequestResult

    // --- reply to a received request ---
    void reply(RoutingId rid, long requestSeq, Message message);                        // @throws SubmitException
    void reply(RoutingId rid, long requestSeq, Message message, SendFlags flags);       // @throws SubmitException
    void reply(RoutingId rid, long requestSeq, List<Message> parts);                    // @throws SubmitException
    void reply(RoutingId rid, long requestSeq, List<Message> parts, SendFlags flags);   // @throws SubmitException

    // --- router -> spot routed send ---
    void sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message part);         // @throws SubmitException
    void sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message part,
                    SendFlags flags);                                                    // @throws SubmitException
    void sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, List<Message> parts);  // @throws SubmitException
    void sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, List<Message> parts,
                    SendFlags flags);                                                    // @throws SubmitException

    // --- router -> spot routed request (async, no flags) ---
    CompletableFuture<Received> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              Message part);                             // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              Message part, Duration timeout);           // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              List<Message> parts);                      // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              List<Message> parts, Duration timeout);    // @throws SubmitException; future completes with RequestException on failure

    // --- router -> spot routed request (callback, has flags, throws on submit failure) ---
    void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       Message part,
                       BiConsumer<RequestResult, Received> callback);                    // @throws SubmitException; callback receives RequestResult
    void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       Message part,
                       BiConsumer<RequestResult, Received> callback,
                       SendFlags flags);                                                 // @throws SubmitException; callback receives RequestResult
    void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       Message part,
                       BiConsumer<RequestResult, Received> callback,
                       SendFlags flags, Duration timeout);                               // @throws SubmitException; callback receives RequestResult
    void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       List<Message> parts,
                       BiConsumer<RequestResult, Received> callback);                    // @throws SubmitException; callback receives RequestResult
    void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       List<Message> parts,
                       BiConsumer<RequestResult, Received> callback,
                       SendFlags flags);                                                 // @throws SubmitException; callback receives RequestResult
    void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       List<Message> parts,
                       BiConsumer<RequestResult, Received> callback,
                       SendFlags flags, Duration timeout);                               // @throws SubmitException; callback receives RequestResult

    // --- router -> spot routed reply ---
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, Message message);                                  // @throws SubmitException
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, Message message, SendFlags flags);                 // @throws SubmitException
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts);                              // @throws SubmitException
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts, SendFlags flags);             // @throws SubmitException

    // --- router spot receive ---
    Received recvSpot();                                             // @throws RecvException
    Received recvSpot(RecvFlags flags);                              // @throws RecvException
    void onSpotReceive(RouterSpotHandler handler);                   // @throws HandlerException

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

    void publish(String topicId, Message part);                      // @throws SubmitException
    void publish(String topicId, Message part, SendFlags flags);     // @throws SubmitException
    void publish(String topicId, List<Message> parts);               // @throws SubmitException
    void publish(String topicId, List<Message> parts, SendFlags flags); // @throws SubmitException

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
    TopicMessage subscribe(RecvFlags flags);                         // @throws RecvException
    void onSubscribe(SubscribeHandler handler);                      // @throws HandlerException

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

    void send(RoutingId rid, Message part);                          // @throws SubmitException
    void send(RoutingId rid, Message part, SendFlags flags);         // @throws SubmitException
    void send(RoutingId rid, List<Message> parts);                   // @throws SubmitException
    void send(RoutingId rid, List<Message> parts, SendFlags flags);  // @throws SubmitException

    Received recv();                                                 // @throws RecvException
    Received recv(RecvFlags flags);                                  // @throws RecvException
    void onReceive(SocketMessageHandler handler);                    // @throws HandlerException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException

    void attachStreamRaw(StreamPacketHandler handler);               // @throws ConfigException
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
    static Message copyOf(byte[] data);                              // @throws ConfigException
    static Message copyOf(byte[] data, int offset, int length);      // @throws ConfigException
    static Message copyOfUtf8(String value);                         // @throws ConfigException
    static Message copyOf(ByteBuffer data);                          // @throws ConfigException
    static Message copyOf(ByteBuf buf);                              // @throws ConfigException
    static Message copyOf(ByteSpan span);                            // @throws ConfigException
    static Message copyOf(MemorySegment data);                       // @throws ConfigException
    static Message copyOf(MemorySegment data, long offset, long length); // @throws ConfigException

    // --- factories (zero-copy borrow) ---
    static Message wrapDirect(ByteBuffer data);                      // @throws ConfigException
    static Message wrapDirect(ByteBuf buf);                          // @throws ConfigException
    static Message wrapNative(MemorySegment data);                   // @throws ConfigException
    static Message wrapNative(MemorySegment data, long offset, long length); // @throws ConfigException
    static Message wrap(ByteSpan span);                              // @throws ConfigException

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

    // --- copy to destination ---
    int copyTo(byte[] destination);
    int copyTo(byte[] destination, int offset);
    int copyTo(ByteBuffer destination);
    int copyTo(ByteBuf destination);
    boolean tryCopyTo(ByteBuffer destination);

    // --- batch close ---
    static void closeAll(Message[] parts);                           // @throws CloseException
    static void closeAll(Iterable<? extends Message> parts);         // @throws CloseException

    void close();                                                    // @throws CloseException
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

Thrown by handler registration methods (`onReceive`, `onSubscribe`,
`onSendReady`, `onSpotReceive`, etc.). Wraps a `HandlerResult`.

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
    INTERNAL_ERROR(12);

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

Result code for handler registration operations (onReceive, onSubscribe, etc.).

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

    void close();                                                    // @throws CloseException
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

    void close();                                                    // @throws CloseException
}
```

### SubscriptionEvent

Reports a subscribe/unsubscribe event from XPub sockets.

```java
public record SubscriptionEvent(RoutingId routingId, boolean subscribed,
                                String topic) {}
```

---

## Monitoring

### MonitorSocket

Socket-level event monitor. Receives connect, disconnect, and handshake events.
Implements `AutoCloseable`.

```java
public final class MonitorSocket implements AutoCloseable {
    MonitorEvent recv();                                             // @throws RecvException
    MonitorSnapshot snapshot();                                      // @throws ConfigException

    void close();                                                    // @throws CloseException
}
```

### ServiceMonitor

Service-level event monitor for discovery, registry, and spot.
Implements `AutoCloseable`.

```java
public final class ServiceMonitor implements AutoCloseable {
    void onEvent(ServiceMonitorHandler handler);                    // @throws HandlerException
    ServiceEvent recv();                                             // @throws RecvException
    MonitorSnapshot snapshot();                                      // @throws ConfigException

    void close();                                                    // @throws CloseException
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
    void setTlsServer(String certPem, String keyPem, boolean requireClientCert); // @throws ConfigException
    void setTlsClient(String caCertPem, String hostname, boolean trustSystem);   // @throws ConfigException

    SpotNodeStatus statusSnapshot();                                 // @throws ConfigException
    List<SpotNodePeerEntry> peersSnapshot();                         // @throws ConfigException
    List<SpotNodePeerEntry> peersQuery(SpotNodePeerFilter filter);   // @throws ConfigException
    List<SpotNodeSubjectEntry> subjectsSnapshot();                   // @throws ConfigException
    List<SpotNodeSubjectEntry> subjectsSnapshot(SpotNodeSubjectFilter filter); // @throws ConfigException

    void close();                                                    // @throws CloseException
}
```

### Spot

Spot messaging endpoint. Provides pub/sub and subscription management.
Implements `AutoCloseable`.

```java
public final class Spot implements AutoCloseable {
    Spot(SpotNode node);

    // --- publish ---
    void publish(String topicId, Message part);                      // @throws SubmitException
    void publish(String topicId, Message part, SendFlags flags);     // @throws SubmitException
    void publish(String topicId, List<Message> parts);               // @throws SubmitException
    void publish(String topicId, List<Message> parts, SendFlags flags); // @throws SubmitException

    // --- subscribe ---
    void setSubscription(String topicId);                            // @throws ConfigException
    void unsetSubscription(String topicIdOrPattern);                 // @throws ConfigException
    void onSubscribe(SubscribeHandler handler);                      // @throws HandlerException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException
    TopicMessage subscribe();                                        // @throws RecvException
    TopicMessage subscribe(RecvFlags flags);                         // @throws RecvException

    // --- routed send (spot -> spot) ---
    void sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message part);         // @throws SubmitException
    void sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message part,
                    SendFlags flags);                                                    // @throws SubmitException
    void sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, List<Message> parts);  // @throws SubmitException
    void sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, List<Message> parts,
                    SendFlags flags);                                                    // @throws SubmitException

    // --- routed request (spot -> spot, async, no flags) ---
    CompletableFuture<Received> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              Message part);                             // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              Message part, Duration timeout);           // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              List<Message> parts);                      // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> requestToSpot(RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              List<Message> parts, Duration timeout);    // @throws SubmitException; future completes with RequestException on failure

    // --- routed request (spot -> spot, callback, has flags, throws on submit failure) ---
    void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       Message part,
                       BiConsumer<RequestResult, Received> callback);                    // @throws SubmitException; callback receives RequestResult
    void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       Message part,
                       BiConsumer<RequestResult, Received> callback,
                       SendFlags flags);                                                 // @throws SubmitException; callback receives RequestResult
    void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       Message part,
                       BiConsumer<RequestResult, Received> callback,
                       SendFlags flags, Duration timeout);                               // @throws SubmitException; callback receives RequestResult
    void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       List<Message> parts,
                       BiConsumer<RequestResult, Received> callback);                    // @throws SubmitException; callback receives RequestResult
    void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       List<Message> parts,
                       BiConsumer<RequestResult, Received> callback,
                       SendFlags flags);                                                 // @throws SubmitException; callback receives RequestResult
    void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       List<Message> parts,
                       BiConsumer<RequestResult, Received> callback,
                       SendFlags flags, Duration timeout);                               // @throws SubmitException; callback receives RequestResult

    // --- routed reply (spot -> spot) ---
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, Message message);                                  // @throws SubmitException
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, Message message, SendFlags flags);                 // @throws SubmitException
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts);                              // @throws SubmitException
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts, SendFlags flags);             // @throws SubmitException

    // --- routed send (spot -> router) ---
    void sendToRouter(RoutingId peerRid, Message part);                                  // @throws SubmitException
    void sendToRouter(RoutingId peerRid, Message part, SendFlags flags);                 // @throws SubmitException
    void sendToRouter(RoutingId peerRid, List<Message> parts);                           // @throws SubmitException
    void sendToRouter(RoutingId peerRid, List<Message> parts, SendFlags flags);          // @throws SubmitException

    // --- routed request (spot -> router, async, no flags) ---
    CompletableFuture<Received> requestToRouter(RoutingId peerRid, Message part);        // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> requestToRouter(RoutingId peerRid, Message part,
                                                Duration timeout);                       // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> requestToRouter(RoutingId peerRid, List<Message> parts); // @throws SubmitException; future completes with RequestException on failure
    CompletableFuture<Received> requestToRouter(RoutingId peerRid, List<Message> parts,
                                                Duration timeout);                       // @throws SubmitException; future completes with RequestException on failure

    // --- routed request (spot -> router, callback, has flags, throws on submit failure) ---
    void requestToRouter(RoutingId peerRid, Message part,
                         BiConsumer<RequestResult, Received> callback);                  // @throws SubmitException; callback receives RequestResult
    void requestToRouter(RoutingId peerRid, Message part,
                         BiConsumer<RequestResult, Received> callback,
                         SendFlags flags);                                               // @throws SubmitException; callback receives RequestResult
    void requestToRouter(RoutingId peerRid, Message part,
                         BiConsumer<RequestResult, Received> callback,
                         SendFlags flags, Duration timeout);                             // @throws SubmitException; callback receives RequestResult
    void requestToRouter(RoutingId peerRid, List<Message> parts,
                         BiConsumer<RequestResult, Received> callback);                  // @throws SubmitException; callback receives RequestResult
    void requestToRouter(RoutingId peerRid, List<Message> parts,
                         BiConsumer<RequestResult, Received> callback,
                         SendFlags flags);                                               // @throws SubmitException; callback receives RequestResult
    void requestToRouter(RoutingId peerRid, List<Message> parts,
                         BiConsumer<RequestResult, Received> callback,
                         SendFlags flags, Duration timeout);                             // @throws SubmitException; callback receives RequestResult

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

    void close();                                                    // @throws CloseException
}
```

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

---

## Poller

### Poller

Event poller for multiplexing socket and file descriptor readiness.
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
    int size();
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
