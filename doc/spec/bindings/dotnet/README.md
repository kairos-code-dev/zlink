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
    void Send(Message message);
    void Send(IReadOnlyList<Message> parts);
    SendResult TrySend(Message message);
    SendResult TrySend(IReadOnlyList<Message> parts);
    Received Recv();
    bool TryRecv(out Received? received);
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
    void Publish(string topic, Message message);
    void Publish(string topic, IReadOnlyList<Message> parts);
    SendResult TryPublish(string topic, Message message);
    SendResult TryPublish(string topic, IReadOnlyList<Message> parts);
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
    Subscribed Subscribe();
    bool TrySubscribe(out Subscribed? subscribed);
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
    void Send(Message message);
    void Send(IReadOnlyList<Message> parts);
    SendResult TrySend(Message message);
    SendResult TrySend(IReadOnlyList<Message> parts);
    Received Recv();
    bool TryRecv(out Received? received);
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
    void Send(string routingId, Message message);
    void Send(RoutingId routingId, Message message);
    void Send(string routingId, IReadOnlyList<Message> parts);
    void Send(RoutingId routingId, IReadOnlyList<Message> parts);
    SendResult TrySend(string routingId, Message message);
    SendResult TrySend(RoutingId routingId, Message message);
    SendResult TrySend(string routingId, IReadOnlyList<Message> parts);
    SendResult TrySend(RoutingId routingId, IReadOnlyList<Message> parts);
    Received Recv();
    bool TryRecv(out Received? received);
    void OnReceive(SocketRecvHandler handler);
    void OnSendReady(Action handler);

    // --- router → spot routed send ---
    void SendSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message message);
    void SendSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                  IReadOnlyList<Message> parts);
    SendResult TrySendSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                           Message message);
    SendResult TrySendSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                           IReadOnlyList<Message> parts);

    // --- router → spot routed request (async) ---
    Task<Received> RequestSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                    Message message, TimeSpan timeout = default,
                                    CancellationToken ct = default);
    Task<Received> RequestSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                    IReadOnlyList<Message> parts,
                                    TimeSpan timeout = default,
                                    CancellationToken ct = default);
    void RequestSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message message,
                     RequestReplyCallback onReply, RequestErrorCallback onError,
                     TimeSpan timeout = default);

    // --- router → spot routed reply ---
    void ReplySpot(RoutingId destNodeRid, RoutingId destSpotRid,
                   ulong requestSequence, Message message);
    void ReplySpot(RoutingId destNodeRid, RoutingId destSpotRid,
                   ulong requestSequence, IReadOnlyList<Message> parts);

    // --- router spot receive ---
    Received RecvSpot();
    bool TryRecvSpot(out Received? received);
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

    SubscriptionEvent ReceiveSubscriptionEvent();
    bool TryReceiveSubscriptionEvent(out SubscriptionEvent? subscriptionEvent);

    // inherited from PublisherSocketBase
    void Publish(string topic, Message message);
    void Publish(string topic, IReadOnlyList<Message> parts);
    SendResult TryPublish(string topic, Message message);
    SendResult TryPublish(string topic, IReadOnlyList<Message> parts);
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
    Subscribed Subscribe();
    bool TrySubscribe(out Subscribed? subscribed);
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
    void Send(string routingId, Message message);
    void Send(RoutingId routingId, Message message);
    void Send(string routingId, IReadOnlyList<Message> parts);
    void Send(RoutingId routingId, IReadOnlyList<Message> parts);
    SendResult TrySend(string routingId, Message message);
    SendResult TrySend(RoutingId routingId, Message message);
    SendResult TrySend(string routingId, IReadOnlyList<Message> parts);
    SendResult TrySend(RoutingId routingId, IReadOnlyList<Message> parts);
    Received Recv();
    bool TryRecv(out Received? received);
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

### SendResult

