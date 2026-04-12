[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# .NET Binding Specification

This document defines the complete public API surface of the .NET binding.
Every class, its purpose, and all public method signatures are listed.
Internal helpers and implementation details are omitted.

All types live in the `Zlink` namespace.

---

## Core

### Context

Manages the lifecycle of IO threads and sockets.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class Context : IDisposable, IAsyncDisposable
{
    Context();

    /// <exception cref="ZlinkConfigException"/>
    ContextOptions Options { get; }

    /// <exception cref="ZlinkCloseException"/>
    void Shutdown();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

### ContextOptions

Typed facade for context configuration options.

```csharp
public sealed class ContextOptions
{
    /// <exception cref="ZlinkConfigException"/>
    int IoThreads { get; set; }
    /// <exception cref="ZlinkConfigException"/>
    int MaxSockets { get; set; }
    /// <exception cref="ZlinkConfigException"/>
    int SocketLimit { get; }
    /// <exception cref="ZlinkConfigException"/>
    int ThreadPriority { get; set; }
    /// <exception cref="ZlinkConfigException"/>
    int ThreadSchedulingPolicy { get; set; }
    /// <exception cref="ZlinkConfigException"/>
    int MaxMessageSize { get; set; }
    /// <exception cref="ZlinkConfigException"/>
    int MessageThreadSize { get; }
    /// <exception cref="ZlinkConfigException"/>
    bool Blocky { get; set; }

    /// <exception cref="ZlinkConfigException"/>
    void AddThreadAffinityCpu(int cpu);
    /// <exception cref="ZlinkConfigException"/>
    void RemoveThreadAffinityCpu(int cpu);
}
```

---

## Socket Types

### Common base methods

All socket types inherit from `SocketBase` and expose these common operations.

```csharp
// Available on all socket types (SocketBase)
/// <exception cref="ZlinkConfigException"/>
CommonSocketOptions Options { get; }
/// <exception cref="ZlinkBindException"/>
void Bind(string address);
/// <exception cref="ZlinkConnectException"/>
void Unbind(string address);
/// <exception cref="ZlinkConfigException"/>
SocketMonitor MonitorOpen(SocketEvent events = SocketEvent.All);
/// <exception cref="ZlinkCloseException"/>
void Close();
/// <exception cref="ZlinkCloseException"/>
void Dispose();
/// <exception cref="ZlinkCloseException"/>
ValueTask DisposeAsync();

// Available on connectable socket types (ConnectableSocketBase)
/// <exception cref="ZlinkConnectException"/>
void Connect(string address);
/// <exception cref="ZlinkConnectException"/>
void Disconnect(string address);
```

### PairSocket

Bidirectional exclusive pair socket.

```csharp
public sealed class PairSocket : MessageSocketBase
{
    PairSocket(Context context);

    // inherited from MessageSocketBase
    /// <exception cref="ZlinkSubmitException"/>
    void Send(Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void Send(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkRecvException"/>
    Received Recv(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnReceive(SocketRecvHandler handler);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSendReady(Action handler);
}
```

### PubSocket

Publisher socket. Sends topic-prefixed messages to all matching subscribers.

```csharp
public sealed class PubSocket : PublisherSocketBase
{
    PubSocket(Context context);

    /// <exception cref="ZlinkConfigException"/>
    PubSocketOptions PubOptions { get; }

    /// <exception cref="ZlinkConfigException"/>
    void AttachDiscovery(Discovery discovery);

    // inherited from PublisherSocketBase
    /// <exception cref="ZlinkSubmitException"/>
    void Publish(string topic, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void Publish(string topic, IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSendReady(Action handler);
}
```

### SubSocket

Subscriber socket. Receives topic-filtered messages from publishers.

```csharp
public sealed class SubSocket : SubscriberSocketBase
{
    SubSocket(Context context);

    /// <exception cref="ZlinkConfigException"/>
    SubSocketOptions SubOptions { get; }

    /// <exception cref="ZlinkConfigException"/>
    void AttachDiscovery(Discovery discovery);

    // inherited from SubscriberSocketBase
    /// <exception cref="ZlinkConfigException"/>
    void SetSubscription(string topicOrPattern);
    /// <exception cref="ZlinkConfigException"/>
    void UnsetSubscription(string topicOrPattern);
    /// <exception cref="ZlinkRecvException"/>
    TopicMessage Subscribe(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSubscribe(SocketSubscribeHandler handler);
}
```

### DealerSocket

Asynchronous client socket for fair-queued request distribution.

```csharp
public sealed class DealerSocket : MessageSocketBase
{
    DealerSocket(Context context);

    /// <exception cref="ZlinkConfigException"/>
    DealerSocketOptions DealerOptions { get; }

    /// <exception cref="ZlinkConfigException"/>
    void SetRoutingId(RoutingId routingId);
    /// <exception cref="ZlinkConfigException"/>
    RoutingId GetRoutingId();

    /// <exception cref="ZlinkConfigException"/>
    void AttachDiscovery(Discovery discovery);

    // inherited from MessageSocketBase
    /// <exception cref="ZlinkSubmitException"/>
    void Send(Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void Send(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkRecvException"/>
    Received Recv(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnReceive(SocketRecvHandler handler);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSendReady(Action handler);

    // --- request (async, blocking submit, no flags) ---
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<Received> RequestAsync(Message part, CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<Received> RequestAsync(Message part, TimeSpan timeout,
                                CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<Received> RequestAsync(IReadOnlyList<Message> parts,
                                CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<Received> RequestAsync(IReadOnlyList<Message> parts, TimeSpan timeout,
                                CancellationToken ct = default);

    // --- request (callback, has flags) ---
    // Callback receives a RequestResult for the reply phase (see ZlinkRequestException / RequestResult).
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    void Request(Message part,
                 Action<RequestResult, Received?> callback,
                 SendFlags flags = SendFlags.None,
                 TimeSpan? timeout = null);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    void Request(IReadOnlyList<Message> parts,
                 Action<RequestResult, Received?> callback,
                 SendFlags flags = SendFlags.None,
                 TimeSpan? timeout = null);
}
```

### RouterSocket

Server socket that routes messages to specific peers by routing id.

```csharp
public sealed class RouterSocket : ConnectableRoutedMessageSocketBase
{
    RouterSocket(Context context);

    /// <exception cref="ZlinkConfigException"/>
    RouterSocketOptions RouterOptions { get; }

    /// <exception cref="ZlinkConfigException"/>
    void AttachDiscovery(Discovery discovery);

    // inherited from RoutedMessageSocketBase
    /// <exception cref="ZlinkSubmitException"/>
    void Send(string routingId, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void Send(RoutingId routingId, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void Send(string routingId, IReadOnlyList<Message> parts,
              SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void Send(RoutingId routingId, IReadOnlyList<Message> parts,
              SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkRecvException"/>
    Received Recv(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnReceive(SocketRecvHandler handler);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSendReady(Action handler);

    // --- request (async, blocking submit, no flags) ---
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<Received> RequestAsync(RoutingId peerRid, Message part,
                                CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<Received> RequestAsync(RoutingId peerRid, Message part, TimeSpan timeout,
                                CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<Received> RequestAsync(RoutingId peerRid, IReadOnlyList<Message> parts,
                                CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<Received> RequestAsync(RoutingId peerRid, IReadOnlyList<Message> parts,
                                TimeSpan timeout, CancellationToken ct = default);

    // --- request (callback, has flags) ---
    // Callback receives a RequestResult for the reply phase (see ZlinkRequestException / RequestResult).
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    void Request(RoutingId peerRid, Message part,
                 Action<RequestResult, Received?> callback,
                 SendFlags flags = SendFlags.None,
                 TimeSpan? timeout = null);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    void Request(RoutingId peerRid, IReadOnlyList<Message> parts,
                 Action<RequestResult, Received?> callback,
                 SendFlags flags = SendFlags.None,
                 TimeSpan? timeout = null);

    // --- reply ---
    /// <exception cref="ZlinkSubmitException"/>
    void Reply(RoutingId rid, ulong requestSeq, Message message,
               SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void Reply(RoutingId rid, ulong requestSeq, IReadOnlyList<Message> parts,
               SendFlags flags = SendFlags.None);

    // --- router -> spot routed send ---
    /// <exception cref="ZlinkSubmitException"/>
    void SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message message,
                    SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                    IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);

    // --- router -> spot routed request (async, blocking submit, no flags) ---
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<Received> RequestToSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                      Message message, TimeSpan timeout = default,
                                      CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<Received> RequestToSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                      IReadOnlyList<Message> parts,
                                      TimeSpan timeout = default,
                                      CancellationToken ct = default);

    // --- router -> spot routed request (callback, has flags) ---
    // Callback receives a RequestResult for the reply phase (see ZlinkRequestException / RequestResult).
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    void RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       Message message,
                       Action<RequestResult, Received?> callback,
                       SendFlags flags = SendFlags.None,
                       TimeSpan timeout = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    void RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       IReadOnlyList<Message> parts,
                       Action<RequestResult, Received?> callback,
                       SendFlags flags = SendFlags.None,
                       TimeSpan timeout = default);

    // --- router -> spot routed reply ---
    /// <exception cref="ZlinkSubmitException"/>
    void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     ulong requestSequence, Message message,
                     SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     ulong requestSequence, IReadOnlyList<Message> parts,
                     SendFlags flags = SendFlags.None);

    // --- router spot receive ---
    /// <exception cref="ZlinkRecvException"/>
    Received RecvSpot(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSpotReceive(Action<Received> handler);
}
```

### XPubSocket

Extended publisher. Like PubSocket but also receives subscription events.

```csharp
public sealed class XPubSocket : PublisherSocketBase
{
    XPubSocket(Context context);

    /// <exception cref="ZlinkConfigException"/>
    XPubSocketOptions XPubOptions { get; }

    /// <exception cref="ZlinkRecvException"/>
    SubscriptionEvent ReceiveSubscriptionEvent(RecvFlags flags = RecvFlags.None);

    // inherited from PublisherSocketBase
    /// <exception cref="ZlinkSubmitException"/>
    void Publish(string topic, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void Publish(string topic, IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSendReady(Action handler);
}
```

### XSubSocket

Extended subscriber. Like SubSocket with raw subscription forwarding.

```csharp
public sealed class XSubSocket : SubscriberSocketBase
{
    XSubSocket(Context context);

    /// <exception cref="ZlinkConfigException"/>
    SubSocketOptions SubOptions { get; }

    // inherited from SubscriberSocketBase
    /// <exception cref="ZlinkConfigException"/>
    void SetSubscription(string topicOrPattern);
    /// <exception cref="ZlinkConfigException"/>
    void UnsetSubscription(string topicOrPattern);
    /// <exception cref="ZlinkRecvException"/>
    TopicMessage Subscribe(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSubscribe(SocketSubscribeHandler handler);
}
```

### StreamSocket

Raw TCP stream socket. Bind-only; does not support `Connect`.

```csharp
public sealed class StreamSocket : RoutedMessageSocketBase
{
    StreamSocket(Context context);

    /// <exception cref="ZlinkConfigException"/>
    StreamSocketOptions StreamOptions { get; }

    /// <exception cref="ZlinkHandlerException"/>
    void AttachStreamRaw(StreamPacketHandler handler);
    /// <exception cref="ZlinkCloseException"/>
    void DetachStream();

    // inherited from RoutedMessageSocketBase
    /// <exception cref="ZlinkSubmitException"/>
    void Send(string routingId, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void Send(RoutingId routingId, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void Send(string routingId, IReadOnlyList<Message> parts,
              SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void Send(RoutingId routingId, IReadOnlyList<Message> parts,
              SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkRecvException"/>
    Received Recv(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnReceive(SocketRecvHandler handler);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSendReady(Action handler);
}
```

---

## Message / Domain Types

### Message

Owns one native message frame.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class Message : IDisposable, IAsyncDisposable
{
    /// <exception cref="ZlinkConfigException"/>
    Message();
    /// <exception cref="ZlinkConfigException"/>
    Message(int size);
    /// <exception cref="ZlinkConfigException"/>
    Message(ReadOnlySpan<byte> data);

    // --- factories ---
    /// <exception cref="ZlinkConfigException"/>
    static Message FromBytes(byte[] data);
    /// <exception cref="ZlinkConfigException"/>
    static Message FromBytes(ReadOnlySpan<byte> data);
    /// <exception cref="ZlinkConfigException"/>
    static Message FromString(string value);
    /// <exception cref="ZlinkConfigException"/>
    static Message FromString(string value, Encoding encoding);

    // --- accessors ---
    /// <exception cref="ZlinkConfigException"/>
    int Size { get; }
    /// <exception cref="ZlinkConfigException"/>
    int RefCount { get; }
    /// <exception cref="ZlinkConfigException"/>
    byte[] ToArray();
    /// <exception cref="ZlinkConfigException"/>
    ReadOnlySpan<byte> AsReadOnlySpan();
    /// <exception cref="ZlinkConfigException"/>
    string GetString();
    /// <exception cref="ZlinkConfigException"/>
    string GetString(Encoding encoding);
    /// <exception cref="ZlinkConfigException"/>
    string? GetProperty(string property);

    // --- copy to destination ---
    /// <exception cref="ZlinkConfigException"/>
    int CopyTo(Span<byte> destination);
    /// <exception cref="ZlinkConfigException"/>
    bool TryCopyTo(Span<byte> destination, out int bytesWritten);

    // --- ownership transfer ---
    /// <exception cref="ZlinkConfigException"/>
    Message Move();
    /// <exception cref="ZlinkConfigException"/>
    Message Copy();

    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

### RoutingId

Immutable binary-safe routing identity value type (1-255 bytes).
Carries raw bytes; string conversions are convenience only.

```csharp
public readonly struct RoutingId : IEquatable<RoutingId>
{
    // --- factories (binary-safe) ---
    /// <exception cref="ZlinkConfigException">Length is 0 or exceeds 255 bytes.</exception>
    static RoutingId FromBytes(ReadOnlySpan<byte> bytes);
    /// <exception cref="ZlinkConfigException">Length is 0 or exceeds 255 bytes.</exception>
    static RoutingId FromBytes(byte[] bytes);

    // --- accessors ---
    int Size { get; }                        // 1..255
    bool IsEmpty { get; }
    ReadOnlySpan<byte> ToBytes();            // zero-copy view of the raw bytes
    byte[] ToByteArray();                    // heap-allocated copy

    // --- string convenience (NOT a primary representation) ---
    string ToString();                       // UTF-8 decode; non-UTF-8 bytes replaced
    string ToHex();                          // lowercase hex

    // --- equality ---
    bool Equals(RoutingId other);
    bool Equals(object? obj);
    int GetHashCode();

    static bool operator ==(RoutingId left, RoutingId right);
    static bool operator !=(RoutingId left, RoutingId right);
}
```

### ZlinkException

Abstract base exception for all zlink failures. Concrete failures always
surface as one of the 8 function-category subclasses below — never as a
raw `ZlinkException` — so callers can distinguish failure domains from the
method signature alone.

Every binding maps the C API's 8 function-category result enums to a
matching exception subclass hierarchy (see the "Per-Function Error Type
Hierarchy" section in the top-level [bindings spec](../README.md)). The
.NET subclasses are:

| C result enum | Subclass |
|---|---|
| `zlink_submit_result_t`  | `ZlinkSubmitException` |
| `zlink_request_result_t` | `ZlinkRequestException` |
| `zlink_recv_result_t`    | `ZlinkRecvException` |
| `zlink_handler_result_t` | `ZlinkHandlerException` |
| `zlink_close_result_t`   | `ZlinkCloseException` |
| `zlink_bind_result_t`    | `ZlinkBindException` |
| `zlink_connect_result_t` | `ZlinkConnectException` |
| `zlink_config_result_t`  | `ZlinkConfigException` |

All zlink exceptions are **unchecked** — .NET has no checked-exception
mechanism. Methods document their failure surface through XML doc
`/// <exception cref="..."/>` comments; catch the concrete subclass when
the failure domain matters, or `ZlinkException` to catch any zlink
failure.

The `Code` property is a globally unique `int` that spans all result enum
ranges (0-703). The code alone identifies the error without needing to
know which enum it belongs to. `InternalErrno` carries the underlying
platform errno when available (0 otherwise).

```csharp
public abstract class ZlinkException : Exception
{
    protected ZlinkException(int code);
    protected ZlinkException(int code, int internalErrno);

    public int Code { get; }
    public int InternalErrno { get; }
    public override string Message { get; }
}
```

### ZlinkSubmitException

Thrown by send / publish / request-submit / reply-submit paths. Wraps a
`SubmitResult`.

```csharp
public sealed class ZlinkSubmitException : ZlinkException
{
    public ZlinkSubmitException(SubmitResult result);
    public ZlinkSubmitException(SubmitResult result, int internalErrno);

    public SubmitResult Result { get; }
}
```

### ZlinkRequestException

Thrown / surfaced for request completion failures. Async `RequestAsync`
overloads raise this exception when the reply phase fails (timeout, peer
terminated, protocol error). Callback-based `Request` overloads instead
deliver the `RequestResult` through the callback — see the callback note
on those methods.

```csharp
public sealed class ZlinkRequestException : ZlinkException
{
    public ZlinkRequestException(RequestResult result);
    public ZlinkRequestException(RequestResult result, int internalErrno);

    public RequestResult Result { get; }
}
```

### ZlinkRecvException

Thrown by recv / subscribe / subscription-event / monitor-recv /
timer-recv paths. Wraps a `RecvResult`.

```csharp
public sealed class ZlinkRecvException : ZlinkException
{
    public ZlinkRecvException(RecvResult result);
    public ZlinkRecvException(RecvResult result, int internalErrno);

    public RecvResult Result { get; }
}
```

### ZlinkHandlerException

Thrown by handler-registration calls (`OnReceive`, `OnSendReady`,
`OnSubscribe`, `OnEvent`, `OnFire`, `OnSpotReceive`, `OnRoutedReceive`,
`OnDispatchEvent`, `AttachStreamRaw`, etc.). Wraps a `HandlerResult`.

```csharp
public sealed class ZlinkHandlerException : ZlinkException
{
    public ZlinkHandlerException(HandlerResult result);
    public ZlinkHandlerException(HandlerResult result, int internalErrno);

    public HandlerResult Result { get; }
}
```

### ZlinkCloseException

Thrown by lifecycle operations (`Close`, `Dispose`, `DisposeAsync`,
`Shutdown`, `DetachStream`). Wraps a `CloseResult`.

```csharp
public sealed class ZlinkCloseException : ZlinkException
{
    public ZlinkCloseException(CloseResult result);
    public ZlinkCloseException(CloseResult result, int internalErrno);

    public CloseResult Result { get; }
}
```

### ZlinkBindException

Thrown by `Bind(...)`. Wraps a `BindResult`.

```csharp
public sealed class ZlinkBindException : ZlinkException
{
    public ZlinkBindException(BindResult result);
    public ZlinkBindException(BindResult result, int internalErrno);

    public BindResult Result { get; }
}
```

### ZlinkConnectException

Thrown by `Connect` / `Disconnect` / `Unbind` / `ConnectPeer` /
`DisconnectPeer` / `ConnectRegistry`. Wraps a `ConnectResult`.

```csharp
public sealed class ZlinkConnectException : ZlinkException
{
    public ZlinkConnectException(ConnectResult result);
    public ZlinkConnectException(ConnectResult result, int internalErrno);

    public ConnectResult Result { get; }
}
```

### ZlinkConfigException

Thrown by option setters/getters, TLS configuration, discovery
attachment, snapshot/query calls, poller mutation, timer configuration,
message lifecycle helpers, and `ContextOptions` mutators. Wraps a
`ConfigResult`.

```csharp
public sealed class ZlinkConfigException : ZlinkException
{
    public ZlinkConfigException(ConfigResult result);
    public ZlinkConfigException(ConfigResult result, int internalErrno);

    public ConfigResult Result { get; }
}
```

### SubmitResult

Result code for send/request/reply/publish operations.
Maps 1-to-1 to the C API `zlink_submit_result_t`.

```csharp
public enum SubmitResult
{
    Ok = 0,
    Backpressured = 1,
    NotConnected = 2,
    NotFound = 3,
    Terminated = 4,
    InvalidHandle = 5,
    InvalidArgument = 6,
    NotSupported = 7,
    InvalidState = 8,
    ThreadViolation = 9,
    OutOfMemory = 10,
    SeqExhausted = 11,
    InternalError = 12
}
```

### RequestResult

Result code delivered to request completion callbacks and async results.

```csharp
public enum RequestResult
{
    Ok = 0,
    TimedOut = 101,
    NotFound = 102,
    Terminated = 103,
    ProtocolError = 104
}
```

### RecvResult

Result code for recv, subscribe, and subscription event operations.

```csharp
public enum RecvResult
{
    Ok = 0,
    NoData = 201,
    Busy = 202,
    Terminated = 203,
    InvalidHandle = 204,
    NotSupported = 205
}
```

### HandlerResult

Result code for handler registration operations (OnReceive, OnSubscribe, etc.).

```csharp
public enum HandlerResult
{
    Ok = 0,
    InvalidArgument = 301,
    Busy = 302,
    NotSupported = 303,
    Deadlock = 304,
    InvalidHandle = 305
}
```

### CloseResult

Result code for close and destroy operations.

```csharp
public enum CloseResult
{
    Ok = 0,
    Busy = 401,
    Shutdown = 402,
    InvalidHandle = 403
}
```

### BindResult

Result code for bind operations.

```csharp
public enum BindResult
{
    Ok = 0,
    InvalidArgument = 501,
    AddrInUse = 502,
    NotSupported = 503,
    InvalidHandle = 504
}
```

### ConnectResult

Result code for connect, disconnect, and unbind operations.

```csharp
public enum ConnectResult
{
    Ok = 0,
    InvalidArgument = 601,
    NotSupported = 602,
    InvalidHandle = 603
}
```

### ConfigResult

Result code for configuration, option, and snapshot operations.

```csharp
public enum ConfigResult
{
    Ok = 0,
    InvalidHandle = 701,
    InvalidArgument = 702,
    NotSupported = 703
}
```

### Received

Aggregates one recv result with optional routing id and message parts.
Implements `IDisposable`.

```csharp
public sealed class Received : IDisposable
{
    RoutingId? RoutingId { get; }            // null when transport carries no source id
    ulong? RequestSeq { get; }               // null when not a request-reply recv
    IReadOnlyList<Message> Parts { get; }
    bool IsSinglePart { get; }

    /// <exception cref="ZlinkRecvException"/>
    Message FirstPart();
    /// <exception cref="ZlinkRecvException"/>
    Message SinglePartOrThrow();

    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
}
```

### TopicMessage

Topic-aware recv result used by SUB / XSUB / Spot subscribe paths.
Implements `IDisposable`.

```csharp
public sealed class TopicMessage : IDisposable
{
    RoutingId? RoutingId { get; }            // null when transport carries no source id
    string Topic { get; }                    // UTF-8
    IReadOnlyList<Message> Parts { get; }
    bool IsSinglePart { get; }

    /// <exception cref="ZlinkRecvException"/>
    Message FirstPart();
    /// <exception cref="ZlinkRecvException"/>
    Message SinglePartOrThrow();

    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
}
```

### SubscriptionEvent

Reports a subscribe/unsubscribe event from XPub sockets.
Pure value object (no lifecycle). Defined as a record.

```csharp
public sealed record SubscriptionEvent(
    RoutingId? RoutingId,                    // null when transport carries no source id
    string Topic,                            // UTF-8
    bool Subscribed);                        // true=subscribe, false=unsubscribe
```

### SendFlags

```csharp
[Flags]
public enum SendFlags
{
    None = 0,
    DontWait = 1
}
```

### RecvFlags

```csharp
[Flags]
public enum RecvFlags
{
    None = 0,
    DontWait = 1
}
```

---

## Monitoring

### SocketMonitor

Socket-level event monitor. Receives connect, disconnect, and handshake events.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class SocketMonitor : IDisposable, IAsyncDisposable
{
    /// <exception cref="ZlinkHandlerException"/>
    void OnEvent(Action<MonitorEvent> handler);
    /// <exception cref="ZlinkRecvException"/>
    MonitorEvent Recv();
    /// <exception cref="ZlinkRecvException"/>
    MonitorEvent? Recv(bool nonBlocking);
    /// <exception cref="ZlinkConfigException"/>
    MonitorSnapshot Snapshot();
    /// <exception cref="ZlinkCloseException"/>
    void Close();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

### ServiceMonitor

Service-level event monitor for discovery and spot.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class ServiceMonitor : IDisposable, IAsyncDisposable
{
    /// <exception cref="ZlinkHandlerException"/>
    void OnEvent(Action<ServiceEvent> handler);
    /// <exception cref="ZlinkRecvException"/>
    ServiceEvent Recv();
    /// <exception cref="ZlinkRecvException"/>
    ServiceEvent? Recv(bool nonBlocking);
    /// <exception cref="ZlinkConfigException"/>
    MonitorSnapshot Snapshot();
    /// <exception cref="ZlinkCloseException"/>
    void Close();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

### MonitorEvent

Socket monitor event. Pure value object.

```csharp
public sealed record MonitorEvent(
    MonitorEventType Event,                  // CONNECTION_READY, CONNECTED, DISCONNECTED, ...
    uint Value,                              // per-event detail (e.g. disconnect reason code)
    RoutingId? RoutingId,                    // null when event has no associated peer
    string LocalAddr,
    string RemoteAddr);
```

### MonitorSnapshot

Runtime snapshot of a socket or service monitor handle.

```csharp
public sealed class MonitorSnapshot
{
    SourceKind SourceKind { get; }           // monitor target kind
    uint StateFlags { get; }                 // state bitmask
    uint DetailFlags { get; }                // detail bitmask
    ulong SndPendingMsgs { get; }            // send-queue pending messages
    ulong RcvPendingMsgs { get; }            // recv-queue pending messages

    bool IsReady { get; }                    // ready bit of StateFlags
}
```

### ServiceEvent

Service monitor event (discovery / registry / spot). Pure value object.

```csharp
public sealed record ServiceEvent(
    ServiceKind ServiceKind,                 // ZLINK_SERVICE_TYPE_SPOT, SOCKET, ...
    ServiceEventType EventType,              // UP, DOWN, PROVIDERS_CHANGED, ERROR, ...
    uint Status,                             // status code
    uint ErrorCode,                          // errno on error, 0 otherwise
    ulong Value,                             // per-event value
    uint DetailFlags,                        // detail bitmask
    string ServiceName,
    string Endpoint,
    RoutingId? RoutingId,                    // peer routing id (null when not applicable)
    string Subject,                          // subscribe subject (topic)
    SubjectKind SubjectKind);                // subject kind
```

---

## Services

### Registry

Registry service node. Manages service topology and membership broadcast.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class Registry : IDisposable, IAsyncDisposable
{
    Registry(Context context);

    /// <exception cref="ZlinkBindException"/>
    void Bind(string pubEndpoint, string routerEndpoint);
    /// <exception cref="ZlinkConfigException"/>
    void SetId(uint registryId);
    /// <exception cref="ZlinkConfigException"/>
    void AddPeer(string peerPubEndpoint);
    /// <exception cref="ZlinkConfigException"/>
    void SetHeartbeat(uint intervalMs, uint timeoutMs);
    /// <exception cref="ZlinkConfigException"/>
    void SetBroadcastInterval(uint intervalMs);

    /// <exception cref="ZlinkConfigException"/>
    RegistryStatus StatusSnapshot();
    /// <exception cref="ZlinkConfigException"/>
    RegistryServiceSummaryEntry[] ServiceSummarySnapshot(
        RegistryServiceSummaryFilter? filter = null);
    /// <exception cref="ZlinkConfigException"/>
    RegistryTopologyEntry[] TopologySnapshot();
    /// <exception cref="ZlinkConfigException"/>
    RegistryTopologyEntry[] TopologyQuery(RegistryTopologyFilter filter);
    /// <exception cref="ZlinkConfigException"/>
    MemberPeerEntry[] MemberPeers(ServiceType serviceType, string serviceName);
    /// <exception cref="ZlinkConfigException"/>
    Message MemberPeerMetadata(ServiceType serviceType, string serviceName,
                               ServiceRole serviceRole, string endpoint);

    /// <exception cref="ZlinkCloseException"/>
    void Close();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

### Discovery

Fixed-service discovery view. Tracks one service type/name pair.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class Discovery : IDisposable, IAsyncDisposable
{
    Discovery(Context context, ServiceType serviceType, string serviceName);

    /// <exception cref="ZlinkConnectException"/>
    void ConnectRegistry(string registryPubEndpoint);
    /// <exception cref="ZlinkConfigException"/>
    void SetValue(long value);
    /// <exception cref="ZlinkConfigException"/>
    long GetValue();
    /// <exception cref="ZlinkConfigException"/>
    void SetMetadata(Message metadata);
    /// <exception cref="ZlinkConfigException"/>
    Message GetMetadata();

    /// <exception cref="ZlinkConfigException"/>
    MemberPeerEntry[] MemberPeers();
    /// <exception cref="ZlinkConfigException"/>
    Message MemberPeerMetadata(ServiceRole serviceRole, string endpoint);

    /// <exception cref="ZlinkConfigException"/>
    ServiceMonitor MonitorOpen(params ServiceMonitorEventMask[] events);

    /// <exception cref="ZlinkCloseException"/>
    void Close();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

### SpotNode

Spot node lifecycle and topology facade.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class SpotNode : IDisposable, IAsyncDisposable
{
    SpotNode(Context context);

    /// <exception cref="ZlinkConfigException"/>
    SpotNodePublisherOptions PublisherOptions { get; }
    /// <exception cref="ZlinkConfigException"/>
    SpotNodeSubscriberOptions SubscriberOptions { get; }

    /// <exception cref="ZlinkBindException"/>
    void Bind(string endpoint);
    /// <exception cref="ZlinkConfigException"/>
    string LastEndpoint { get; }
    /// <exception cref="ZlinkConnectException"/>
    void ConnectPeer(string peerEndpoint);
    /// <exception cref="ZlinkConnectException"/>
    void DisconnectPeer(string peerEndpoint);
    /// <exception cref="ZlinkConfigException"/>
    void AttachDiscovery(Discovery discovery);

    /// <exception cref="ZlinkConfigException"/>
    void SetTlsServer(string certPath, string keyPath,
                      bool requireClientCert = false);
    /// <exception cref="ZlinkConfigException"/>
    void SetTlsClient(string caCertPath, string hostname,
                      bool trustSystem = false);

    /// <exception cref="ZlinkConfigException"/>
    SpotNodeStatus StatusSnapshot();
    /// <exception cref="ZlinkConfigException"/>
    SpotNodePeerEntry[] PeersSnapshot();
    /// <exception cref="ZlinkConfigException"/>
    SpotNodePeerEntry[] PeersQuery(SpotNodePeerFilter filter);
    /// <exception cref="ZlinkConfigException"/>
    SpotNodeSubjectEntry[] SubjectsSnapshot(
        SpotNodeSubjectFilter? filter = null);

    /// <exception cref="ZlinkCloseException"/>
    void Close();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

### Spot

Spot messaging endpoint. Provides pub/sub and subscription management.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class Spot : IDisposable, IAsyncDisposable
{
    Spot(SpotNode node);

    // --- publish ---
    /// <exception cref="ZlinkSubmitException"/>
    void Publish(string topic, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void Publish(string topic, IReadOnlyList<Message> parts,
                 SendFlags flags = SendFlags.None);

    // --- subscribe ---
    /// <exception cref="ZlinkConfigException"/>
    void SetSubscription(string topicOrPattern);
    /// <exception cref="ZlinkConfigException"/>
    void UnsetSubscription(string topicOrPattern);
    /// <exception cref="ZlinkRecvException"/>
    TopicMessage Subscribe(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSubscribe(SpotSubHandler handler);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSendReady(Action handler);

    // --- routed send (spot -> spot) ---
    /// <exception cref="ZlinkSubmitException"/>
    void SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message message,
                    SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                    IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);

    // --- routed request (spot -> spot, async, blocking submit, no flags) ---
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<Received> RequestToSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                      Message message, TimeSpan timeout = default,
                                      CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<Received> RequestToSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                      IReadOnlyList<Message> parts,
                                      TimeSpan timeout = default,
                                      CancellationToken ct = default);

    // --- routed request (spot -> spot, callback, has flags) ---
    // Callback receives a RequestResult for the reply phase (see ZlinkRequestException / RequestResult).
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    void RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       Message message,
                       Action<RequestResult, Received?> callback,
                       SendFlags flags = SendFlags.None,
                       TimeSpan timeout = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    void RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       IReadOnlyList<Message> parts,
                       Action<RequestResult, Received?> callback,
                       SendFlags flags = SendFlags.None,
                       TimeSpan timeout = default);

    // --- routed reply (spot -> spot) ---
    /// <exception cref="ZlinkSubmitException"/>
    void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     ulong requestSequence, Message message,
                     SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     ulong requestSequence, IReadOnlyList<Message> parts,
                     SendFlags flags = SendFlags.None);

    // --- routed send (spot -> router) ---
    /// <exception cref="ZlinkSubmitException"/>
    void SendToRouter(RoutingId peerRid, Message message,
                      SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void SendToRouter(RoutingId peerRid, IReadOnlyList<Message> parts,
                      SendFlags flags = SendFlags.None);

    // --- routed request (spot -> router, async, blocking submit, no flags) ---
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<Received> RequestToRouterAsync(RoutingId peerRid, Message message,
                                        TimeSpan timeout = default,
                                        CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<Received> RequestToRouterAsync(RoutingId peerRid, IReadOnlyList<Message> parts,
                                        TimeSpan timeout = default,
                                        CancellationToken ct = default);

    // --- routed request (spot -> router, callback, has flags) ---
    // Callback receives a RequestResult for the reply phase (see ZlinkRequestException / RequestResult).
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    void RequestToRouter(RoutingId peerRid, Message message,
                         Action<RequestResult, Received?> callback,
                         SendFlags flags = SendFlags.None,
                         TimeSpan timeout = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    void RequestToRouter(RoutingId peerRid, IReadOnlyList<Message> parts,
                         Action<RequestResult, Received?> callback,
                         SendFlags flags = SendFlags.None,
                         TimeSpan timeout = default);

    // --- routed reply (spot -> router) ---
    /// <exception cref="ZlinkSubmitException"/>
    void ReplyToRouter(RoutingId peerRid, ulong requestSequence, Message message,
                       SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void ReplyToRouter(RoutingId peerRid, ulong requestSequence,
                       IReadOnlyList<Message> parts,
                       SendFlags flags = SendFlags.None);

    // --- routed receive ---
    /// <exception cref="ZlinkRecvException"/>
    Received RecvRouted(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnRoutedReceive(Action<Received> handler);
    /// <exception cref="ZlinkHandlerException"/>
    void OnDispatchEvent(Action<SpotDispatchEvent> handler);

    /// <exception cref="ZlinkCloseException"/>
    void Close();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

### RegistryQueryClient

Remote registry query client. Connects to a registry and fetches topology snapshots.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class RegistryQueryClient : IDisposable, IAsyncDisposable
{
    RegistryQueryClient(Context context);

    /// <exception cref="ZlinkConnectException"/>
    void Connect(string endpoint);
    /// <exception cref="ZlinkConfigException"/>
    RegistryTopologyEntry[] Snapshot(RegistryTopologyFilter? filter = null);

    /// <exception cref="ZlinkCloseException"/>
    void Close();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

### Service-Layer Entry Types

Snapshot / query result value objects returned by the service layer. All
are pure value objects exposed as records with named typed fields. They
never expose raw C structs.

#### MemberPeerEntry

Discovery / registry member peer entry.

```csharp
public sealed record MemberPeerEntry(
    ServiceType ServiceType,
    ServiceRole ServiceRole,
    string ServiceName,
    string Endpoint,
    RoutingId? RoutingId,
    long Value);
```

#### RegistryTopologyEntry

Registry topology entry.

```csharp
public sealed record RegistryTopologyEntry(
    RoutingId? RoutingId,
    ServiceKind ServiceKind,
    ServiceRole ServiceRole,
    string ServiceName,
    string Endpoint,
    TopologySource Source,
    TopologyState State,
    uint DesiredCount,
    uint ReadyCount,
    uint ErrorCode,
    ulong LastReportedMs);
```

#### RegistryServiceSummaryEntry

Registry service summary entry.

```csharp
public sealed record RegistryServiceSummaryEntry(
    ServiceKind ServiceKind,
    ServiceRole ServiceRole,
    string ServiceName,
    uint TotalCount,
    uint ConnectingCount,
    uint ReadyCount,
    uint ErrorCount,
    uint StoppedCount,
    ulong LastReportedMs);
```

#### RegistryStatus

Registry status snapshot.

```csharp
public sealed record RegistryStatus(
    uint RegistryId,
    string BindEndpoint,
    RegistryState State,
    uint TopologyEntryCount,
    uint PeerRegistryCount,
    uint ConnectedPeerRegistryCount,
    ulong ListSeq,
    int LastError,
    ulong LastChangedMs);
```

#### SpotNodeStatus

Spot node status snapshot.

```csharp
public sealed record SpotNodeStatus(
    string ServiceName,
    string LocalEndpoint,
    RoutingId? NodeRoutingId,
    SpotNodeState State,
    uint ConfiguredPeerCount,
    uint ActivePeerCount,
    uint ConnectedPeerCount,
    uint SubjectCount,
    uint ReadySubjectCount,
    int LastError,
    ulong LastChangedMs);
```

#### SpotNodePeerEntry

Spot node peer entry.

```csharp
public sealed record SpotNodePeerEntry(
    string ServiceName,
    string LocalEndpoint,
    string PeerEndpoint,
    SpotPeerSource Source,
    SpotPeerState State,
    ulong ConnectedSinceMs,
    ulong LastChangedMs);
```

#### SpotNodeSubjectEntry

Spot node subject entry.

```csharp
public sealed record SpotNodeSubjectEntry(
    SpotRole Role,
    string Subject,
    SubjectKind SubjectKind,
    uint ReadyPeerCount,
    uint ActivePeerCount,
    ulong LastChangedMs);
```

---

## Poller

### Poller

Event poller for multiplexing socket and file descriptor readiness.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class Poller : IDisposable, IAsyncDisposable
{
    Poller();

    int Count { get; }

    // --- socket registration ---
    /// <exception cref="ZlinkConfigException"/>
    void Add(IZlinkSocket socket, PollEvents events, object? tag = null);
    /// <exception cref="ZlinkConfigException"/>
    void Modify(IZlinkSocket socket, PollEvents events);
    /// <exception cref="ZlinkConfigException"/>
    bool Remove(IZlinkSocket socket);

    // --- file descriptor registration ---
    /// <exception cref="ZlinkConfigException"/>
    void AddFd(int fd, PollEvents events, object? tag = null);
    /// <exception cref="ZlinkConfigException"/>
    void ModifyFd(int fd, PollEvents events);
    /// <exception cref="ZlinkConfigException"/>
    bool Remove(int fd);

    // --- wait ---
    /// <exception cref="ZlinkRecvException"/>
    int Wait(List<PollEvent> events, int timeoutMs);
    /// <exception cref="ZlinkRecvException"/>
    int Wait(Span<PollEvent> destination, int timeoutMs,
             out int eventsWritten);

    /// <exception cref="ZlinkConfigException"/>
    void Clear();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

### ZlinkPoll

Static helper for simple poll operations without managing a Poller instance.

```csharp
public static class ZlinkPoll
{
    /// <exception cref="ZlinkRecvException"/>
    static int Poll(IReadOnlyList<IZlinkSocket> sockets, int timeoutMs);
    /// <exception cref="ZlinkRecvException"/>
    static int Poll(IReadOnlyList<SocketMonitor> monitors, int timeoutMs);
}
```

---

## Timer

### Timer

Interval timer with optional spot integration.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class Timer : IDisposable, IAsyncDisposable
{
    Timer();

    /// <exception cref="ZlinkConfigException"/>
    static Timer FromSpot(Spot spot);

    /// <exception cref="ZlinkConfigException"/>
    void Start(ulong intervalNs, ulong repeatCount);
    /// <exception cref="ZlinkConfigException"/>
    void Stop();
    /// <exception cref="ZlinkRecvException"/>
    ulong Recv(int flags = 0);
    /// <exception cref="ZlinkHandlerException"/>
    void OnFire(Action<Timer, ulong> handler);

    /// <exception cref="ZlinkCloseException"/>
    void Close();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

---

## Utilities

### Zlink

Static utility class for global library operations.

```csharp
public static class Zlink
{
    /// Return the errno for the current thread.
    static int Errno();

    /// Return a human-readable string for the given error number.
    static string Strerror(int errnum);

    /// Return the runtime library version as (major, minor, patch).
    static (int Major, int Minor, int Patch) Version();

    /// Check if the library supports a given capability (e.g. "ipc", "tls").
    static bool Has(string capability);

    /// Start a built-in proxy between frontend and backend sockets.
    /// <exception cref="ZlinkConfigException"/>
    static void Proxy(IZlinkSocket frontend, IZlinkSocket backend,
                      IZlinkSocket? capture = null);

    /// Start a steerable proxy with an additional control socket.
    /// <exception cref="ZlinkConfigException"/>
    static void ProxySteerable(IZlinkSocket frontend, IZlinkSocket backend,
                               IZlinkSocket? capture, IZlinkSocket control);

    /// Sleep for the given number of seconds.
    static void Sleep(int seconds);

    /// Close all parts in a multipart message array.
    /// <exception cref="ZlinkConfigException"/>
    static void MultipartClose(IReadOnlyList<Message> parts);
}
```

### ZlinkStopwatch

Simple elapsed-time stopwatch.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class ZlinkStopwatch : IDisposable, IAsyncDisposable
{
    ZlinkStopwatch();

    ulong Intermediate();
    ulong Stop();

    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

### ZlinkThread

Background thread managed by the C library.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class ZlinkThread : IDisposable, IAsyncDisposable
{
    ZlinkThread(Action task);

    /// Wait for the thread to finish and release its handle.
    void Join();

    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

### AtomicCounter

Lock-free atomic counter.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class AtomicCounter : IDisposable, IAsyncDisposable
{
    AtomicCounter();

    void Set(int value);
    int Increment();
    int Decrement();
    int Value { get; }

    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```
