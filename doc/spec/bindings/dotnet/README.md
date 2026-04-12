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

    ContextOptions Options { get; }

    void Shutdown();
    void Dispose();
    ValueTask DisposeAsync();
}
```

### ContextOptions

Typed facade for context configuration options.

```csharp
public sealed class ContextOptions
{
    int IoThreads { get; set; }
    int MaxSockets { get; set; }
    int SocketLimit { get; }
    int ThreadPriority { get; set; }
    int ThreadSchedulingPolicy { get; set; }
    int MaxMessageSize { get; set; }
    int MessageThreadSize { get; }
    bool Blocky { get; set; }

    void AddThreadAffinityCpu(int cpu);
    void RemoveThreadAffinityCpu(int cpu);
}
```

---

## Socket Types

### Common base methods

All socket types inherit from `SocketBase` and expose these common operations.

```csharp
// Available on all socket types (SocketBase)
CommonSocketOptions Options { get; }
void Bind(string address);
void Unbind(string address);
SocketMonitor MonitorOpen(SocketEvent events = SocketEvent.All);
void Close();
void Dispose();
ValueTask DisposeAsync();

// Available on connectable socket types (ConnectableSocketBase)
void Connect(string address);
void Disconnect(string address);
```

### PairSocket

Bidirectional exclusive pair socket.

```csharp
public sealed class PairSocket : MessageSocketBase
{
    PairSocket(Context context);

    // inherited from MessageSocketBase
    void Send(Message message, SendFlags flags = SendFlags.None);
    void Send(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
    Received Recv(RecvFlags flags = RecvFlags.None);
    void OnReceive(SocketRecvHandler handler);
    void OnSendReady(Action handler);
}
```

### PubSocket

Publisher socket. Sends topic-prefixed messages to all matching subscribers.

```csharp
public sealed class PubSocket : PublisherSocketBase
{
    PubSocket(Context context);

    PubSocketOptions PubOptions { get; }

    void AttachDiscovery(Discovery discovery);

    // inherited from PublisherSocketBase
    void Publish(string topic, Message message, SendFlags flags = SendFlags.None);
    void Publish(string topic, IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
    void OnSendReady(Action handler);
}
```

### SubSocket

Subscriber socket. Receives topic-filtered messages from publishers.

```csharp
public sealed class SubSocket : SubscriberSocketBase
{
    SubSocket(Context context);

    SubSocketOptions SubOptions { get; }

    void AttachDiscovery(Discovery discovery);

    // inherited from SubscriberSocketBase
    void SetSubscription(string topicOrPattern);
    void UnsetSubscription(string topicOrPattern);
    Subscribed Subscribe(RecvFlags flags = RecvFlags.None);
    void OnSubscribe(SocketSubscribeHandler handler);
}
```

### DealerSocket

Asynchronous client socket for fair-queued request distribution.

```csharp
public sealed class DealerSocket : MessageSocketBase
{
    DealerSocket(Context context);

    DealerSocketOptions DealerOptions { get; }

    void SetRoutingId(RoutingId routingId);
    RoutingId GetRoutingId();

    void AttachDiscovery(Discovery discovery);

    // inherited from MessageSocketBase
    void Send(Message message, SendFlags flags = SendFlags.None);
    void Send(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
    Received Recv(RecvFlags flags = RecvFlags.None);
    void OnReceive(SocketRecvHandler handler);
    void OnSendReady(Action handler);
}
```

### RouterSocket

Server socket that routes messages to specific peers by routing id.

```csharp
public sealed class RouterSocket : ConnectableRoutedMessageSocketBase
{
    RouterSocket(Context context);

    RouterSocketOptions RouterOptions { get; }

    void AttachDiscovery(Discovery discovery);

    // inherited from RoutedMessageSocketBase
    void Send(string routingId, Message message, SendFlags flags = SendFlags.None);
    void Send(RoutingId routingId, Message message, SendFlags flags = SendFlags.None);
    void Send(string routingId, IReadOnlyList<Message> parts,
              SendFlags flags = SendFlags.None);
    void Send(RoutingId routingId, IReadOnlyList<Message> parts,
              SendFlags flags = SendFlags.None);
    Received Recv(RecvFlags flags = RecvFlags.None);
    void OnReceive(SocketRecvHandler handler);
    void OnSendReady(Action handler);