```csharp
public enum SendResult
{
    Sent,
    Backpressured,
    NotReady
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

    // --- RequestAsync ---
    Task<Received> RequestAsync(RoutingId routingId, Message message,
                                TimeSpan timeout = default, CancellationToken ct = default);
    Task<Received> RequestAsync(RoutingId routingId, IReadOnlyList<Message> parts,
                                TimeSpan timeout = default, CancellationToken ct = default);

    // --- TryRequestAsync ---
    Task<Received> TryRequestAsync(RoutingId routingId, Message message,
                                   TimeSpan timeout = default, CancellationToken ct = default);
    Task<Received> TryRequestAsync(RoutingId routingId, IReadOnlyList<Message> parts,
                                   TimeSpan timeout = default, CancellationToken ct = default);

    // --- Request (callback) ---
    void Request(RoutingId routingId, Message message,
                 RequestReplyCallback onReply, RequestErrorCallback onError,
                 TimeSpan timeout = default);
    void Request(RoutingId routingId, IReadOnlyList<Message> parts,
                 RequestReplyCallback onReply, RequestErrorCallback onError,
                 TimeSpan timeout = default);

    // --- TryRequest (callback) ---
    void TryRequest(RoutingId routingId, Message message,
                    RequestReplyCallback onReply, RequestErrorCallback onError,
                    TimeSpan timeout = default);
    void TryRequest(RoutingId routingId, IReadOnlyList<Message> parts,
                    RequestReplyCallback onReply, RequestErrorCallback onError,
                    TimeSpan timeout = default);

    // --- Reply ---
    void Reply(RoutingId routingId, ulong requestSequence, Message message);
    void Reply(RoutingId routingId, ulong requestSequence, IReadOnlyList<Message> parts);
    SendResult TryReply(RoutingId routingId, ulong requestSequence, Message message);
    SendResult TryReply(RoutingId routingId, ulong requestSequence, IReadOnlyList<Message> parts);

    // --- Receive ---
    Received Recv();
    bool TryRecv(out Received? received);
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

    // --- RequestAsync ---
    Task<Received> RequestAsync(Message message,
                                TimeSpan timeout = default, CancellationToken ct = default);
    Task<Received> RequestAsync(IReadOnlyList<Message> parts,
                                TimeSpan timeout = default, CancellationToken ct = default);

    // --- TryRequestAsync ---
    Task<Received> TryRequestAsync(Message message,
                                   TimeSpan timeout = default, CancellationToken ct = default);
    Task<Received> TryRequestAsync(IReadOnlyList<Message> parts,
                                   TimeSpan timeout = default, CancellationToken ct = default);

    // --- Request (callback) ---
    void Request(Message message, RequestReplyCallback onReply,
                 RequestErrorCallback onError, TimeSpan timeout = default);
    void Request(IReadOnlyList<Message> parts, RequestReplyCallback onReply,
                 RequestErrorCallback onError, TimeSpan timeout = default);

    // --- TryRequest (callback) ---
    void TryRequest(Message message, RequestReplyCallback onReply,
                    RequestErrorCallback onError, TimeSpan timeout = default);
    void TryRequest(IReadOnlyList<Message> parts, RequestReplyCallback onReply,
                    RequestErrorCallback onError, TimeSpan timeout = default);

    // --- Receive ---
    Received Recv();
    bool TryRecv(out Received? received);
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
    bool TryRecv(out SocketMonitorEvent? monitorEvent);
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
    bool TryRecv(out ServiceMonitorEvent? monitorEvent);
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

    // --- publish ---
    void Publish(string topic, Message message);
    void Publish(string topic, IReadOnlyList<Message> parts);
    SendResult TryPublish(string topic, Message message);
    SendResult TryPublish(string topic, IReadOnlyList<Message> parts);

    // --- subscribe ---
    void SetSubscription(string topicOrPattern);
    void UnsetSubscription(string topicOrPattern);
    Subscribed Subscribe();
    bool TrySubscribe(out Subscribed? subscribed);
    void OnSubscribe(SpotSubHandler handler);
    void OnSendReady(Action handler);

    // --- routed send (spot → spot) ---
    void SendSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message message);
    void SendSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                  IReadOnlyList<Message> parts);
    SendResult TrySendSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                           Message message);
    SendResult TrySendSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                           IReadOnlyList<Message> parts);

    // --- routed request (spot → spot, async) ---
    Task<Received> RequestSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                    Message message, TimeSpan timeout = default,
                                    CancellationToken ct = default);
    Task<Received> RequestSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                    IReadOnlyList<Message> parts,
                                    TimeSpan timeout = default,
                                    CancellationToken ct = default);
    void RequestSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message message,
                     RequestReplyCallback onReply, RequestErrorCallback onError,
                     TimeSpan timeout = default);

    // --- routed reply (spot → spot) ---
    void ReplySpot(RoutingId destNodeRid, RoutingId destSpotRid,
                   ulong requestSequence, Message message);
    void ReplySpot(RoutingId destNodeRid, RoutingId destSpotRid,
                   ulong requestSequence, IReadOnlyList<Message> parts);

    // --- routed send (spot → router) ---
    void SendRouter(RoutingId peerRid, Message message);
    void SendRouter(RoutingId peerRid, IReadOnlyList<Message> parts);
    SendResult TrySendRouter(RoutingId peerRid, Message message);
    SendResult TrySendRouter(RoutingId peerRid, IReadOnlyList<Message> parts);

    // --- routed request (spot → router, async) ---
    Task<Received> RequestRouterAsync(RoutingId peerRid, Message message,
                                      TimeSpan timeout = default,
                                      CancellationToken ct = default);
    Task<Received> RequestRouterAsync(RoutingId peerRid, IReadOnlyList<Message> parts,
                                      TimeSpan timeout = default,
                                      CancellationToken ct = default);
    void RequestRouter(RoutingId peerRid, Message message,
                       RequestReplyCallback onReply, RequestErrorCallback onError,
                       TimeSpan timeout = default);

    // --- routed reply (spot → router) ---
    void ReplyRouter(RoutingId peerRid, ulong requestSequence, Message message);
    void ReplyRouter(RoutingId peerRid, ulong requestSequence,
                     IReadOnlyList<Message> parts);

    // --- routed receive ---
    Received RecvRouted();
    bool TryRecvRouted(out Received? received);
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
