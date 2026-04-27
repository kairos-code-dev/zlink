[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# .NET Binding Specification

This document defines the complete public API surface of the .NET binding.
Every class, its purpose, and all public method signatures are listed.
Internal helpers and implementation details are omitted.

All types live in the `Zlink` namespace.

Only the public types and members listed in this document are part of the
contract. `internal` types, `Zlink.Sockets.Internal`, native interop helpers,
and other assembly-private helpers are not public API. Perf, samples, and
tests must use the public assembly surface only. `InternalsVisibleTo` may be
used for tests during development, but it does not change the public contract.

Implementation follow-up:
- `InternalsVisibleTo` must not remain open for perf projects once the public
  surface and perf harness are fully aligned.
- Perf and sample projects must continue to compile against the public
  assembly surface only.

---

## Current Core Alignment Overrides

The sections below still contain some older signatures. When they conflict
with the rules here, this section wins.

- `PairSocket`, `DealerSocket`, and `RouterSocket` are recv-only on the data
  plane. Remove `OnReceive(...)` from their public contract.
- `SubSocket` and `XSubSocket` are recv-only. Remove `OnSubscribe(...)` from
  their public contract.
- `StreamSocket` keeps `Recv(...)` and exposes a packet callback surface
  mapped to `zlink_stream_packet_handler()`. Recommended canonical name:
  `OnPacket(...)`.
- `SpotNode` must expose channel-aware attachment APIs:
  `AttachDiscovery(Discovery discovery)`,
  `AttachChannelDealer(Discovery discovery, DealerSocket dealer)`,
  `AttachChannelDealerManual(string channelName, DealerSocket dealer)`, and
  `AttachPubIngress(PubSocket pub)`.
- `Spot` must expose channel-aware data-plane methods:
  `SendChannel(...)`, `SendToSpot(...)`, `RequestChannelAsync(...)`, and
  `Publish(string serviceName, string topic, ...)`.
- `Spot.OnDispatchEvent(...)` takes `Action<Spot, SpotDispatchInfo>`.
  `SpotDispatchInfo` carries `Event`, `SubjectKind`, and `Subject`
  (`IntPtr`, the source handle). `Spot` must also expose
  `DrainChannelReplyFrom(IntPtr dealerSubject)` for draining
  `ChannelReplyReadable` completions from the given attached dealer.
- `DealerSocket` must expose `SetChannelName(string channelName)` and
  `GetChannelName()` for fixed logical channel name metadata used by
  `AttachChannelDealerManual`. Must be set before attach; read-only after.
- `Spot.Subscribe(...)` returns a service-aware `TopicMessage`.
  `TopicMessage` therefore needs `ServiceName` populated for SPOT subscribe
  results and `null` for raw `SUB` / `XSUB`.
- `Spot` must not expose `OnSubscribe(...)`.
- `SpotDispatchEvent.SubscribeReadable` and `.RoutedReadable` are readiness
  notifications, not one-event-per-message delivery counters. Binding docs and
  samples must drain until the recv API reports `EAGAIN`.
- Peer weight is exposed only on `RouterSocket` and `DealerSocket` through
  typed option/property surfaces. The value range is `0..100`, default
  `100`; `0` drains new outbound selection. Submit
  attempts to a weight-`0` peer throw `ZlinkSubmitException` with `Code =
  SubmitResult.NotAdmitted`.
- `Pollout` is a send-recovery readiness signal, shared with
  `OnSendReady(...)`. It is not a "transport writable" bit.
- ROUTER / PUB socket option defaults follow the core header: `Mandatory =
  true`, `Handover = false`, `Nodrop = true`.
- Internal pairing rule: when auto-connect pairs two same-service ROUTERs
  via Discovery, the library picks one initiator per pair by a total order
  on `(routing_id, advertise endpoint)`. Users do not configure this.

## Core

### Context

Manages the lifecycle of IO threads and sockets.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class Context : IDisposable, IAsyncDisposable
{
    Context();

    ContextOptions Options { get; }

    /// <exception cref="ZlinkCloseException"/>
    void Shutdown();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

## Peer Disconnect by Routing ID

.NET bindings expose `DisconnectRid(RoutingId rid)` on raw sockets and
`DisconnectPeerRid(RoutingId targetNodeRid)` on `SpotNode`. The duplicate
policy option and `NotFound` / `Conflict` / `Busy` connect errors mirror the C
core. `Spot` does not expose a peer-rid disconnect method.

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
    bool AutoHwmEnabled { get; set; }
    /// <exception cref="ZlinkConfigException"/>
    int AutoHwmTotalMemoryBudgetMb { get; set; }

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
CommonSocketOptions Options { get; }
/// <exception cref="ZlinkBindException"/>
void Bind(string address);
/// <exception cref="ZlinkConnectException"/>
void Unbind(string address);
/// <exception cref="ZlinkConfigException"/>
SocketMonitor MonitorOpen(SocketEvent events = SocketEvent.All);
/// <exception cref="ZlinkConfigException"/>
// No common peer-weight accessor. Bindings expose weight only on
// RouterSocket and DealerSocket.
/// <exception cref="ZlinkConfigException"/>
void SetTlsServer(string certPath, string keyPath,
                  bool requireClientCert = false);
/// <exception cref="ZlinkConfigException"/>
void SetTlsClient(string caCertPath, string hostname,
                  bool trustSystem = false);
/// <exception cref="ZlinkConfigException"/>
void SetChannelName(string channelName);
/// <exception cref="ZlinkConfigException"/>
string GetChannelName();
/// <exception cref="ZlinkConfigException"/>
// Weight-bearing handles use typed option/property surfaces instead.
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

// Available on MessageSocketBase
/// <exception cref="ZlinkSubmitException"/>
bool Send(Message message, SendFlags flags = SendFlags.None);
/// <exception cref="ZlinkSubmitException"/>
bool Send(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
/// <exception cref="ZlinkRecvException"/>
Received? Recv(RecvFlags flags = RecvFlags.None);

// Available on RoutedMessageSocketBase
/// <exception cref="ZlinkSubmitException"/>
bool Send(string routingId, Message message, SendFlags flags = SendFlags.None);
/// <exception cref="ZlinkSubmitException"/>
bool Send(RoutingId routingId, Message message, SendFlags flags = SendFlags.None);
/// <exception cref="ZlinkSubmitException"/>
bool Send(string routingId, IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
/// <exception cref="ZlinkSubmitException"/>
bool Send(RoutingId routingId, IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
/// <exception cref="ZlinkRecvException"/>
Received? Recv(RecvFlags flags = RecvFlags.None);
```

`Send(...)` and `Publish(...)` return `false` only for temporary backpressure
when `SendFlags.DontWait` is used. Blocking submit returns `true` on success.
Route-not-ready and other submit failures still raise `ZlinkSubmitException`.
`Recv(...)` and `Subscribe(...)` return `null` when `RecvFlags.DontWait` finds
no message and still raise `ZlinkRecvException` for real recv failures.

The binding also exposes the following public infrastructure types:

- `IZlinkSocket`: public marker interface implemented by all socket handles.
  Used by `Zlink.Proxy(...)`, `Zlink.ProxySteerable(...)`, `Poller`, and
  `ZlinkPoll`.
- `SocketBase`, `ConnectableSocketBase`, `MessageSocketBase`,
  `PublisherSocketBase`, `SubscriberSocketBase`, `RoutedMessageSocketBase`,
  and `ConnectableRoutedMessageSocketBase`: public abstract inheritance
  carriers with internal constructors. They are marked
  `EditorBrowsable(EditorBrowsableState.Never)` and expose exactly the public
  instance members summarized in the base-method tables above.

### Option Facades

Typed option facades are part of the public surface. They do not expose raw
option ids.

```csharp
public sealed class CommonSocketOptions
{
    long MaxMessageSize { get; set; }
    int SendHighWaterMark { get; set; }
    int ReceiveHighWaterMark { get; set; }
    int SendBufferSize { get; set; }
    int ReceiveBufferSize { get; set; }
    TimeSpan? Linger { get; set; }
    TimeSpan? ReceiveTimeout { get; set; }
    TimeSpan? SendTimeout { get; set; }
    TimeSpan? ConnectTimeout { get; set; }
    TimeSpan? HandshakeInterval { get; set; }
    bool IPv6 { get; set; }
    bool TcpNoDelay { get; set; }
    bool Immediate { get; set; }
    string LastEndpoint { get; }
    int FileDescriptor { get; }
}

public sealed class DealerSocketOptions
{
    RoutingId RoutingId { get; set; }
    bool ProbeRouter { get; set; }
}

public sealed class RouterSocketOptions
{
    RoutingId RoutingId { get; set; }
    bool Mandatory { get; set; }
    bool Handover { get; set; }
    bool Probe { get; set; }
    RoutingId ConnectRoutingId { get; set; }
}

public sealed class StreamSocketOptions
{
    bool Notify { get; set; }
    RoutingId ConnectRoutingId { get; set; }
}

public sealed class XPubSocketOptions
{
    bool Verbose { get; set; }
    bool Verboser { get; set; }
    bool Manual { get; set; }
    bool ManualLastValue { get; set; }
    bool NoDrop { get; set; }
    string WelcomeMessage { get; set; }
    int TopicsCount { get; }
}

public sealed class PubSocketOptions
{
    bool Verbose { get; set; }
    bool Verboser { get; set; }
    bool Manual { get; set; }
    bool ManualLastValue { get; set; }
    bool NoDrop { get; set; }
    string WelcomeMessage { get; set; }
    int TopicsCount { get; }
}

public sealed class SubSocketOptions
{
    int TopicsCount { get; }
}

public sealed class SpotNodePublisherOptions
{
    int SendHighWaterMark { set; }
    TimeSpan? SendTimeout { set; }
    TimeSpan? Linger { set; }
    bool NoDrop { set; }
}

public sealed class SpotNodeSubscriberOptions
{
    int ReceiveHighWaterMark { set; }
    TimeSpan? ReceiveTimeout { set; }
    TimeSpan? Linger { set; }
}
```

### PairSocket

Bidirectional exclusive pair socket.

```csharp
public sealed class PairSocket : MessageSocketBase
{
    PairSocket(Context context);

    // inherited from MessageSocketBase
    /// <exception cref="ZlinkSubmitException"/>
    bool Send(Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool Send(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkRecvException"/>
    Received? Recv(RecvFlags flags = RecvFlags.None);
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

    PubSocketOptions PubOptions { get; }

    /// <exception cref="ZlinkConfigException"/>
    void AttachDiscovery(Discovery discovery);

    // inherited from PublisherSocketBase
    /// <exception cref="ZlinkSubmitException"/>
    bool Publish(string topic, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool Publish(string topic, IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
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

    SubSocketOptions SubOptions { get; }

    /// <exception cref="ZlinkConfigException"/>
    void AttachDiscovery(Discovery discovery);

    // inherited from SubscriberSocketBase
    /// <exception cref="ZlinkConfigException"/>
    void SetSubscription(string topicOrPattern);
    /// <exception cref="ZlinkConfigException"/>
    void UnsetSubscription(string topicOrPattern);
    /// <exception cref="ZlinkRecvException"/>
    TopicMessage? Subscribe(RecvFlags flags = RecvFlags.None);
}
```

### DealerSocket

Asynchronous client socket for fair-queued request distribution.

```csharp
public sealed class DealerSocket : MessageSocketBase
{
    DealerSocket(Context context);

    DealerSocketOptions DealerOptions { get; }

    /// <exception cref="ZlinkConfigException"/>
    void SetRoutingId(RoutingId routingId);
    /// <exception cref="ZlinkConfigException"/>
    RoutingId GetRoutingId();

    /// <exception cref="ZlinkConfigException"/>
    void AttachDiscovery(Discovery discovery);

    // inherited from MessageSocketBase
    /// <exception cref="ZlinkSubmitException"/>
    bool Send(Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool Send(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkRecvException"/>
    Received? Recv(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSendReady(Action handler);

    // --- request (async, blocking submit, no flags) ---
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestAsync(Message part, CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestAsync(Message part, TimeSpan timeout,
                                              CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestAsync(IReadOnlyList<Message> parts,
                                              CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestAsync(IReadOnlyList<Message> parts, TimeSpan timeout,
                                              CancellationToken ct = default);

    // --- request (callback submit) ---
    // Callback receives a RequestResult for the reply phase (see ZlinkRequestException / RequestResult).
    // The reply payload is delivered as an IReadOnlyList<Message> (empty list on failure).
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool Request(Message part,
                 Action<RequestResult, IReadOnlyList<Message>> callback,
                 SendFlags flags = SendFlags.None,
                 TimeSpan? timeout = null);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool Request(IReadOnlyList<Message> parts,
                 Action<RequestResult, IReadOnlyList<Message>> callback,
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

    RouterSocketOptions RouterOptions { get; }

    /// <exception cref="ZlinkConfigException"/>
    void AttachDiscovery(Discovery discovery);

    // inherited from RoutedMessageSocketBase
    /// <exception cref="ZlinkSubmitException"/>
    bool Send(string routingId, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool Send(RoutingId routingId, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool Send(string routingId, IReadOnlyList<Message> parts,
              SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool Send(RoutingId routingId, IReadOnlyList<Message> parts,
              SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkRecvException"/>
    Received? Recv(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSendReady(Action handler);

    // --- request (async, blocking submit, no flags) ---
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestAsync(RoutingId peerRid, Message part,
                                              CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestAsync(RoutingId peerRid, Message part, TimeSpan timeout,
                                              CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestAsync(RoutingId peerRid, IReadOnlyList<Message> parts,
                                              CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestAsync(RoutingId peerRid, IReadOnlyList<Message> parts,
                                              TimeSpan timeout, CancellationToken ct = default);

    // --- request (callback submit) ---
    // Callback receives a RequestResult for the reply phase (see ZlinkRequestException / RequestResult).
    // The reply payload is delivered as an IReadOnlyList<Message> (empty list on failure).
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool Request(RoutingId peerRid, Message part,
                 Action<RequestResult, IReadOnlyList<Message>> callback,
                 SendFlags flags = SendFlags.None,
                 TimeSpan? timeout = null);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool Request(RoutingId peerRid, IReadOnlyList<Message> parts,
                 Action<RequestResult, IReadOnlyList<Message>> callback,
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
    bool SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message message,
                    SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                    IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);

    // --- router -> spot routed request (async, blocking submit, no flags) ---
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestToSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                                    Message message, TimeSpan timeout = default,
                                                    CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestToSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                                    IReadOnlyList<Message> parts,
                                                    TimeSpan timeout = default,
                                                    CancellationToken ct = default);

    // --- router -> spot routed request (callback submit) ---
    // Callback receives a RequestResult for the reply phase (see ZlinkRequestException / RequestResult).
    // The reply payload is delivered as an IReadOnlyList<Message> (empty list on failure).
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       Message message,
                       Action<RequestResult, IReadOnlyList<Message>> callback,
                       SendFlags flags = SendFlags.None,
                       TimeSpan timeout = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       IReadOnlyList<Message> parts,
                       Action<RequestResult, IReadOnlyList<Message>> callback,
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

    // NOTE: RouterSocket 의 routed 수신 plane 은 단일 표면이다. 일반
    // ROUTER 트래픽과 spot-origin routed 트래픽을 모두 Recv 로 받는다.
    // `Received.RoutingId` 는 source_node_rid, `Received.SpotRid` 는
    // spot-origin 트래픽에서만 값이 있다. 별도의 RecvSpot /
    // OnSpotReceive 는 제공하지 않는다.
}
```

### XPubSocket

Extended publisher. Like PubSocket but also receives subscription events.

```csharp
public sealed class XPubSocket : PublisherSocketBase
{
    XPubSocket(Context context);

    XPubSocketOptions XPubOptions { get; }

    /// <exception cref="ZlinkRecvException"/>
    SubscriptionEvent ReceiveSubscriptionEvent(RecvFlags flags = RecvFlags.None);

    // inherited from PublisherSocketBase
    /// <exception cref="ZlinkSubmitException"/>
    bool Publish(string topic, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool Publish(string topic, IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
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

    SubSocketOptions SubOptions { get; }

    // inherited from SubscriberSocketBase
    /// <exception cref="ZlinkConfigException"/>
    void SetSubscription(string topicOrPattern);
    /// <exception cref="ZlinkConfigException"/>
    void UnsetSubscription(string topicOrPattern);
    /// <exception cref="ZlinkRecvException"/>
    TopicMessage? Subscribe(RecvFlags flags = RecvFlags.None);
}
```

### StreamSocket

Raw TCP stream socket. Bind-only; does not support `Connect`.

```csharp
public sealed class StreamSocket : RoutedMessageSocketBase
{
    StreamSocket(Context context);

    StreamSocketOptions StreamOptions { get; }

    /// <exception cref="ZlinkCloseException"/>
    void DetachStream();

    // inherited from RoutedMessageSocketBase
    /// <exception cref="ZlinkSubmitException"/>
    bool Send(string routingId, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool Send(RoutingId routingId, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool Send(string routingId, IReadOnlyList<Message> parts,
              SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool Send(RoutingId routingId, IReadOnlyList<Message> parts,
              SendFlags flags = SendFlags.None);
    /// Two mutually-exclusive receive modes on the same StreamSocket:
    ///   (1) Recv(), (2) OnPacket(handler). Second attach throws
    ///   ZlinkHandlerException(HandlerResult.Busy).
    /// <exception cref="ZlinkRecvException"/>
    Received? Recv(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnPacket(StreamPacketHandler handler);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSendReady(Action handler);
}
```

`StreamSocket` does not expose the legacy `uint routingId` send overloads or
framed packet callback overloads on its public contract. The canonical public
packet callback is `OnPacket(StreamPacketHandler handler)`.

```csharp
public delegate int StreamPacketHandler(string routingId, Message payload);
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
    // Public input adapters are copy-based only; borrowed external-wrap
    // APIs are intentionally not exposed on managed bindings.
    /// <exception cref="ZlinkConfigException"/>
    Message(ReadOnlySpan<byte> data);
    /// <exception cref="ZlinkConfigException"/>
    Message(ReadOnlyMemory<byte> data);

    // --- factories ---
    /// <exception cref="ZlinkConfigException"/>
    static Message FromBytes(byte[] data);
    /// <exception cref="ZlinkConfigException"/>
    static Message FromBytes(ReadOnlySpan<byte> data);
    /// <exception cref="ZlinkConfigException"/>
    static Message FromBytes(ReadOnlyMemory<byte> data);
    /// <exception cref="ZlinkConfigException"/>
    static Message FromSequence(ReadOnlySequence<byte> data);
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
    ReadOnlyMemory<byte> AsReadOnlyMemory();
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
    int CopyTo(IBufferWriter<byte> destination);
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

### Codec Extensions

The binding exposes separate codec extension libraries. The public assembly and
namespace names are fixed to:

- NuGet `Zlink.Codecs.Protobuf`
- NuGet `Zlink.Codecs.Json`
- NuGet `Zlink.Codecs.MessagePack`

- `Zlink.Codecs.Protobuf`
- `Zlink.Codecs.Json`
- `Zlink.Codecs.MessagePack`

These extensions are separate public modules layered on top of the core
binding. They must not be merged into the `Zlink` core assembly.

JSON codec baseline: `System.Text.Json`.
MessagePack codec baseline: `MessagePack for C#`.

```csharp
namespace Zlink.Codecs.Protobuf;

public static class ProtobufMessageExtensions
{
    T ParseProto<T>(this Message message)
        where T : Google.Protobuf.IMessage<T>, new();

    // compat alias
    Message ToMessage<T>(this T value)
        where T : Google.Protobuf.IMessage<T>;

    Message ToProtoMessage<T>(this T value)
        where T : Google.Protobuf.IMessage<T>;
}
```

```csharp
namespace Zlink.Codecs.Json;

public static class JsonMessageExtensions
{
    T ParseJson<T>(this Message message);

    // compat alias
    Message ToMessage<T>(this T value);

    Message ToJsonMessage<T>(this T value);
}
```

```csharp
namespace Zlink.Codecs.MessagePack;

public static class MessagePackMessageExtensions
{
    T ParseMessagePack<T>(this Message message);

    // compat alias
    Message ToMessage<T>(this T value);

    Message ToMessagePackMessage<T>(this T value);
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

Thrown by handler-registration calls (`OnPacket`, `OnSendReady`,
`OnEvent`, `OnFire`, `OnRoutedReceive`, `OnDispatchEvent`, etc.). Wraps a
`HandlerResult`.

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
    InternalError = 12,
    NotAdmitted = 13   // target peer has weight 0
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

Result code for handler registration operations (`OnPacket`,
`OnSendReady`, `OnRoutedReceive`, `OnDispatchEvent`, `OnEvent`, etc.).

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
public sealed class Received : IDisposable, IReadOnlyList<Message>
{
    RoutingId? RoutingId { get; }            // peer_rid (Router) / source_node_rid (Spot)
    RoutingId? SpotRid { get; }              // SPOT routed recv 에서만 값 있음
    ulong? RequestSeq { get; }               // null when not a request-reply recv
    IReadOnlyList<Message> Parts { get; }
    int Count { get; }
    Message this[int index] { get; }
    bool IsSinglePart { get; }

    /// <exception cref="ZlinkRecvException"/>
    Message FirstPart();
    /// <exception cref="ZlinkRecvException"/>
    Message SinglePartOrThrow();

    // Reply — RequestSeq 가 null 이 아니어야 함. null 또는 invalid reply
    // context 는 ZlinkSubmitException.
    /// <exception cref="ZlinkSubmitException"/>
    void Reply(Message part);
    /// <exception cref="ZlinkSubmitException"/>
    void Reply(Message part, SendFlags flags);
    /// <exception cref="ZlinkSubmitException"/>
    void Reply(IReadOnlyList<Message> parts);
    /// <exception cref="ZlinkSubmitException"/>
    void Reply(IReadOnlyList<Message> parts, SendFlags flags);

    /// <exception cref="ZlinkCloseException"/>
    void Dispose();

    IEnumerator<Message> GetEnumerator();
}
```

### TopicMessage

Topic-aware recv result used by SUB / XSUB / Spot subscribe paths.
Implements `IDisposable`.

```csharp
public sealed class TopicMessage : IDisposable
{
    RoutingId? RoutingId { get; }            // null when transport carries no source id
    string? ServiceName { get; }             // Spot subscribe only; null for raw SUB / XSUB
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

Reports a subscribe/unsubscribe event from XPub sockets and Spot
subscription event recv. Pure value object (no lifecycle). Defined as a record.

```csharp
public sealed record SubscriptionEvent(
    RoutingId? RoutingId,                    // null when transport carries no source id
    string? ServiceName,                     // Spot subscription event only; null for XPub
    string Topic,                            // UTF-8
    bool Subscribed);                        // true=subscribe, false=unsubscribe
```

### SpotDispatchEvent

Dispatch event kind delivered to `Spot.OnDispatchEvent`. Maps 1-to-1 to the
C API `zlink_spot_dispatch_event_t`.

```csharp
public enum SpotDispatchEvent
{
    SubscribeReadable    = 1,  // topic message ready — drain via Spot.Subscribe()
    RoutedReadable       = 2,  // routed message ready — drain via Spot.RecvRouted()
    TimerReadable        = 3,  // timer fired — drain via Timer.Recv(); Subject is Timer
    ChannelReplyReadable = 4   // channel reply ready — drain via Spot.DrainChannelReplyFrom(Subject)
}
```

### SpotDispatchSubjectKind

Subject kind accompanying a `SpotDispatchInfo`. Maps to the C API
`zlink_spot_dispatch_subject_kind_t`.

```csharp
public enum SpotDispatchSubjectKind
{
    Spot          = 1,   // Subject is the Spot itself (SubscribeReadable / RoutedReadable)
    Timer         = 2,   // Subject is a Timer handle (TimerReadable)
    ChannelDealer = 3    // Subject is a DealerSocket handle (ChannelReplyReadable)
}
```

### SpotDispatchInfo

Structured dispatch event info passed to `Spot.OnDispatchEvent`. Maps to
the C API `zlink_spot_dispatch_info_t`. Pure value object.

```csharp
public sealed record SpotDispatchInfo(
    SpotDispatchEvent        Event,
    SpotDispatchSubjectKind  SubjectKind,
    IntPtr                   Subject);   // source native handle; IntPtr.Zero when none
```

`Subject` is the source native handle. It is `IntPtr.Zero` only for
`SubscribeReadable` and `RoutedReadable` (where `SubjectKind == Spot`).
Pass the handle back to APIs that consume it, such as
`DrainChannelReplyFrom(IntPtr dealerSubject)`.

`SubscribeReadable` and `RoutedReadable` are readiness events. Callers must
drain `Spot.Subscribe(...)` or `Spot.RecvRouted(...)` until the binding
surfaces `EAGAIN` / no-data.

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

```csharp
[Flags]
public enum SocketEvent
{
    Connected = 0x0001,
    ConnectDelayed = 0x0002,
    ConnectRetried = 0x0004,
    Listening = 0x0008,
    BindFailed = 0x0010,
    Accepted = 0x0020,
    AcceptFailed = 0x0040,
    Closed = 0x0080,
    CloseFailed = 0x0100,
    Disconnected = 0x0200,
    MonitorStopped = 0x0400,
    HandshakeFailedNoDetail = 0x0800,
    ConnectionReady = 0x1000,
    HandshakeFailedProtocol = 0x2000,
    HandshakeFailedAuth = 0x4000,
    PeerWeightChanged = 0x8000,
    All = 0xFFFF
}

public enum MonitorEventType
{
    Connected = 0x0001,
    ConnectDelayed = 0x0002,
    ConnectRetried = 0x0004,
    Listening = 0x0008,
    BindFailed = 0x0010,
    Accepted = 0x0020,
    AcceptFailed = 0x0040,
    Closed = 0x0080,
    CloseFailed = 0x0100,
    Disconnected = 0x0200,
    MonitorStopped = 0x0400,
    HandshakeFailedNoDetail = 0x0800,
    ConnectionReady = 0x1000,
    HandshakeFailedProtocol = 0x2000,
    HandshakeFailedAuth = 0x4000,
    PeerWeightChanged = 0x8000
}
```

---

## Monitoring

### SocketMonitor

Socket-level event monitor. Receives connect, disconnect, and handshake events.
Implements `IDisposable` and `IAsyncDisposable`.
Starts in recv model. `OnEvent(...)` transitions one-way to callback-only
model; after that `Recv(...)` raises busy and `Snapshot()` still works.

```csharp
public sealed class SocketMonitor : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// No-op callback for callback-only model. Pass to OnEvent() to keep a
    /// valid handler when the application does not care about events; once
    /// installed the monitor is in callback-only model and Recv(...) raises
    /// busy (Snapshot() still works). To drive the monitor via Snapshot() /
    /// Recv(...) instead, leave the handler unset. Maps to
    /// zlink_monitor_ignore_handler.
    /// </summary>
    public static readonly Action<MonitorEvent> IgnoreHandler;

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

### MonitorEvent

Socket monitor event. Pure value object.

```csharp
public sealed record MonitorEvent(
    MonitorEventType Event,                  // CONNECTION_READY, CONNECTED, DISCONNECTED, PEER_WEIGHT_CHANGED, ...
    uint Value,                              // per-event detail (e.g. disconnect reason code, PEER_WEIGHT_CHANGED carries the new 0..100 weight)
    RoutingId? RoutingId,                    // null when event has no associated peer
    string LocalAddr,
    string RemoteAddr);
```

`MonitorEventType` includes `PeerWeightChanged` (bit 15). When this event
fires, `Value` carries the new `0..100` weight for the peer.

### MonitorSnapshot

Runtime snapshot of a socket monitor handle.

```csharp
public sealed class MonitorSnapshot
{
    SourceKind SourceKind { get; }           // monitor target kind
    uint StateFlags { get; }                 // state bitmask
    uint DetailFlags { get; }                // detail bitmask
    ulong SndPendingMsgs { get; }            // send-queue pending messages
    ulong RcvPendingMsgs { get; }            // recv-queue pending messages
    bool AutoHwmEnabled { get; }
    uint AutoHwmRole { get; }
    uint AutoHwmManagedConnections { get; }
    uint AutoHwmActiveHwmConnections { get; }
    uint AutoHwmObservedCount { get; }
    uint AutoHwmPlanningCount { get; }
    uint AutoHwmContextTotalPlanningCount { get; }
    uint AutoHwmBaseFloorPerConnection { get; }
    int AutoHwmAppliedSndHwm { get; }
    int AutoHwmAppliedRcvHwm { get; }
    int AutoHwmRequestedSndBuf { get; }
    int AutoHwmRequestedRcvBuf { get; }
    int AutoHwmEffectiveSndBuf { get; }
    int AutoHwmEffectiveRcvBuf { get; }
    ulong AutoHwmTotalMemoryBudgetBytes { get; }
    ulong AutoHwmQueueBudgetBytes { get; }
    ulong AutoHwmTransportBudgetBytes { get; }
    ulong AutoHwmRuntimeReserveBytes { get; }
    ulong AutoHwmSocketQueueShareBytes { get; }
    ulong AutoHwmSocketMessageSlots { get; }
    ulong AutoHwmEffectiveMessageBytes { get; }
    ulong AutoHwmEstimatedMaxMemoryBytes { get; }
    ulong AutoHwmLastRecalcMs { get; }
    uint AutoHwmLastRecalcReason { get; }
    uint AutoHwmSendBlockedRatioPpm { get; }
    uint AutoHwmScope { get; }
    uint AutoHwmScopeCount { get; }
    ulong AutoHwmAutoBufferBytes { get; }
    ulong AutoHwmManualBufferBytes { get; }
    uint AutoHwmBufferConnections { get; }
    int AutoHwmDeferredSndHwm { get; }
    int AutoHwmDeferredRcvHwm { get; }

    bool IsReady { get; }                    // raw socket monitor source의 ready bit
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
    RegistryTopologyEntry[] TopologyQuery(RegistryTopologyFilter? filter = null);
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
public enum DiscoveryDealerPeerMode { Router = 1, Dealer = 2 }

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

    /// <summary>
    /// Resolve the current owner node routing id for a logical spot routing id.
    /// Intended for send/request destination lookup. Maps to zlink_discovery_resolve_spot.
    /// </summary>
    /// <exception cref="ZlinkConfigException"/>
    RoutingId ResolveSpot(RoutingId spotRid);

    /// <summary>
    /// Set the auto-connect target policy used by DEALER sockets in this
    /// discovery view. Default is Router. Maps to zlink_discovery_set_dealer_peer_mode.
    /// </summary>
    /// <exception cref="ZlinkConfigException"/>
    void SetDealerPeerMode(DiscoveryDealerPeerMode mode);

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
    SpotNode(Context context, SpotNodeOptions options);

    // --- identity / routing ---
    /// <summary>
    /// Logical address for the SpotNode. Maps to zlink_set_routing_id(node, ...)
    /// / zlink_get_routing_id(node, ...).
    /// </summary>
    /// <exception cref="ZlinkConfigException"/>
    void SetRoutingId(RoutingId routingId);
    /// <exception cref="ZlinkConfigException"/>
    RoutingId RoutingId { get; }

    SpotNodePublisherOptions PublisherOptions { get; }
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
    void AttachChannelDealer(Discovery discovery, DealerSocket dealer);
    /// <exception cref="ZlinkConfigException"/>
    void AttachChannelDealerManual(string channelName, DealerSocket dealer);
    /// <exception cref="ZlinkConfigException"/>
    void AttachPubIngress(PubSocket pub);

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
    SpotNodeSocketSnapshotEntry[] InternalSocketsSnapshot(
        SpotNodeSocketSnapshotFilter? filter = null);
    // Spot 생성은 반드시 SpotNode 에서만
    /// <exception cref="ZlinkConfigException"/>
    Spot CreateSpot();

    // Close/Dispose cascades: live Spot 을 먼저 정리한 후 node 종료
    /// <exception cref="ZlinkCloseException"/>
    void Close();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

`SpotNode` 가 lifecycle 소유자. `Spot` 은 `SpotNode.CreateSpot()` factory
로만 생성한다. `new Spot(SpotNode)` 는 internal (public 생성자 아님).

### Spot

Spot messaging endpoint. Provides service-aware pub/sub and routed messaging.
Implements `IDisposable` and `IAsyncDisposable`. **`SpotNode.CreateSpot()`
로만 생성**.

```csharp
public sealed class Spot : IDisposable, IAsyncDisposable
{
    // Spot(SpotNode) constructor 는 internal. 사용자는 SpotNode.CreateSpot() 을 사용.

    // --- identity / routing ---
    /// <summary>
    /// Logical address / spot-level routed ownership key.
    /// Maps to zlink_set_routing_id(spot, ...) / zlink_get_routing_id(spot, ...).
    /// </summary>
    /// <exception cref="ZlinkConfigException"/>
    void SetRoutingId(RoutingId routingId);
    /// <exception cref="ZlinkConfigException"/>
    RoutingId RoutingId { get; }

    // --- channel-aware publish / request ---
    /// <exception cref="ZlinkSubmitException"/>
    bool Publish(string serviceName, string topic, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool Publish(string serviceName, string topic, IReadOnlyList<Message> parts,
                 SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool SendChannel(string channelName, Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool SendChannel(string channelName, IReadOnlyList<Message> parts,
                     SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestChannelAsync(string channelName, Message message,
                                                     TimeSpan timeout = default,
                                                     CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestChannelAsync(string channelName,
                                                     IReadOnlyList<Message> parts,
                                                     TimeSpan timeout = default,
                                                     CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool RequestChannel(string channelName, Message message,
                        Action<RequestResult, IReadOnlyList<Message>> callback,
                        SendFlags flags = SendFlags.None,
                        TimeSpan? timeout = null);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool RequestChannel(string channelName, IReadOnlyList<Message> parts,
                        Action<RequestResult, IReadOnlyList<Message>> callback,
                        SendFlags flags = SendFlags.None,
                        TimeSpan? timeout = null);

    // --- subscribe ---
    /// <exception cref="ZlinkConfigException"/>
    void SetSubscription(string topicOrPattern);
    /// <exception cref="ZlinkConfigException"/>
    void UnsetSubscription(string topicOrPattern);
    /// <exception cref="ZlinkRecvException"/>
    TopicMessage? Subscribe(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkRecvException"/>
    SubscriptionEvent ReceiveSubscriptionEvent(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSendReady(Action handler);

    // --- routed request (spot -> spot) ---
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestToSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                                    Message message,
                                                    TimeSpan timeout = default,
                                                    CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestToSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
                                                    IReadOnlyList<Message> parts,
                                                    TimeSpan timeout = default,
                                                    CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       Message message,
                       Action<RequestResult, IReadOnlyList<Message>> callback,
                       SendFlags flags = SendFlags.None,
                       TimeSpan? timeout = null);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       IReadOnlyList<Message> parts,
                       Action<RequestResult, IReadOnlyList<Message>> callback,
                       SendFlags flags = SendFlags.None,
                       TimeSpan? timeout = null);

    // --- routed request (spot -> router) ---
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestToRouterAsync(RoutingId peerRid,
                                                      Message message,
                                                      TimeSpan timeout = default,
                                                      CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestToRouterAsync(RoutingId peerRid,
                                                      IReadOnlyList<Message> parts,
                                                      TimeSpan timeout = default,
                                                      CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool RequestToRouter(RoutingId peerRid,
                         Message message,
                         Action<RequestResult, IReadOnlyList<Message>> callback,
                         SendFlags flags = SendFlags.None,
                         TimeSpan? timeout = null);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool RequestToRouter(RoutingId peerRid,
                         IReadOnlyList<Message> parts,
                         Action<RequestResult, IReadOnlyList<Message>> callback,
                         SendFlags flags = SendFlags.None,
                         TimeSpan? timeout = null);

    // --- routed reply (spot -> spot) ---
    /// <exception cref="ZlinkSubmitException"/>
    void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     ulong requestSequence, Message message,
                     SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     ulong requestSequence, IReadOnlyList<Message> parts,
                     SendFlags flags = SendFlags.None);

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
    /// <summary>
    /// Register the unified dispatch event handler. info.Subject is the source
    /// native handle for the event.
    /// Maps to zlink_spot_set_dispatch_event_handler with zlink_spot_dispatch_info_t.
    /// </summary>
    /// <exception cref="ZlinkHandlerException"/>
    void OnDispatchEvent(Action<Spot, SpotDispatchInfo> handler);

    // --- channel reply dispatch ---
    /// <summary>
    /// Drain pending channel reply completions from the given attached dealer.
    /// Call only from within an OnDispatchEvent callback when event is
    /// SpotDispatchEvent.ChannelReplyReadable. info.Subject is the dealer handle.
    /// Maps to zlink_spot_channel_reply_progress_from(spot, dealer).
    /// </summary>
    /// <exception cref="ZlinkConfigException"/>
    void DrainChannelReplyFrom(IntPtr dealerSubject);

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

Primary entry types used in the default service flow:

#### MemberPeerEntry

Discovery / registry member peer entry.

```csharp
public sealed record MemberPeerEntry(
    ServiceType ServiceType,
    ServiceRole ServiceRole,
    string ServiceName,
    string Endpoint,
    RoutingId? RoutingId,
    long Value,
    uint Weight);
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

Advanced / Diagnostic entry types and filters:

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

#### SpotNodePeerEntry

Spot node peer entry.

```csharp
public sealed record SpotNodePeerEntry(
    string ServiceName,
    string LocalEndpoint,
    string PeerEndpoint,
    SpotPeerSource Source,
    SpotPeerState State,
    uint Weight,
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

public sealed record RegistryServiceSummaryFilter(
    ServiceKind? ServiceKind = null,
    ServiceRole? ServiceRole = null,
    string? ServiceName = null);

public sealed record RegistryTopologyFilter(
    ServiceKind? ServiceKind = null,
    ServiceRole? ServiceRole = null,
    string? ServiceName = null,
    RoutingId? RoutingId = null,
    TopologyState? State = null,
    TopologySource? Source = null);

public sealed record SpotNodePeerFilter(
    string? PeerEndpoint = null,
    SpotPeerSource? Source = null,
    SpotPeerState? State = null);

public sealed record SpotNodeSubjectFilter(
    SpotRole? Role = null,
    string? Subject = null,
    SubjectKind? SubjectKind = null);
```

---

## Poller

### Poller

Event poller for multiplexing socket and file descriptor readiness.
Implements `IDisposable` and `IAsyncDisposable`.

The current public poller contract is still generic. It does not yet expose a
Spot-aware result carrying owner `Spot`, dispatch event kind, and drain
subject together.

```csharp
public sealed class Poller : IDisposable, IAsyncDisposable
{
    Poller();

    int Count { get; }

    /// <summary>Number of registered pollable items. Maps to zlink_poller_size.</summary>
    /// <exception cref="ZlinkConfigException"/>
    int Size { get; }

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
             out int totalReady);

    /// <exception cref="ZlinkConfigException"/>
    void Clear();
    /// <exception cref="ZlinkCloseException"/>
    void Close();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

```csharp
public readonly struct PollEvent
{
    IZlinkSocket? Socket { get; }
    int? Fd { get; }
    object? Tag { get; }
    PollEvents Events { get; }
    PollEvents Revents { get; }
}
```

```csharp
public enum PollEvents
{
    None = 0,
    PollIn = 1,
    PollOut = 2,
    PollErr = 4,
    PollPri = 8
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
    static int Poll(IReadOnlyList<IZlinkSocket> sockets,
                    IReadOnlyList<PollEvents> events,
                    Span<PollEvents> revents,
                    int timeoutMs);
    /// <exception cref="ZlinkRecvException"/>
    static int Poll(IReadOnlyList<SocketMonitor> monitors, int timeoutMs);
    /// <exception cref="ZlinkRecvException"/>
    static int Poll(IReadOnlyList<SocketMonitor> monitors,
                    IReadOnlyList<PollEvents> events,
                    Span<PollEvents> revents,
                    int timeoutMs);
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
    // Zlink.Errno() is NOT public. Access internal errno through
    // ZlinkException.InternalErrno on the caught exception.

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

    void Dispose();
    ValueTask DisposeAsync();
}
```

### ZlinkThread

Background thread wrapper over `System.Threading.Thread`.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class ZlinkThread : IDisposable, IAsyncDisposable
{
    ZlinkThread(Action task);

    /// Wait for the thread to finish and release its handle.
    void Join();
    void Close();

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