    // --- router -> spot routed send (throws ZlinkException on failure) ---
    void SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message message,
                    SendFlags flags = SendFlags.None);
    void SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                    IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);

    // --- router -> spot routed request (async, blocking submit, no flags) ---
    Task<Received> RequestToSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                      Message message, TimeSpan timeout = default,
                                      CancellationToken ct = default);
    Task<Received> RequestToSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                      IReadOnlyList<Message> parts,
                                      TimeSpan timeout = default,
                                      CancellationToken ct = default);

    // --- router -> spot routed request (callback, has flags, throws on submit failure) ---
    void RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       Message message,
                       Action<RequestResult, Received?> callback,
                       SendFlags flags = SendFlags.None,
                       TimeSpan timeout = default);
    void RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       IReadOnlyList<Message> parts,
                       Action<RequestResult, Received?> callback,
                       SendFlags flags = SendFlags.None,
                       TimeSpan timeout = default);

    // --- router -> spot routed reply (throws ZlinkException on failure) ---
    void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     ulong requestSequence, Message message,
                     SendFlags flags = SendFlags.None);
    void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     ulong requestSequence, IReadOnlyList<Message> parts,
                     SendFlags flags = SendFlags.None);

    // --- router spot receive ---
    Received RecvSpot(RecvFlags flags = RecvFlags.None);
    void OnSpotReceive(Action<Received> handler);
}
```

### XPubSocket

Extended publisher. Like PubSocket but also receives subscription events.

```csharp
public sealed class XPubSocket : PublisherSocketBase
{
    XPubSocket(Context context);

    XPubSocketOptions XPubOptions { get; }

    SubscriptionEvent ReceiveSubscriptionEvent(RecvFlags flags = RecvFlags.None);

    // inherited from PublisherSocketBase
    void Publish(string topic, Message message, SendFlags flags = SendFlags.None);
    void Publish(string topic, IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
    void OnSendReady(Action handler);
}
```

### XSubSocket

Extended subscriber. Like SubSocket with raw subscription forwarding.

```csharp
public sealed class XSubSocket : SubscriberSocketBase
{
    XSubSocket(Context context);

    SubSocketOptions SubOptions { get; }

    // inherited from SubscriberSocketBase
    void SetSubscription(string topicOrPattern);
    void UnsetSubscription(string topicOrPattern);
    Subscribed Subscribe(RecvFlags flags = RecvFlags.None);
    void OnSubscribe(SocketSubscribeHandler handler);
}
```

### StreamSocket

Raw TCP stream socket. Bind-only; does not support `Connect`.

```csharp
public sealed class StreamSocket : RoutedMessageSocketBase
{
    StreamSocket(Context context);

    StreamSocketOptions StreamOptions { get; }

    void AttachStreamRaw(StreamPacketHandler handler);
    void DetachStream();

    // inherited from RoutedMessageSocketBase
    void Send(string routingId, Message message, SendFlags flags = SendFlags.None);
    void Send(RoutingId routingId, Message message, SendFlags flags = SendFlags.None);
    void Send(string routingId, IReadOnlyList<Message> parts,
              SendFlags flags = SendFlags.None);
    void Send(RoutingId routingId, IReadOnlyList<Message> parts,
              SendFlags flags = SendFlags.None);
    Received Recv(RecvFlags flags = RecvFlags.None);
    void OnReceive(SocketRecvHandler handler);
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
    Message();
    Message(int size);
    Message(ReadOnlySpan<byte> data);

    // --- factories ---
    static Message FromBytes(byte[] data);
    static Message FromBytes(ReadOnlySpan<byte> data);
    static Message FromString(string value);
    static Message FromString(string value, Encoding encoding);

    // --- accessors ---
    int Size { get; }
    int RefCount { get; }
    byte[] ToArray();
    ReadOnlySpan<byte> AsReadOnlySpan();
    string GetString();
    string GetString(Encoding encoding);
    string? GetProperty(string property);

    // --- copy to destination ---
    int CopyTo(Span<byte> destination);
    bool TryCopyTo(Span<byte> destination, out int bytesWritten);

    // --- ownership transfer ---
    Message Move();
    Message Copy();

    void Dispose();
    ValueTask DisposeAsync();
}
```

### RoutingId

Immutable string-based routing identity value type.

```csharp
public readonly struct RoutingId : IEquatable<RoutingId>
{
    RoutingId(string value);

    string Value { get; }
    bool IsEmpty { get; }

    string ToString();
    bool Equals(RoutingId other);
    bool Equals(object? obj);
    int GetHashCode();

    static bool operator ==(RoutingId left, RoutingId right);
    static bool operator !=(RoutingId left, RoutingId right);
    static explicit operator string(RoutingId routingId);
}
```

### ZlinkException

Exception thrown when any operation fails.
The `Code` property is a globally unique `int` that spans all result enum
ranges (0-703). The code alone identifies the error without needing to
know which enum it belongs to.

```csharp
public class ZlinkException : Exception
{
    public ZlinkException(int code);
    public ZlinkException(int code, int errno);

    public int Code { get; }
    public int Errno { get; }
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

```csharp
public sealed class Received
{
    string RoutingId { get; }
    RoutingId? RoutingIdValue { get; }
    IReadOnlyList<Message> Parts { get; }
    ulong RequestSequence { get; }
    bool HasRequestSequence { get; }
    bool HasSinglePart { get; }

    Message SinglePartOrThrow();
}
```

### TopicMessage

Topic-aware recv result used by SUB and Spot subscribe paths.

```csharp
public class TopicMessage
{
    string RoutingId { get; }
    RoutingId? RoutingIdValue { get; }
    string Topic { get; }
    IReadOnlyList<Message> Parts { get; }
    bool HasSinglePart { get; }

    Message SinglePartOrThrow();
}
```

### Subscribed

Topic-aware recv result from subscriber sockets. Extends `TopicMessage`.

```csharp
public sealed class Subscribed : TopicMessage { }
```

### SubscriptionEvent

Reports a subscribe/unsubscribe event from XPub sockets.

```csharp
public sealed class SubscriptionEvent
{
    string RoutingId { get; }
    RoutingId? RoutingIdValue { get; }
    string Topic { get; }
    bool Subscribed { get; }
}
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

## Request-Reply

### RequestRouter

Request-reply layer on top of a RouterSocket. Manages request correlation
and reply dispatch.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class RequestRouter : IDisposable, IAsyncDisposable
{
    RequestRouter(RouterSocket socket);

    RouterSocket Socket { get; }
    TimeSpan DefaultRequestTimeout { get; set; }

    // --- request (async, blocking submit, no flags) ---
    Task<Received> RequestAsync(RoutingId routingId, Message message,
                                TimeSpan timeout = default, CancellationToken ct = default);
    Task<Received> RequestAsync(RoutingId routingId, IReadOnlyList<Message> parts,
                                TimeSpan timeout = default, CancellationToken ct = default);

    // --- request (callback, has flags, throws on submit failure) ---
    void Request(RoutingId routingId, Message message,
                 Action<RequestResult, Received?> callback,
                 SendFlags flags = SendFlags.None,
                 TimeSpan timeout = default);
    void Request(RoutingId routingId, IReadOnlyList<Message> parts,
                 Action<RequestResult, Received?> callback,
                 SendFlags flags = SendFlags.None,
                 TimeSpan timeout = default);

    // --- reply (throws ZlinkException on failure) ---
    void Reply(RoutingId routingId, ulong requestSequence, Message message,
               SendFlags flags = SendFlags.None);
    void Reply(RoutingId routingId, ulong requestSequence, IReadOnlyList<Message> parts,
               SendFlags flags = SendFlags.None);

    // --- receive ---
    Received Recv(RecvFlags flags = RecvFlags.None);
    void OnReceive(Action<Received> handler);

    void Dispose();
    ValueTask DisposeAsync();
}
```

### RequestDealer

Request-reply layer on top of a DealerSocket.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class RequestDealer : IDisposable, IAsyncDisposable
{
    RequestDealer(DealerSocket socket);

    DealerSocket Socket { get; }
    TimeSpan DefaultRequestTimeout { get; set; }

    // --- request (async, blocking submit, no flags) ---
    Task<Received> RequestAsync(Message message,
                                TimeSpan timeout = default, CancellationToken ct = default);
    Task<Received> RequestAsync(IReadOnlyList<Message> parts,
                                TimeSpan timeout = default, CancellationToken ct = default);

    // --- request (callback, has flags, throws on submit failure) ---
    void Request(Message message,
                 Action<RequestResult, Received?> callback,
                 SendFlags flags = SendFlags.None,
                 TimeSpan timeout = default);
    void Request(IReadOnlyList<Message> parts,
                 Action<RequestResult, Received?> callback,
                 SendFlags flags = SendFlags.None,
                 TimeSpan timeout = default);

    // --- receive ---
    Received Recv(RecvFlags flags = RecvFlags.None);
    void OnReceive(Action<Received> handler);

    void Dispose();
    ValueTask DisposeAsync();
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
    void OnEvent(Action<SocketMonitorEvent> handler);
    SocketMonitorEvent Recv();
    SocketMonitorEvent? Recv(bool nonBlocking);
    MonitorSnapshot Snapshot();
    void Close();
    void Dispose();
    ValueTask DisposeAsync();
}
```

### ServiceMonitor

Service-level event monitor for discovery and spot.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class ServiceMonitor : IDisposable, IAsyncDisposable
{
    void OnEvent(Action<ServiceMonitorEvent> handler);
    ServiceMonitorEvent Recv();
    ServiceMonitorEvent? Recv(bool nonBlocking);
    MonitorSnapshot Snapshot();
    void Close();
    void Dispose();
    ValueTask DisposeAsync();
}
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

    void Bind(string pubEndpoint, string routerEndpoint);
    void SetId(uint registryId);
    void AddPeer(string peerPubEndpoint);
    void SetHeartbeat(uint intervalMs, uint timeoutMs);
    void SetBroadcastInterval(uint intervalMs);

    RegistryStatus StatusSnapshot();
    RegistryServiceSummaryEntry[] ServiceSummarySnapshot(
        RegistryServiceSummaryFilter? filter = null);
    RegistryTopologyEntry[] TopologySnapshot();
    RegistryTopologyEntry[] TopologyQuery(RegistryTopologyFilter filter);
    MemberPeerEntry[] MemberPeers(ServiceType serviceType, string serviceName);
    Message MemberPeerMetadata(ServiceType serviceType, string serviceName,
                               ServiceRole serviceRole, string endpoint);

    void Close();
    void Dispose();
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

    void ConnectRegistry(string registryPubEndpoint);
    void SetValue(long value);
    long GetValue();
    void SetMetadata(Message metadata);
    Message GetMetadata();

    MemberPeerEntry[] MemberPeers();
    Message MemberPeerMetadata(ServiceRole serviceRole, string endpoint);

    ServiceMonitor MonitorOpen(params ServiceMonitorEventMask[] events);

    void Close();
    void Dispose();
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

    SpotNodePublisherOptions PublisherOptions { get; }
    SpotNodeSubscriberOptions SubscriberOptions { get; }

    void Bind(string endpoint);
    string LastEndpoint { get; }
    void ConnectPeer(string peerEndpoint);
    void DisconnectPeer(string peerEndpoint);
    void AttachDiscovery(Discovery discovery);

    void SetTlsServer(string certPath, string keyPath,
                      bool requireClientCert = false);
    void SetTlsClient(string caCertPath, string hostname,
                      bool trustSystem = false);

    SpotNodeStatus StatusSnapshot();
    SpotNodePeerEntry[] PeersSnapshot();
    SpotNodePeerEntry[] PeersQuery(SpotNodePeerFilter filter);
    SpotNodeSubjectEntry[] SubjectsSnapshot(
        SpotNodeSubjectFilter? filter = null);

    void Close();
    void Dispose();
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

    // --- publish (throws ZlinkException on failure) ---
    void Publish(string topic, Message message, SendFlags flags = SendFlags.None);
    void Publish(string topic, IReadOnlyList<Message> parts,
                 SendFlags flags = SendFlags.None);

    // --- subscribe ---
    void SetSubscription(string topicOrPattern);
    void UnsetSubscription(string topicOrPattern);
    Subscribed Subscribe(RecvFlags flags = RecvFlags.None);
    void OnSubscribe(SpotSubHandler handler);
    void OnSendReady(Action handler);

    // --- routed send (spot -> spot, throws ZlinkException on failure) ---
    void SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message message,
                    SendFlags flags = SendFlags.None);
    void SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                    IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);

    // --- routed request (spot -> spot, async, blocking submit, no flags) ---
    Task<Received> RequestToSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                      Message message, TimeSpan timeout = default,
                                      CancellationToken ct = default);
    Task<Received> RequestToSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                      IReadOnlyList<Message> parts,
                                      TimeSpan timeout = default,
                                      CancellationToken ct = default);

    // --- routed request (spot -> spot, callback, has flags, throws on submit failure) ---
    void RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       Message message,
                       Action<RequestResult, Received?> callback,
                       SendFlags flags = SendFlags.None,
                       TimeSpan timeout = default);
    void RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       IReadOnlyList<Message> parts,
                       Action<RequestResult, Received?> callback,
                       SendFlags flags = SendFlags.None,
                       TimeSpan timeout = default);

    // --- routed reply (spot -> spot, throws ZlinkException on failure) ---
    void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     ulong requestSequence, Message message,
                     SendFlags flags = SendFlags.None);
    void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     ulong requestSequence, IReadOnlyList<Message> parts,
                     SendFlags flags = SendFlags.None);

    // --- routed send (spot -> router, throws ZlinkException on failure) ---
    void SendToRouter(RoutingId peerRid, Message message,
                      SendFlags flags = SendFlags.None);
    void SendToRouter(RoutingId peerRid, IReadOnlyList<Message> parts,
                      SendFlags flags = SendFlags.None);

    // --- routed request (spot -> router, async, blocking submit, no flags) ---
    Task<Received> RequestToRouterAsync(RoutingId peerRid, Message message,
                                        TimeSpan timeout = default,
                                        CancellationToken ct = default);
    Task<Received> RequestToRouterAsync(RoutingId peerRid, IReadOnlyList<Message> parts,
                                        TimeSpan timeout = default,
                                        CancellationToken ct = default);

    // --- routed request (spot -> router, callback, has flags, throws on submit failure) ---
    void RequestToRouter(RoutingId peerRid, Message message,
                         Action<RequestResult, Received?> callback,
                         SendFlags flags = SendFlags.None,
                         TimeSpan timeout = default);
    void RequestToRouter(RoutingId peerRid, IReadOnlyList<Message> parts,
                         Action<RequestResult, Received?> callback,
                         SendFlags flags = SendFlags.None,
                         TimeSpan timeout = default);

    // --- routed reply (spot -> router, throws ZlinkException on failure) ---
    void ReplyToRouter(RoutingId peerRid, ulong requestSequence, Message message,
                       SendFlags flags = SendFlags.None);
    void ReplyToRouter(RoutingId peerRid, ulong requestSequence,
                       IReadOnlyList<Message> parts,
                       SendFlags flags = SendFlags.None);

    // --- routed receive ---
    Received RecvRouted(RecvFlags flags = RecvFlags.None);
    void OnRoutedReceive(Action<Received> handler);
    void OnDispatchEvent(Action<SpotDispatchEvent> handler);

    void Close();
    void Dispose();
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

    void Connect(string endpoint);
    RegistryTopologyEntry[] Snapshot(RegistryTopologyFilter? filter = null);

    void Close();
    void Dispose();
    ValueTask DisposeAsync();
}
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
    void Add(IZlinkSocket socket, PollEvents events, object? tag = null);
    void Modify(IZlinkSocket socket, PollEvents events);
    bool Remove(IZlinkSocket socket);

    // --- file descriptor registration ---
    void AddFd(int fd, PollEvents events, object? tag = null);
    void ModifyFd(int fd, PollEvents events);
    bool Remove(int fd);

    // --- wait ---
    int Wait(List<PollEvent> events, int timeoutMs);
    int Wait(Span<PollEvent> destination, int timeoutMs,
             out int eventsWritten);

    void Clear();
    void Dispose();
    ValueTask DisposeAsync();
}
```

### ZlinkPoll

Static helper for simple poll operations without managing a Poller instance.

```csharp
public static class ZlinkPoll
{
    static int Poll(IReadOnlyList<IZlinkSocket> sockets, int timeoutMs);
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

    static Timer FromSpot(Spot spot);

    void Start(ulong intervalNs, ulong repeatCount);
    void Stop();
    ulong Recv(int flags = 0);
    void OnFire(Action<Timer, ulong> handler);

    void Close();
    void Dispose();
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
    static void Proxy(IZlinkSocket frontend, IZlinkSocket backend,
                      IZlinkSocket? capture = null);

    /// Start a steerable proxy with an additional control socket.
    static void ProxySteerable(IZlinkSocket frontend, IZlinkSocket backend,
                               IZlinkSocket? capture, IZlinkSocket control);

    /// Sleep for the given number of seconds.
    static void Sleep(int seconds);

    /// Close all parts in a multipart message array.
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

    void Dispose();
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

    void Dispose();
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

    void Dispose();
    ValueTask DisposeAsync();
}
```
