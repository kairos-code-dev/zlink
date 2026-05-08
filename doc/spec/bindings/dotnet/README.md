[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# .NET Binding Specification

This document defines the complete public API surface of the .NET binding.
Every class, its purpose, and all public method signatures are listed.
Internal helpers and implementation details are omitted.

All types live in the `Systems.Zlink` namespace. The canonical NuGet package
id is `Systems.Zlink`.

This README is the normative specification and implementation baseline for
the .NET binding public surface. Binding implementation work must use this
document as the target contract and update the library to match it. If the
current binding implementation differs from this document, the implementation
must be corrected unless this specification is first amended against the core
public contract.

Only the public types and members listed in this document are part of the
contract. `internal` types, `Systems.Zlink.Sockets.Internal`, native interop
helpers, and other assembly-private helpers are not public API. Perf and
sample projects must compile against the public assembly surface only.
`InternalsVisibleTo` may be used for tests during development, but it does not
change the public contract.

## Design Basis

The .NET binding follows the repository POSD design policy. Public classes
must hide native sequencing, ownership, and option encoding behind typed,
deep interfaces so callers do not need core implementation details.

The public .NET surface must model stable domain concepts, not native interop
steps. Public types are justified when they own context/socket lifetime,
message ownership, receive metadata, service membership, callbacks, or typed
options. `SafeHandle` management, P/Invoke names, part-loop sequencing, request
tokens, callback userdata, and raw option encoding stay inside internal
implementation classes.

Design review uses these POSD constraints:

- shared rules for send/recv, nonblocking behavior, disposal, and error
  mapping live in one internal owner rather than being copied across socket
  classes
- canonical result and facade methods do not ask callers to pass state already
  captured by the object, such as a source socket, request sequence, or bound
  service address
- compatibility shims, if retained, are clearly outside the canonical API and
  are not used by new docs, samples, or tests
- a public class that only forwards to a native method without adding
  validation, ownership, lifetime, or result-shape semantics is too shallow and
  must be removed or made internal

## High-Performance Requirements

The .NET binding is part of a high-performance messaging library. Hot paths
must not use reflection, dynamic invocation, repeated boxing, unnecessary
allocation, avoidable buffer copies, coarse lock contention, hidden waits,
sleeps, busy waits, or thread joins. Native interop code must construct managed
`Message` and result objects directly from the core `*_part` substrate and
must not create native aggregate arrays only to copy them into managed
collections.

## Core Capability Rule

`core/include/zlink.h` is the source of truth for core capabilities. The .NET
binding must map every stable, user-facing core capability into this public
contract. The public signatures in the sections below are the canonical .NET
contract. Native coverage is tracked separately in the
[Core Capability Coverage Appendix](#core-capability-coverage-appendix) so the
public API and native checklist do not duplicate each other's authority.

## Core Alignment Rules

The detailed sections below are the canonical .NET binding contract. This
section states cross-cutting constraints once so the per-type API lists can
stay focused on signatures.

- `PairSocket`, `DealerSocket`, and `RouterSocket` keep their documented send,
  recv, request, and reply methods, but they do not expose direct data-plane
  receive callbacks such as `OnReceive(...)`.
- `SubSocket` and `XSubSocket` are receive-only topic sockets and do not
  expose direct topic callbacks such as `OnSubscribe(...)`.
- `StreamSocket` keeps `Recv(...)` and exposes the packet callback surface
  `OnPacket(...)`, mapped to `zlink_stream_packet_handler()`.
- `SpotNode` exposes channel-aware attachment methods:
  `AttachDiscovery(...)`, `AttachChannelDealer(...)`,
  `AttachChannelDealerManual(...)`, and `AttachPubIngress(...)`.
- `AttachPubIngress(...)` attaches an external raw `PUB` as an ingress source
  for the node's SPOT topic plane. It is not the implementation path for
  `Spot.Publish(...)`.
- `Spot` exposes data-plane operation builders:
  `Publish(serviceName, topic)` enters the SPOT topic plane,
  `SendChannel(...)` and `RequestChannel(...)` use attached channel
  `DEALER` handles, and `SendToSpot(...)` uses routed SPOT delivery.
- `Spot.Publish(...)` does not expose or select a raw `PUB` socket. Core
  admits the publish into the `SpotNode` topic data-plane; the binding keeps
  the native part-by-part substrate internal.
- `Spot.Subscribe(...)` returns a service-aware `TopicMessage`.
  `TopicMessage.ServiceName` is populated for SPOT subscribe results and is
  `null` for raw `SUB` / `XSUB`.
- `Spot` does not expose `OnSubscribe(...)`. Topic readiness is reported
  through `OnDispatchEvent(...)`, then callers drain with `Subscribe(...)`,
  routed recv, or timer recv as appropriate.
- `SpotDispatchEvent.SubscribeReadable` and `.RoutedReadable` are readiness
  notifications, not one-event-per-message delivery counters. Callers drain
  until the receive path reports no data.
- `Spot.OnRoutedReceive(...)` and `Spot.OnDispatchEvent(...)` are mutually
  exclusive on the routed axis.
- Peer weight is exposed only on `RouterSocket` and `DealerSocket` through
  typed option/property surfaces. The value range is `0..100`, default `100`;
  `0` drains new outbound selection. Submit attempts to a weight-`0` peer
  throw `ZlinkSubmitException` with
  `ZlinkSubmitException.ErrorCode.NotAdmitted`.
- `PollEventFlags.PollOut` is a send-recovery readiness signal, shared with
  `OnSendReady(...)`. It is not a "transport writable" bit.
- ROUTER / PUB socket option defaults follow the core header:
  `Mandatory = true`, `Handover = false`, and `NoDrop = true`.
- SPOT admission HWM defaults follow the core header. Router and pubsub
  admission profile/numeric options are exposed; relay and delivery HWM stay
  `0` and are not public .NET options.
- When Discovery auto-connect pairs two same-service ROUTERs, the library
  chooses one initiator per pair by total order on
  `(routingId, advertiseEndpoint)`. Users do not configure this.

## Boundary Validation Rules

The .NET binding validates fixed-size native boundary values before calling
core and never truncates user input.

- `RoutingId` validates its `1..255` byte size when the value object is
  created.
- Actor ids are non-empty UTF-8 strings up to 255 bytes and must not contain
  NUL.
- Endpoint strings passed to `Bind`, `Connect`, `Disconnect`, registry, and
  peer-connect APIs must fit in 255 UTF-8 bytes and must not contain NUL.
- SPOT `serviceName` values must fit in 255 UTF-8 bytes and must not contain
  NUL.
- Raw topic and subscription filter strings must fit in `0..255` UTF-8 bytes
  and must not contain NUL. Empty raw topics and filters remain valid because
  raw PUB/SUB uses the empty filter to mean "match all".
- SPOT topic and subscription filter strings must fit in `1..255` UTF-8 bytes
  and must not contain NUL.
- `TimeSpan` values converted to native milliseconds or nanoseconds must be in
  range. Overflow, negative values where core does not accept them, and lossy
  truncation raise .NET argument exceptions before the native call.

## Actor Dispatch Public Surface

.NET exposes Actor dispatch through public types in the zlink assembly. Actor
dispatch is a separate service-layer capability, not a subsection of SPOT.

```csharp
public readonly struct ActorRef : IEquatable<ActorRef> { ... }
public sealed record ActorCreateResult(ActorCreateStatus Status,
                                       ActorRef Actor);
public sealed record ActorRoute(ActorRef Actor, bool Joined,
                                RoutingId? JoinedSpotRid);
public sealed record ActorRecvInfo(ActorRef Actor, RoutingId SourceNodeRid,
                                   RoutingId SourceSessionRid, uint Flags);
public sealed record ActorJoinInfo(ActorRef SourceActor,
                                   ActorRef TargetActor,
                                   RoutingId SourceNodeRid,
                                   RoutingId SourceSpotRid,
                                   RoutingId TargetNodeRid,
                                   RoutingId TargetSpotRid,
                                   ulong JoinEpoch,
                                   uint Flags);
public sealed record ActorPart(ActorRecvInfo Info, Message Message,
                               bool More);
public sealed class ActorJoinRequest { ... }
public sealed class Actor : IDisposable, IAsyncDisposable { ... }
```

`SpotNode` exposes Actor factory/lookup, unchecked remote refs, remote
create-or-get, ref-based destroy/join/leave, admission, and Actor snapshots.
`Actor` owns handle-based join/leave, destroy, receive, and bound-session
operations. `Spot` exposes Actor join receive/reply and joined Actor snapshots.
`StreamSocket` exposes Actor bind/unbind and bound Actor send. `Discovery`
exposes Actor route resolve.

`Generation == 0` is an unchecked remote ref and is not invalid.
`SpotNode.RemoteActorRef(nodeRid, actorId)` is the unchecked remote-ref factory.
The public contract does not expose raw native Actor pointers.
`ActorJoinRequest` carries only public join metadata and the join message; the
native reply context stays inside the binding and is consumed by
`Spot.ReplyActorJoin(...)`.

## Out Of Scope

Higher-level runtime APIs, channel builder names, and application callback
interfaces are outside this binding API specification. They may depend on this
assembly, but their public contract belongs in a separate runtime spec. This
document only defines the lower-level .NET binding surface.

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
    /// <exception cref="ZlinkConfigException"/>
    void RecalculateAutoHwm();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

## Peer Disconnect by Routing ID

.NET bindings expose `DisconnectRid(RoutingId rid)` on connectable raw sockets
and `DisconnectPeerRid(RoutingId targetNodeRid)` on `SpotNode`. The duplicate
policy option and `NotFound` / `Conflict` / `Busy` connect errors mirror the C
core. `StreamSocket` and `Spot` do not expose peer-rid disconnect methods.

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
    AutoHwmProfile AutoHwmProfile { get; set; }
    /// <exception cref="ZlinkConfigException"/>
    bool AutoHwmEnabled { get; set; }
    /// <exception cref="ZlinkConfigException"/>
    TimeSpan AutoHwmRecalcDebounce { get; set; }
    /// <exception cref="ZlinkConfigException"/>
    string ThreadNamePrefix { get; set; }

    /// <exception cref="ZlinkConfigException"/>
    void AddThreadAffinityCpu(int cpu);
    /// <exception cref="ZlinkConfigException"/>
    void RemoveThreadAffinityCpu(int cpu);
}
```

The native context memory-budget and bootstrap auto-HWM options are
deprecated no-op compatibility options. The .NET binding does not expose
typed properties for them. `AutoHwmRecalcDebounce` remains public because it
controls the minimum debounce window before connection churn triggers another
automatic HWM recalculation.

`ThreadNamePrefix` is encoded as UTF-8 and must fit in 16 bytes. `null` is not
valid. The empty string is valid and clears the prefix. The property stores the
last value set by the managed binding; the native public API does not expose a
string getter for this option.

```csharp
public enum AutoHwmProfile
{
    Compact = 0,
    LowLatency = 1,
    Balanced = 2,
    Throughput = 3
}
```

---

## Socket Types

### Socket Capability Surfaces

These capability surfaces are the canonical definitions for inherited socket
members. Concrete socket sections list constructors and members introduced by
that concrete type; they do not repeat inherited signatures.

```csharp
// Available on all socket types (SocketBase)
CommonSocketOptions Options { get; }
/// <exception cref="ZlinkBindException"/>
void Bind(string address);
/// <exception cref="ZlinkConnectException"/>
void Unbind(string address);
/// <exception cref="ZlinkConfigException"/>
SocketMonitor MonitorOpen(SocketEvent events = SocketEvent.All);
// No common peer-weight accessor. Bindings expose peer weight only on
// RouterSocket and DealerSocket.
/// <exception cref="ZlinkConfigException"/>
void SetTlsServer(string certPath, string keyPath,
                  bool requireClientCert = false);
/// <exception cref="ZlinkConfigException"/>
void SetTlsClient(string caCertPath, string hostname,
                  bool trustSystem = false);
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
/// <exception cref="ZlinkConnectException"/>
void DisconnectRid(RoutingId rid);

// Available on MessageSocketBase
/// <exception cref="ZlinkSubmitException"/>
bool Send(Message message, SendFlags flags = SendFlags.None);
/// <exception cref="ZlinkSubmitException"/>
bool Send(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
/// <exception cref="ZlinkRecvException"/>
Received? Recv(RecvFlags flags = RecvFlags.None);

// Available on PublisherSocketBase
/// <exception cref="ZlinkSubmitException"/>
bool Publish(string topic, Message message, SendFlags flags = SendFlags.None);
/// <exception cref="ZlinkSubmitException"/>
bool Publish(string topic, IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);

// Available on SubscriberSocketBase
/// <exception cref="ZlinkConfigException"/>
void SetSubscription(string topicOrPattern);
/// <exception cref="ZlinkConfigException"/>
void UnsetSubscription(string topicOrPattern);
/// <exception cref="ZlinkConfigException"/>
SubscriptionEntry? SubscriptionAt(int index);
/// <exception cref="ZlinkRecvException"/>
TopicMessage? Subscribe(RecvFlags flags = RecvFlags.None);

// Available on RoutedMessageSocketBase
/// <exception cref="ZlinkSubmitException"/>
bool Send(RoutingId routingId, Message message, SendFlags flags = SendFlags.None);
/// <exception cref="ZlinkSubmitException"/>
bool Send(RoutingId routingId, IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
/// <exception cref="ZlinkRecvException"/>
Received? Recv(RecvFlags flags = RecvFlags.None);

// Available on send-capable socket handles.
/// <exception cref="ZlinkHandlerException"/>
void OnSendReady(Action handler);
```

`Send(...)` and `Publish(...)` return `false` only for temporary backpressure
when `SendFlags.DontWait` is used. Blocking submit returns `true` on success.
Route-not-ready and other submit failures still raise `ZlinkSubmitException`.
`Recv(...)`, `RecvRouted(...)`, `Subscribe(...)`, and
`ReceiveSubscriptionEvent(...)` return `null` when `RecvFlags.DontWait` finds
no data and still raise `ZlinkRecvException` for real recv failures.
`SocketMonitor.Recv(...)` and `Timer.Recv(...)` follow the same no-data rule.

The binding also exposes the following public infrastructure types:

- `IZlinkSocket`: public marker interface implemented by all socket handles.
  Used by `Zlink.Proxy(...)`, `Zlink.ProxySteerable(...)`, and `Poller`.
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
public class CommonSocketOptions
{
    TimeSpan? Linger { get; set; }
    int SendHighWaterMark { get; set; }
    int ReceiveHighWaterMark { get; set; }
    TimeSpan? SendTimeout { get; set; }
    TimeSpan? ReceiveTimeout { get; set; }
    bool Immediate { get; set; }
    TimeSpan? ConnectTimeout { get; set; }
    bool IPv6 { get; set; }
    bool TcpNoDelay { get; set; }
    int TcpKeepAlive { get; set; }          // -1 = OS default, 0 = off, 1 = on
    TimeSpan? HeartbeatInterval { get; set; }
    TimeSpan? HeartbeatTtl { get; set; }
    TimeSpan? HeartbeatTimeout { get; set; }
    RidDuplicatePolicy RoutingIdDuplicatePolicy { get; set; }
    long MaxMessageSize { get; set; }
    int AutoHwmMessageUnitBytes { get; set; }
    int Backlog { get; set; }
    TimeSpan? ReconnectInterval { get; set; }
    TimeSpan? ReconnectIntervalMax { get; set; }
    string LastEndpoint { get; }
}

public sealed class DealerSocketOptions : CommonSocketOptions
{
    bool Probe { set; }
    TimeSpan? RequestTimeout { set; }
    int PeerWeight { set; }
}

public sealed class RouterSocketOptions : CommonSocketOptions
{
    bool Mandatory { get; set; }
    bool Handover { get; set; }
    bool Probe { get; set; }
    RoutingId? ConnectRoutingId { get; }
    void SetConnectRoutingId(RoutingId routingId);
    TimeSpan? RequestTimeout { get; set; }
    int PeerWeight { get; set; }
}

public sealed class StreamSocketOptions : CommonSocketOptions
{
    bool Notify { get; set; }
}

public sealed class PubSocketOptions : CommonSocketOptions
{
    bool Verbose { get; set; }
    bool Verboser { get; set; }
    bool Manual { get; set; }
    bool ManualLastValue { get; set; }
    bool NoDrop { get; set; }
    Message WelcomeMessage { get; set; }
    int TopicsCount { get; }
    void ApproveSubscribe(RoutingId routingId);
    void RejectSubscribe(RoutingId routingId);
}

public sealed class SubSocketOptions : CommonSocketOptions
{
    int TopicsCount { get; }
}

public enum RidDuplicatePolicy
{
    Reject = 0,
    Handover = 1
}

public enum SocketType
{
    Any = 0,
    Pair = 0x1001,
    Pub = 0x1002,
    Sub = 0x1003,
    Dealer = 0x1004,
    Router = 0x1005,
    XPub = 0x1006,
    XSub = 0x1007,
    Stream = 0x1008
}

public enum SpotNodeMode
{
    PubSub = 1,
    Routed = 2,
    All = 3
}

```

`SpotNodeMode` selects the native node shape. Live HWM and dispatch worker
settings are exposed directly on `SpotNode` so callers do not need to route
through a shallow option facade. The default constructor uses
`SpotNodeMode.All`.
`DispatchWorkersMin` and `DispatchWorkersMax` configure the `SpotNode`-owned
application callback worker pool. They do not configure context IO threads or
the SPOT data-plane thread. `DispatchWorkersMin` must be at least `1`, and
`DispatchWorkersMax` must be greater than or equal to `DispatchWorkersMin`.
When not set explicitly, a single-CPU process uses `min=max=1`; otherwise the
defaults are `min=2` and `max=Environment.ProcessorCount`.

`DealerSocketOptions.RequestTimeout` and `DealerSocketOptions.PeerWeight` are
set-only because the core API exposes `zlink_set_dealer_option(...)` but does
not expose a matching `zlink_get_dealer_option(...)`.

After a socket is attached to `Discovery` through `AttachDiscovery(...)`, the
Discovery handle owns that participant's lifecycle. Manual `Connect(...)`,
`Disconnect(...)`, `DisconnectRid(...)`, `Unbind(...)`, and `Close(...)` calls
on the attached socket raise the corresponding zlink exception with the native
busy/lifecycle error.

### PairSocket

Bidirectional exclusive pair socket.

```csharp
public sealed class PairSocket : MessageSocketBase
{
    PairSocket(Context context);
}
```

### PubSocket

Publisher socket. Sends topic-prefixed messages to all matching subscribers.

```csharp
public sealed class PubSocket : PublisherSocketBase
{
    PubSocket(Context context);

    PubSocketOptions Options { get; }

    /// <exception cref="ZlinkConfigException"/>
    void AttachDiscovery(Discovery discovery);
}
```

### SubSocket

Subscriber socket. Receives topic-filtered messages from publishers.

```csharp
public sealed class SubSocket : SubscriberSocketBase
{
    SubSocket(Context context);

    SubSocketOptions Options { get; }

    /// <exception cref="ZlinkConfigException"/>
    void AttachDiscovery(Discovery discovery);
}
```

### DealerSocket

Asynchronous client socket for fair-queued request distribution.

```csharp
public sealed class DealerSocket : MessageSocketBase
{
    DealerSocket(Context context);

    DealerSocketOptions Options { get; }

    /// <exception cref="ZlinkConfigException"/>
    void SetRoutingId(RoutingId routingId);
    /// <exception cref="ZlinkConfigException"/>
    RoutingId GetRoutingId();

    /// <exception cref="ZlinkConfigException"/>
    void AttachDiscovery(Discovery discovery);
    /// <exception cref="ZlinkConfigException"/>
    void SetChannelName(string channelName);
    /// <exception cref="ZlinkConfigException"/>
    string GetChannelName();

    // --- request (Task, blocking submit, no flags) ---
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> Request(Message part, CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> Request(Message part, TimeSpan timeout,
                                         CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> Request(IReadOnlyList<Message> parts,
                                         CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> Request(IReadOnlyList<Message> parts, TimeSpan timeout,
                                         CancellationToken ct = default);

    // --- request (callback submit) ---
    // Callback receives a RequestResult for the reply phase (see ZlinkRequestException / RequestResult).
    // The reply payload is delivered as an IReadOnlyList<Message> (empty list on failure).
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool Request(Message part,
                 RequestCallback callback,
                 SendFlags flags = SendFlags.None,
                 TimeSpan? timeout = null);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool Request(IReadOnlyList<Message> parts,
                 RequestCallback callback,
                 SendFlags flags = SendFlags.None,
                 TimeSpan? timeout = null);
}
```

`DealerSocket.Request(...)` requires every connected peer to be a ROUTER. If a
Dealer is connected to a mixture of ROUTER and DEALER peers, request can fail.
The binding does not try to infer or validate peer socket types at runtime; the
application is responsible for keeping this topology valid.

### RouterSocket

Server socket that routes messages to specific peers by routing id.

```csharp
public sealed class RouterSocket : ConnectableRoutedMessageSocketBase
{
    RouterSocket(Context context);

    RouterSocketOptions Options { get; }

    /// <exception cref="ZlinkConfigException"/>
    void SetRoutingId(RoutingId routingId);
    /// <exception cref="ZlinkConfigException"/>
    RoutingId GetRoutingId();

    /// <exception cref="ZlinkConfigException"/>
    void AttachDiscovery(Discovery discovery);

    // --- request (Task, blocking submit, no flags) ---
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> Request(RoutingId peerRid, Message part,
                                         CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> Request(RoutingId peerRid, Message part, TimeSpan timeout,
                                         CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> Request(RoutingId peerRid, IReadOnlyList<Message> parts,
                                         CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> Request(RoutingId peerRid, IReadOnlyList<Message> parts,
                                         TimeSpan timeout, CancellationToken ct = default);

    // --- request (callback submit) ---
    // Callback receives a RequestResult for the reply phase (see ZlinkRequestException / RequestResult).
    // The reply payload is delivered as an IReadOnlyList<Message> (empty list on failure).
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool Request(RoutingId peerRid, Message part,
                 RequestCallback callback,
                 SendFlags flags = SendFlags.None,
                 TimeSpan? timeout = null);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool Request(RoutingId peerRid, IReadOnlyList<Message> parts,
                 RequestCallback callback,
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

    // --- router -> spot routed request (Task, blocking submit, no flags) ---
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                                               Message message, TimeSpan timeout = default,
                                               CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed (timeout, peer terminated, etc.).</exception>
    Task<IReadOnlyList<Message>> RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                                               IReadOnlyList<Message> parts,
                                               TimeSpan timeout = default,
                                               CancellationToken ct = default);

    // --- router -> spot routed request (callback submit) ---
    // Callback receives a RequestResult for the reply phase (see ZlinkRequestException / RequestResult).
    // The reply payload is delivered as an IReadOnlyList<Message> (empty list on failure).
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       Message message,
                       RequestCallback callback,
                       SendFlags flags = SendFlags.None,
                       TimeSpan? timeout = null);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       IReadOnlyList<Message> parts,
                       RequestCallback callback,
                       SendFlags flags = SendFlags.None,
                       TimeSpan? timeout = null);

    // --- router -> spot routed reply ---
    /// <exception cref="ZlinkSubmitException"/>
    void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     ulong requestSeq, Message message,
                     SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     ulong requestSeq, IReadOnlyList<Message> parts,
                     SendFlags flags = SendFlags.None);

    // RouterSocket has one routed receive surface. Recv receives both regular
    // ROUTER traffic and spot-origin routed traffic. Received.RoutingId carries
    // the source node routing id. Received.SpotRid is populated only for
    // spot-origin traffic. No separate RecvSpot or OnSpotReceive API exists.
}
```

### XPubSocket

Extended publisher. Like PubSocket but also receives subscription events.

```csharp
public sealed class XPubSocket : PublisherSocketBase
{
    XPubSocket(Context context);

    PubSocketOptions Options { get; }

    /// <exception cref="ZlinkRecvException"/>
    SubscriptionEvent? ReceiveSubscriptionEvent(RecvFlags flags = RecvFlags.None);
}
```

### XSubSocket

Extended subscriber. Like SubSocket with raw subscription forwarding.

```csharp
public sealed class XSubSocket : SubscriberSocketBase
{
    XSubSocket(Context context);

    SubSocketOptions Options { get; }
}
```

### StreamSocket

Raw TCP stream socket. Bind-only; does not support `Connect`,
`Disconnect`, or `DisconnectRid`.

```csharp
public sealed class StreamSocket : RoutedMessageSocketBase
{
    StreamSocket(Context context);

    StreamSocketOptions Options { get; }

    /// <exception cref="ZlinkConfigException"/>
    void SetRoutingId(RoutingId routingId);
    /// <exception cref="ZlinkConfigException"/>
    RoutingId GetRoutingId();

    /// Two mutually-exclusive receive modes on the same StreamSocket:
    ///   (1) Recv(), (2) OnPacket(handler). Second attach throws
    ///   ZlinkHandlerException(ZlinkHandlerException.ErrorCode.Busy).
    /// <exception cref="ZlinkHandlerException"/>
    void OnPacket(StreamPacketHandler handler);

    /// <exception cref="ZlinkRequestException"/>
    void BindActor(SpotNode node, RoutingId sessionRid, ActorRef actor,
                   TimeSpan timeout = default);
    /// <exception cref="ZlinkRequestException"/>
    void UnbindActor(SpotNode node, RoutingId sessionRid, string actorId,
                     TimeSpan timeout = default);
    /// <exception cref="ZlinkSubmitException"/>
    bool SendBoundActor(SpotNode node, RoutingId sessionRid, string actorId,
                        Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool SendBoundActor(SpotNode node, RoutingId sessionRid, string actorId,
                        IReadOnlyList<Message> parts,
                        SendFlags flags = SendFlags.None);
}
```

`StreamSocket` does not expose the legacy `uint routingId` send overloads or
raw direct callback overloads on its public contract. The canonical public
packet callback is `OnPacket(StreamPacketHandler handler)`, mapped to
`zlink_stream_packet_handler(...)`.

```csharp
public delegate void StreamPacketHandler(RoutingId routingId,
                                         Message header,
                                         Message body);
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

    /// <exception cref="ZlinkConfigException"/>
    void Dispose();
    /// <exception cref="ZlinkConfigException"/>
    ValueTask DisposeAsync();
}
```

### Codec Extensions

Codec adapters are separate public extension libraries layered on top of the
core binding. Their contract lives in
[.NET Codec Extension Specification](codec.md). The core `Systems.Zlink`
assembly does not expose codec entrypoints or require codec dependencies.

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
    /// <exception cref="ArgumentException">Value is not a non-empty even-length hex string.</exception>
    /// <exception cref="ArgumentOutOfRangeException">Decoded value exceeds 255 bytes.</exception>
    static RoutingId FromString(string value); // parses ToHex(); hex input is at most 510 chars

    // --- accessors ---
    int Size { get; }                        // 1..255
    ReadOnlySpan<byte> ToBytes();            // zero-copy view of the raw bytes

    // --- string convenience (NOT a primary representation) ---
    string ToString();                       // same lowercase hex representation as ToHex()
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
ranges (0-706). The code alone identifies the error without needing to
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
`ZlinkSubmitException.ErrorCode`.

```csharp
public sealed class ZlinkSubmitException : ZlinkException
{
    public enum ErrorCode
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
        NotAdmitted = 13
    }

    public ZlinkSubmitException(ErrorCode result);
    public ZlinkSubmitException(ErrorCode result, int internalErrno);

    public ErrorCode Result { get; }
}
```

### ZlinkRequestException

Thrown / surfaced for request completion failures. Task-returning `Request`
overloads raise this exception when the reply phase fails (timeout, peer
terminated, protocol error). Callback-based `Request` overloads instead
deliver the `RequestResult` through the callback — see the callback note
on those methods.

```csharp
public sealed class ZlinkRequestException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        TimedOut = 101,
        NotFound = 102,
        Terminated = 103,
        ProtocolError = 104,
        InternalError = 105,
        Rejected = 106,
        Conflict = 107,
        Busy = 108,
        NotConnected = 109,
        InvalidArgument = 110,
        InvalidState = 111,
        NotSupported = 112
    }

    public ZlinkRequestException(ErrorCode result);
    public ZlinkRequestException(ErrorCode result, int internalErrno);

    public ErrorCode Result { get; }
}
```

### ZlinkRecvException

Thrown by recv / subscribe / subscription-event / monitor-recv /
timer-recv paths. Wraps a `ZlinkRecvException.ErrorCode`.

```csharp
public sealed class ZlinkRecvException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        NoData = 201,
        Busy = 202,
        Terminated = 203,
        InvalidHandle = 204,
        NotSupported = 205,
        InternalError = 206
    }

    public ZlinkRecvException(ErrorCode result);
    public ZlinkRecvException(ErrorCode result, int internalErrno);

    public ErrorCode Result { get; }
}
```

### ZlinkHandlerException

Thrown by handler-registration calls (`OnPacket`, `OnSendReady`,
`OnEvent`, `OnFire`, `OnRoutedReceive`, `OnDispatchEvent`, etc.). Wraps a
`ZlinkHandlerException.ErrorCode`.

```csharp
public sealed class ZlinkHandlerException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        InvalidArgument = 301,
        Busy = 302,
        NotSupported = 303,
        Deadlock = 304,
        InvalidHandle = 305,
        InternalError = 306
    }

    public ZlinkHandlerException(ErrorCode result);
    public ZlinkHandlerException(ErrorCode result, int internalErrno);

    public ErrorCode Result { get; }
}
```

### ZlinkCloseException

Thrown by socket, context, monitor, poller, timer, registry, discovery, Spot,
and SpotNode lifecycle operations (`Close`, `Dispose`, `DisposeAsync`,
`Shutdown`). Message-frame lifecycle helpers use
`ZlinkConfigException` because `zlink_msg_close(...)` returns the config result
domain. Wraps a `ZlinkCloseException.ErrorCode`.

```csharp
public sealed class ZlinkCloseException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        Busy = 401,
        Shutdown = 402,
        InvalidHandle = 403,
        InternalError = 404
    }

    public ZlinkCloseException(ErrorCode result);
    public ZlinkCloseException(ErrorCode result, int internalErrno);

    public ErrorCode Result { get; }
}
```

### ZlinkBindException

Thrown by `Bind(...)`. Wraps a `ZlinkBindException.ErrorCode`.

```csharp
public sealed class ZlinkBindException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        InvalidArgument = 501,
        AddrInUse = 502,
        NotSupported = 503,
        InvalidHandle = 504,
        InternalError = 505
    }

    public ZlinkBindException(ErrorCode result);
    public ZlinkBindException(ErrorCode result, int internalErrno);

    public ErrorCode Result { get; }
}
```

### ZlinkConnectException

Thrown by `Connect` / `Disconnect` / `Unbind` / `ConnectPeer` /
`DisconnectPeer` / `ConnectRegistry`. Wraps a
`ZlinkConnectException.ErrorCode`.

```csharp
public sealed class ZlinkConnectException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        InvalidArgument = 601,
        NotSupported = 602,
        InvalidHandle = 603,
        InternalError = 604,
        NotFound = 605,
        Conflict = 606,
        Busy = 607
    }

    public ZlinkConnectException(ErrorCode result);
    public ZlinkConnectException(ErrorCode result, int internalErrno);

    public ErrorCode Result { get; }
}
```

### ZlinkConfigException

Thrown by option setters/getters, TLS configuration, discovery
attachment, snapshot/query calls, poller mutation, timer configuration,
message lifecycle helpers, and `ContextOptions` mutators. Wraps a
`ZlinkConfigException.ErrorCode`.

```csharp
public sealed class ZlinkConfigException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        InvalidHandle = 701,
        InvalidArgument = 702,
        NotSupported = 703,
        InternalError = 704,
        InvalidState = 705,
        NotFound = 706
    }

    public ZlinkConfigException(ErrorCode result);
    public ZlinkConfigException(ErrorCode result, int internalErrno);

    public ErrorCode Result { get; }
}
```

### RequestResult

Result code delivered to request completion callbacks.

```csharp
public enum RequestResult
{
    Ok = 0,
    TimedOut = 101,
    NotFound = 102,
    Terminated = 103,
    ProtocolError = 104,
    InternalError = 105,
    Rejected = 106,
    Conflict = 107,
    Busy = 108,
    NotConnected = 109,
    InvalidArgument = 110,
    InvalidState = 111,
    NotSupported = 112
}
```

### RequestCallback

Callback type used by callback-submit request methods.

```csharp
public delegate void RequestCallback(RequestResult result,
                                     IReadOnlyList<Message> parts);
```

### Received

Aggregates one recv result with optional routing id and message parts.
Implements `IDisposable`.

```csharp
public sealed class Received : IDisposable
{
    RoutingId? RoutingId { get; }            // peer_rid (Router) / source_node_rid (Spot)
    RoutingId? SpotRid { get; }              // set only for SPOT routed recv
    ulong? RequestSeq { get; }               // null when not a request-reply recv
    IReadOnlyList<Message> Parts { get; }
    bool IsSinglePart { get; }

    /// <exception cref="ZlinkRecvException"/>
    Message FirstPart();
    /// <exception cref="ZlinkRecvException"/>
    Message SinglePartOrThrow();

    // Reply requires a non-null RequestSeq. A null or invalid reply context
    // raises ZlinkSubmitException.
    /// <exception cref="ZlinkSubmitException"/>
    void Reply(Message part, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void Reply(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);

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

### SubscriptionEntry

Subscription introspection result returned by `SubscriptionAt(...)`.

```csharp
public sealed record SubscriptionEntry(string Filter, bool IsPattern);
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

### Actor Types

Actor values are public value objects and lifecycle handles for SPOT Actor
dispatch.

```csharp
public readonly struct ActorRef : IEquatable<ActorRef>
{
    ActorRef(RoutingId nodeRid, string actorId, ulong generation);
    RoutingId NodeRid { get; }
    string ActorId { get; }
    ulong Generation { get; }
    bool IsUnchecked { get; }
}

public enum ActorCreateStatus { Created = 1, Existing = 2 }
public enum ActorAdmissionResult { Accept = 1, Reject = 2 }

public sealed record ActorCreateResult(ActorCreateStatus Status,
                                       ActorRef Actor);
public sealed record ActorRoute(ActorRef Actor, bool Joined,
                                RoutingId? JoinedSpotRid);
public sealed record ActorRecvInfo(ActorRef Actor,
                                   RoutingId SourceNodeRid,
                                   RoutingId SourceSessionRid,
                                   uint Flags);
public sealed record ActorJoinInfo(ActorRef SourceActor,
                                   ActorRef TargetActor,
                                   RoutingId SourceNodeRid,
                                   RoutingId SourceSpotRid,
                                   RoutingId TargetNodeRid,
                                   RoutingId TargetSpotRid,
                                   ulong JoinEpoch,
                                   uint Flags);
public sealed record ActorPart(ActorRecvInfo Info, Message Message,
                               bool More);
public sealed class ActorJoinRequest
{
    ActorJoinInfo Info { get; }
    Message Message { get; }
}
public sealed record SpotNodeSpotEntry(RoutingId? SpotRid,
                                       bool DispatchHandlerAttached,
                                       uint JoinedActorCount,
                                       uint PendingActorJoinCount,
                                       bool RouteSynced,
                                       ulong LastChangedMs);
public sealed record SpotNodeActorEntry(ActorRef Actor, bool Joined,
                                        RoutingId? JoinedSpotRid,
                                        bool RouteSynced,
                                        uint PendingMessageCount,
                                        ulong LastChangedMs);

public delegate ActorAdmissionResult ActorAdmissionHandler(string actorId,
                                                           Message message);

public sealed class Actor : IDisposable, IAsyncDisposable
{
    ActorRef Ref { get; }
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed.</exception>
    Task<IReadOnlyList<Message>> Join(Spot spot, Message message,
                                      TimeSpan timeout = default,
                                      CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool Join(Spot spot, Message message,
              RequestCallback callback,
              SendFlags flags = SendFlags.None,
              TimeSpan? timeout = null);
    /// <exception cref="ZlinkRequestException"/>
    void Leave(Spot spot, TimeSpan timeout = default);
    /// <exception cref="ZlinkRecvException"/>
    ActorPart? RecvPart(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    bool SendBoundSession(Message message, SendFlags flags = SendFlags.None);
    /// <exception cref="ZlinkRequestException"/>
    void CloseBoundSession(TimeSpan timeout = default);
    /// <exception cref="ZlinkRequestException"/>
    void Close(TimeSpan timeout = default);
    /// <exception cref="ZlinkRequestException"/>
    void Dispose();
    /// <exception cref="ZlinkRequestException"/>
    ValueTask DisposeAsync();
}
```

Actor ids are non-empty UTF-8 strings up to 255 bytes and must not contain
NUL. `Generation == 0` is an unchecked remote ref, not an invalid ref. One
Actor can join only one Spot at a time. `Leave` does not drop unread Actor
parts. There is no public per-Actor queue limit option.
`ActorJoinRequest` keeps the reply context opaque: callers can inspect
`Info` and `Message`, but `Spot.ReplyActorJoin(...)` receives the request
object so the binding can use the native context without exposing it.
`CreateRemoteActor(...)` is create-or-get: the remote admission handler is
called only when the target Actor does not already exist. Discovery Actor
routes become active when a STREAM session bind succeeds, not when an Actor is
created. `Actor.Close(...)` destroys the local Actor handle. `SpotNode`
also exposes ref-based `DestroyActor(...)`, `JoinActor(...)`, and
`LeaveActor(...)` methods for callers that hold only an `ActorRef`.

### SpotDispatchEvent

Dispatch event kind delivered to `Spot.OnDispatchEvent`. Maps 1-to-1 to the
C API `zlink_spot_dispatch_event_t`.

```csharp
public enum SpotDispatchEvent
{
    SubscribeReadable    = 1,  // topic message ready — drain via Spot.Subscribe()
    RoutedReadable       = 2,  // routed message ready — drain via Spot.RecvRouted()
    TimerReadable        = 3,  // timer fired — drain via info.Timer.Recv()
    ChannelReplyReadable = 4,  // channel reply progress ready
    ActorReadable        = 5,  // actor part ready — drain via SpotDispatchInfo.RecvActorPart()
    ActorJoinReadable    = 6   // actor join request ready — drain via Spot.RecvActorJoin()
}
```

### SpotDispatchSubjectKind

Subject kind accompanying a `SpotDispatchInfo`. Maps to the C API
`zlink_spot_dispatch_subject_kind_t`.

```csharp
public enum SpotDispatchSubjectKind
{
    Spot          = 1,   // event source is the Spot itself
    Timer         = 2,   // event source is available through SpotDispatchInfo.Timer
    ChannelDealer = 3,   // internal channel reply source
    Actor         = 4    // actor parts are available through ActorParts / RecvActorPart()
}
```

### SpotDispatchInfo

Structured dispatch event info passed to `Spot.OnDispatchEvent`. Maps to
the C API `zlink_spot_dispatch_info_t`.

```csharp
public sealed class SpotDispatchInfo
{
    SpotDispatchEvent Event { get; }
    SpotDispatchSubjectKind SubjectKind { get; }
    Timer? Timer { get; }
    IReadOnlyList<ActorPart> ActorParts { get; }
    ActorPart? RecvActorPart();
}
```

The native dispatch subject is callback-lifetime state owned by the binding
and is not exposed as a raw pointer. For `TimerReadable`, `Timer` is the timer
to drain. For `ChannelReplyReadable`, request futures and callbacks progress
their replies inside the binding; the public API does not expose the native
channel dealer subject.

`SubscribeReadable` and `RoutedReadable` are readiness events. Callers must
drain `Spot.Subscribe(...)` or `Spot.RecvRouted(...)` until the binding
reports no data. `ActorReadable` is also a readiness event. The binding drains
Actor parts at native callback entry and exposes them through `ActorParts` and
`RecvActorPart()` so the caller knows which Actor produced the message.

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
The callback runs inline on the native monitor callback thread.

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
    MonitorEvent? Recv(RecvFlags flags = RecvFlags.None);
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
    MonitorSourceKind SourceKind { get; }    // monitor target kind
    uint StateFlags { get; }                 // state bitmask
    uint DetailFlags { get; }                // detail bitmask
    ulong SndPendingMsgs { get; }            // send-queue pending messages
    ulong RcvPendingMsgs { get; }            // recv-queue pending messages
    bool AutoHwmEnabled { get; }
    uint AutoHwmProfile { get; }
    uint AutoHwmRole { get; }
    uint AutoHwmPolicyClass { get; }
    ulong AutoHwmUnitBudgetBytes { get; }
    uint AutoHwmSizeCap { get; }
    ulong AutoHwmSocketMessageSlots { get; }
    ulong AutoHwmEffectiveMessageBytes { get; }
    int AutoHwmAppliedSndHwm { get; }
    int AutoHwmAppliedRcvHwm { get; }
    int AutoHwmEffectiveSndbuf { get; }
    int AutoHwmEffectiveRcvbuf { get; }
    ulong AutoHwmLastRecalcMs { get; }
    uint AutoHwmLastRecalcReason { get; }
    uint AutoHwmSendBlockedRatioPpm { get; }
    int AutoHwmDeferredSndHwm { get; }
    int AutoHwmDeferredRcvHwm { get; }

    bool IsReady { get; }                    // ready bit from raw socket monitor source
}
```

```csharp
public enum MonitorSourceKind
{
    Socket = 1,
    SpotPub = 3,
    SpotSub = 4
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
    void SetHeartbeat(TimeSpan interval, TimeSpan timeout);
    /// <exception cref="ZlinkConfigException"/>
    void SetBroadcastInterval(TimeSpan interval);
    /// <exception cref="ZlinkConfigException"/>
    void SetTlsServer(string certPath, string keyPath,
                      bool requireClientCert = false);
    /// <exception cref="ZlinkConfigException"/>
    void SetTlsClient(string caCertPath, string hostname,
                      bool trustSystem = false);

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
    MemberPeerEntry[] MemberPeers(string channelName);

    /// <exception cref="ZlinkCloseException"/>
    void Close();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

### Discovery

Fixed-channel discovery view. Tracks one auto-connect type and channel name.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public enum AutoConnectType
{
    Invalid = 0,
    RouteMesh = 1,
    ClientServer = 2,
    DealerMesh = 3,
    Fanout = 4,
    SpotMesh = 5
}

public sealed class Discovery : IDisposable, IAsyncDisposable
{
    Discovery(Context context, AutoConnectType autoConnectType, string channelName);

    /// <exception cref="ZlinkConnectException"/>
    void ConnectRegistry(string registryPubEndpoint);
    /// <exception cref="ZlinkConfigException"/>
    void SetTlsClient(string caCertPath, string hostname,
                      bool trustSystem = false);
    /// <exception cref="ZlinkConfigException"/>
    void SetValue(long value);
    /// <exception cref="ZlinkConfigException"/>
    long GetValue();
    /// <exception cref="ZlinkConfigException"/>
    int RouteValueMaxSize { get; }

    /// <exception cref="ZlinkConfigException"/>
    MemberPeerEntry[] MemberPeers();

    /// <summary>
    /// Resolve the current owner node routing id for a logical spot routing id.
    /// Intended for send/request destination lookup. Maps to zlink_discovery_resolve_spot.
    /// Registry-backed lookup requires the publishing Discovery to enable
    /// ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC.
    /// </summary>
    /// <exception cref="ZlinkConfigException"/>
    RoutingId ResolveSpot(RoutingId spotRid);
    /// <exception cref="ZlinkConfigException"/>
    ActorRoute ResolveActor(string actorId);

    /// <summary>Enable or disable publishing SPOT owner rows to Registry.</summary>
    bool SpotOwnerSyncEnabled { get; set; }
    bool ActorRouteSyncEnabled { get; set; }

    /// <exception cref="ZlinkCloseException"/>
    void Close();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

`Discovery.Close()` / `Dispose()` closes every participant attached through
that Discovery handle, including attached raw sockets and `SpotNode` instances.
After close, those participant handles are no longer usable.

### SpotNode

Spot node lifecycle and topology facade.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class SpotNode : IDisposable, IAsyncDisposable
{
    SpotNode(Context context);
    SpotNode(Context context, SpotNodeMode mode);

    // --- identity / routing ---
    /// <summary>
    /// Logical address for the SpotNode. Maps to zlink_set_routing_id(node, ...)
    /// / zlink_get_routing_id(node, ...).
    /// </summary>
    /// <exception cref="ZlinkConfigException"/>
    void SetRoutingId(RoutingId routingId);
    /// <exception cref="ZlinkConfigException"/>
    RoutingId RoutingId { get; }

    /// <exception cref="ZlinkBindException"/>
    void Bind(string endpoint);
    /// <exception cref="ZlinkConfigException"/>
    string LastEndpoint { get; }
    /// <exception cref="ZlinkConnectException"/>
    void ConnectPeer(string peerEndpoint);
    /// <exception cref="ZlinkConnectException"/>
    void DisconnectPeer(string peerEndpoint);
    /// <exception cref="ZlinkConnectException"/>
    void DisconnectPeerRid(RoutingId targetNodeRid);
    /// <exception cref="ZlinkConfigException"/>
    void AttachDiscovery(Discovery discovery);
    /// <exception cref="ZlinkConfigException"/>
    void AttachChannelDealer(Discovery discovery, DealerSocket dealer);
    /// <exception cref="ZlinkConfigException"/>
    void AttachChannelDealerManual(string channelName, DealerSocket dealer);
    /// <exception cref="ZlinkConfigException"/>
    void AttachPubIngress(PubSocket pub);

    // --- node options ---
    AutoHwmProfile RouterHwmProfile { get; set; }
    int RouterHighWaterMark { get; set; }
    AutoHwmProfile PubSubHwmProfile { get; set; }
    int PubSubHighWaterMark { get; set; }
    int DispatchWorkersMin { get; set; }
    int DispatchWorkersMax { get; set; }

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
    /// <exception cref="ZlinkConfigException"/>
    SpotNodeSocketSnapshotEntry[] InternalSocketsSnapshot(
        SpotNodeSocketSnapshotFilter? filter = null);
    /// <exception cref="ZlinkConfigException"/>
    SpotNodeSpotEntry[] SpotsSnapshot();
    /// <exception cref="ZlinkConfigException"/>
    SpotNodeActorEntry[] ActorsSnapshot();
    // Spot creation is owned by SpotNode.
    /// <exception cref="ZlinkConfigException"/>
    Spot CreateSpot();
    /// <exception cref="ZlinkConfigException"/>
    Spot EntrySpot();
    /// <exception cref="ZlinkConfigException"/>
    Spot? SpotLookup(RoutingId spotRid);

    /// <exception cref="ZlinkConfigException"/>
    Actor CreateActor(string actorId);
    /// <exception cref="ZlinkConfigException"/>
    ActorRef ActorLookup(string actorId);
    /// <exception cref="ZlinkConfigException"/>
    static ActorRef RemoteActorRef(RoutingId targetNodeRid, string actorId);
    /// <exception cref="ZlinkRequestException"/>
    ActorCreateResult CreateRemoteActor(RoutingId targetNodeRid,
                                        string actorId,
                                        Message message,
                                        TimeSpan timeout = default);
    /// <exception cref="ZlinkRequestException"/>
    void DestroyActor(ActorRef actor, TimeSpan timeout = default);
    /// <exception cref="ZlinkHandlerException"/>
    void OnActorAdmission(ActorAdmissionHandler handler);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed.</exception>
    Task<IReadOnlyList<Message>> JoinActor(ActorRef actor,
                                           RoutingId destNodeRid,
                                           RoutingId destSpotRid,
                                           Message message,
                                           TimeSpan timeout = default,
                                           CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool JoinActor(ActorRef actor, RoutingId destNodeRid,
                   RoutingId destSpotRid, Message message,
                   RequestCallback callback,
                   SendFlags flags = SendFlags.None,
                   TimeSpan? timeout = null);
    /// <exception cref="ZlinkRequestException"/>
    void LeaveActor(ActorRef actor, RoutingId currentSpotRid,
                    TimeSpan timeout = default);

    // Close/Dispose cascades through live Spot handles before closing the node.
    /// <exception cref="ZlinkCloseException"/>
    void Close();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}
```

`SpotNode` owns the lifecycle. Public callers obtain `Spot` handles only
through `SpotNode.CreateSpot()`, `SpotNode.EntrySpot()`, or
`SpotNode.SpotLookup(...)`. The `Spot(SpotNode)` constructor is internal and is
not part of the public contract.

### Spot

Spot messaging endpoint. Provides service-aware pub/sub and routed messaging.
Implements `IDisposable` and `IAsyncDisposable`. Public callers create it only
through a `SpotNode` factory.

```csharp
public sealed class Spot : IDisposable, IAsyncDisposable
{
    // Spot(SpotNode) is internal. Public callers use SpotNode factories.

    TimeSpan? RequestTimeout { get; set; }

    // --- identity / routing ---
    /// <summary>
    /// Logical address / spot-level routed ownership key.
    /// Maps to zlink_set_routing_id(spot, ...) / zlink_get_routing_id(spot, ...).
    /// </summary>
    /// <exception cref="ZlinkConfigException"/>
    void SetRoutingId(RoutingId routingId);
    /// <exception cref="ZlinkConfigException"/>
    RoutingId RoutingId { get; }

    // --- SPOT topic publish / channel-aware send / request builders ---
    SendOperation Publish(string serviceName, string topic);
    SendOperation SendChannel(string channelName);
    RequestOperation RequestChannel(string channelName);

    // --- subscribe ---
    /// <exception cref="ZlinkConfigException"/>
    void SetSubscription(string topicOrPattern);
    /// <exception cref="ZlinkConfigException"/>
    void UnsetSubscription(string topicOrPattern);
    /// <exception cref="ZlinkConfigException"/>
    SubscriptionEntry? SubscriptionAt(int index);
    /// <exception cref="ZlinkRecvException"/>
    TopicMessage? Subscribe(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkRecvException"/>
    SubscriptionEvent? ReceiveSubscriptionEvent(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnSendReady(Action handler);

    // --- routed send / request / reply builders ---
    SendOperation SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid);
    RequestOperation RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid);
    RequestOperation RequestToRouter(RoutingId peerRid);
    ReplyOperation ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                               ulong requestSeq);
    ReplyOperation ReplyToRouter(RoutingId peerRid, ulong requestSeq);

    // --- routed receive ---
    /// <exception cref="ZlinkRecvException"/>
    Received? RecvRouted(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkHandlerException"/>
    void OnRoutedReceive(Action<Received> handler);
    /// <summary>
    /// Register the unified dispatch event handler. Callback-lifetime native
    /// subjects remain internal to SpotDispatchInfo.
    /// The handler runs inline on the native dispatch callback thread.
    /// Maps to zlink_spot_dispatch_event_handler with zlink_spot_dispatch_info_t.
    /// </summary>
    /// <exception cref="ZlinkHandlerException"/>
    void OnDispatchEvent(Action<SpotDispatchInfo> handler);

    // --- actor join / snapshot ---
    /// <exception cref="ZlinkRecvException"/>
    ActorJoinRequest? RecvActorJoin(RecvFlags flags = RecvFlags.None);
    /// <exception cref="ZlinkSubmitException"/>
    void ReplyActorJoin(ActorJoinRequest request, bool accepted,
                        Message message);
    /// <exception cref="ZlinkConfigException"/>
    ActorRef[] ActorsSnapshot();

    /// <exception cref="ZlinkCloseException"/>
    void Close();
    /// <exception cref="ZlinkCloseException"/>
    void Dispose();
    /// <exception cref="ZlinkCloseException"/>
    ValueTask DisposeAsync();
}

public interface SendOperation
{
    SendSubmitOperation Message(Message message);
}

public interface SendSubmitOperation
{
    SendSubmitOperation Message(Message message);
    SendSubmitOperation Flags(SendFlags flags);
    /// <exception cref="ZlinkSubmitException"/>
    bool Submit();
}

public interface RequestOperation
{
    RequestSubmitOperation Message(Message message);
}

public interface RequestSubmitOperation
{
    RequestSubmitOperation Message(Message message);
    RequestSubmitOperation Timeout(TimeSpan timeout);
    RequestCallbackSubmitOperation Flags(SendFlags flags);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    /// <exception cref="ZlinkRequestException">Reply phase failed.</exception>
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool Submit(RequestCallback callback);
}

public interface RequestCallbackSubmitOperation
{
    RequestCallbackSubmitOperation Message(Message message);
    RequestCallbackSubmitOperation Timeout(TimeSpan timeout);
    RequestCallbackSubmitOperation Flags(SendFlags flags);
    /// <exception cref="ZlinkSubmitException">Submit phase failed.</exception>
    bool Submit(RequestCallback callback);
}

public interface ReplyOperation
{
    ReplySubmitOperation Message(Message message);
}

public interface ReplySubmitOperation
{
    ReplySubmitOperation Message(Message message);
    ReplySubmitOperation Flags(SendFlags flags);
    /// <exception cref="ZlinkSubmitException"/>
    void Submit();
}
```

`Spot.Publish(serviceName, topic)` starts the managed SPOT topic publish
operation. The `serviceName` parameter is the core topic-plane namespace name;
it is not a channel dealer name and does not cause .NET to select a raw `PUB`
socket. The binding keeps native part-loop sequencing internal while core
admits the publish into the owning `SpotNode` topic data-plane. Temporary
admission backpressure is reported through the same `SendFlags.DontWait`
contract as other send-like methods.

`SpotNode.AttachPubIngress(PubSocket pub)` is separate from
`Spot.Publish(...).Message(...).Submit()`. It registers an external raw `PUB`
socket as an ingress
source for the same SPOT topic plane, so messages arriving from that attachment
are drained by `Spot.Subscribe(...)` with the same `TopicMessage` result shape.
Channel calls remain separate: `SendChannel(...)` and `RequestChannel(...)`
address attached channel `DEALER` handles by `channelName`.

`SendOperation`, `RequestOperation`, and `ReplyOperation` are the canonical
.NET SPOT operation builders. Callers add one or more payload parts with
`Message(...)`, optionally add `Flags(...)` or `Timeout(...)`, then execute the
operation with `Submit()` / `SubmitAsync(...)` / `Submit(callback)`.
Implementations must reject submit without at least one message. The canonical
surface does not add separate `Message` and `IReadOnlyList<Message>` overloads
on `Spot` for these paths. Submit consumes the operation; reusing the same
operation object after submit must fail with a validation error.
`RequestSubmitOperation.SubmitAsync(...)` is the async request form and does
not accept submit flags. Calling `Flags(...)` moves the request operation to
the callback-submit stage.

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
    AutoConnectType AutoConnectType,
    ServiceRole ServiceRole,
    string ChannelName,
    string Endpoint,
    RoutingId? RoutingId,
    long Value,
    uint Weight);
```

#### RegistryTopologyEntry

Registry topology entry.

```csharp
public sealed record RegistryTopologyEntry(
    AutoConnectType AutoConnectType,
    RoutingId? RoutingId,
    ServiceKind ServiceKind,
    ServiceRole ServiceRole,
    string ChannelName,
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
    uint DisconnectedSubTargetCount,
    uint DisconnectedRoutedTargetCount,
    int LastError,
    ulong LastChangedMs);
```

`DisconnectedSubTargetCount` and `DisconnectedRoutedTargetCount` expose the
core diagnostic fields. Current core does not disconnect targets only because
a delivery queue grows, so both values are reported as `0`.

Advanced / Diagnostic entry types and filters:

#### RegistryServiceSummaryEntry

Registry service summary entry.

```csharp
public sealed record RegistryServiceSummaryEntry(
    AutoConnectType AutoConnectType,
    ServiceRole ServiceRole,
    string ChannelName,
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

public sealed record SpotNodeSocketSnapshotEntry(
    SpotNodeSocketOwner Owner,
    ulong OwnerId,
    string OwnerName,
    string SocketName,
    SpotNodeSocketType SocketType,
    bool AutoHwmVisible,
    MonitorSnapshot Snapshot);

public sealed record RegistryServiceSummaryFilter(
    AutoConnectType? AutoConnectType = null,
    ServiceRole? ServiceRole = null,
    string? ChannelName = null);

public sealed record RegistryTopologyFilter(
    AutoConnectType? AutoConnectType = null,
    ServiceKind? ServiceKind = null,
    ServiceRole? ServiceRole = null,
    string? ChannelName = null,
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

public sealed record SpotNodeSocketSnapshotFilter(
    SpotNodeSocketOwner? Owner = null,
    SpotNodeSocketType? SocketType = null,
    string? SocketName = null);
```

The service-layer enums map 1-to-1 to the core enum values.

```csharp
public enum ServiceRole
{
    Invalid = 0,
    Spot = 2,
    Router = 3,
    Dealer = 4,
    Pub = 5,
    Sub = 6
}

public enum SpotRole
{
    Pub = 1,
    Sub = 2
}

public enum ServiceKind
{
    Discovery = 1,
    SpotSub = 3,
    SpotPub = 4,
    Socket = 5
}

public enum SubjectKind
{
    None = 0,
    Topic = 1,
    Pattern = 2
}

public enum SpotNodeState
{
    Idle = 1,
    Connecting = 2,
    PartialReady = 3,
    Ready = 4,
    Error = 5
}

public enum SpotPeerSource
{
    Manual = 1,
    Discovery = 2,
    Mixed = 3
}

public enum SpotPeerState
{
    Configured = 1,
    Connecting = 2,
    Connected = 3
}

public enum RegistryState
{
    Idle = 1,
    Active = 2,
    Degraded = 3,
    Error = 4
}

public enum TopologySource
{
    Manual = 1,
    Discovery = 2,
    Registry = 3
}

public enum TopologyState
{
    Discovered = 1,
    Connecting = 2,
    Ready = 3,
    Lost = 4,
    Error = 5,
    Stopped = 6
}

public enum SpotNodeSocketOwner
{
    Any = 0,
    Node = 1,
    Spot = 2
}

public enum SpotNodeSocketType
{
    Any = 0,
    Pair = 0x1001,
    Pub = 0x1002,
    Sub = 0x1003,
    Dealer = 0x1004,
    Router = 0x1005,
    XPub = 0x1006,
    XSub = 0x1007,
    Stream = 0x1008
}
```

---

## Poller

### Poller

Event poller for multiplexing socket, file descriptor, and timer readiness.
Implements `IDisposable` and `IAsyncDisposable`.

```csharp
public sealed class Poller : IDisposable, IAsyncDisposable
{
    Poller();

    /// <summary>Number of registered pollable items. Maps to zlink_poller_size.</summary>
    /// <exception cref="ZlinkConfigException"/>
    int Size { get; }

    // --- socket registration ---
    /// <exception cref="ZlinkConfigException"/>
    void Add(IZlinkSocket socket, PollEventFlags events, object? tag = null);
    /// <exception cref="ZlinkConfigException"/>
    void Modify(IZlinkSocket socket, PollEventFlags events);
    /// <exception cref="ZlinkConfigException"/>
    bool Remove(IZlinkSocket socket);

    // --- file descriptor registration ---
    /// <exception cref="ZlinkConfigException"/>
    void AddFd(int fd, PollEventFlags events, object? tag = null);
    /// <exception cref="ZlinkConfigException"/>
    void ModifyFd(int fd, PollEventFlags events);
    /// <exception cref="ZlinkConfigException"/>
    bool Remove(int fd);

    // --- timer registration ---
    /// <exception cref="ZlinkConfigException"/>
    void Add(Timer timer, object? tag = null);
    /// <exception cref="ZlinkConfigException"/>
    bool Remove(Timer timer);

    // --- wait ---
    /// <exception cref="ZlinkRecvException"/>
    PollEvent? Wait(TimeSpan timeout);
    /// <exception cref="ZlinkRecvException"/>
    IReadOnlyList<PollEvent> WaitAll(int maxEvents, TimeSpan timeout);

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
    Timer? Timer { get; }
    object? Tag { get; }
    PollEventFlags Events { get; }
    PollEventFlags Revents { get; }
}
```

```csharp
public enum PollEventFlags
{
    None = 0,
    PollIn = 1,
    PollOut = 2,
    PollErr = 4,
    PollPri = 8
}
```

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
    void Start(TimeSpan interval, ulong repeatCount);
    /// <exception cref="ZlinkConfigException"/>
    void Stop();
    /// <exception cref="ZlinkRecvException"/>
    ulong? Recv(RecvFlags flags = RecvFlags.None);
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

    /// Sleep for the given duration.
    static void Sleep(TimeSpan duration);

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

---

## Core Capability Coverage Appendix

This appendix is a native coverage checklist, not a second public API contract.
The public signatures above remain the canonical .NET binding contract.

The mapping does not expose every native helper function as a separate public
method. Native `*_part` helpers, adoption helpers, and callback userdata
plumbing may be hidden when the same capability is available through a typed
.NET operation that keeps ownership and error handling clear.

The intentional native-only surfaces are:

- `zlink_errno()`: public callers read the captured errno through
  `ZlinkException.InternalErrno` on the typed exception.
- `zlink_msg_init_data(...)`: borrowed external-buffer messages are not public
  in managed bindings because lifetime cannot be made obvious to callers.
  Public constructors and factories are copy-based.
- `zlink_msg_adopt(...)`: used internally when a received native frame becomes
  a `Message`.
- `zlink_recv_handler(...)`: the core raw STREAM direct callback remains an
  internal bridge in this binding. Public STREAM users choose `Recv(...)` or
  `OnPacket(...)`; `OnPacket(...)` is the framed packet callback mapped to
  `zlink_stream_packet_handler(...)`.
- `*_part` send, recv, request, reply, publish, subscribe helpers: public .NET
  APIs expose complete `Message` or `IReadOnlyList<Message>` operations and
  hide `ZLINK_PART_MORE` / `ZLINK_PART_FINAL`.
- Native callback userdata parameters: public callbacks capture managed
  delegates instead.

Native-to-.NET coverage is grouped below. If a core function is added to
`zlink.h`, this appendix and the relevant public signature section must be
updated in the same change.

| Core API group | Native functions | .NET public mapping |
|----------------|------------------|---------------------|
| Errors and version | `zlink_errno`, `zlink_strerror`, `zlink_version`, `zlink_has` | typed exceptions, `Zlink.Strerror`, `Zlink.Version`, `Zlink.Has` |
| Context lifecycle and options | `zlink_ctx_new`, `zlink_ctx_term`, `zlink_ctx_shutdown`, `zlink_ctx_set`, `zlink_ctx_set_data`, `zlink_ctx_get`, `zlink_ctx_auto_hwm_recalculate` | `Context`, `Context.Dispose`, `Context.Shutdown`, `ContextOptions`, `Context.RecalculateAutoHwm` |
| Message frames | `zlink_msg_init`, `zlink_msg_init_size`, `zlink_msg_init_data`, `zlink_msg_close`, `zlink_msg_move`, `zlink_msg_copy`, `zlink_msg_adopt`, `zlink_msg_data`, `zlink_msg_size`, `zlink_msg_refcnt`, `zlink_msg_gets` | `Message` constructors, factories, accessors, `Move`, `Copy`, `Dispose`, `GetProperty` |
| Raw socket lifecycle | `zlink_socket`, `zlink_close`, `zlink_bind`, `zlink_unbind`, `zlink_connect`, `zlink_disconnect`, `zlink_disconnect_rid`, `zlink_socket_attach_discovery` | concrete socket constructors, `Close`, `Bind`, `Unbind`, `Connect` / `Disconnect` / `DisconnectRid` on connectable raw sockets, `AttachDiscovery` |
| Common socket options | `zlink_set_option`, `zlink_get_option`, `zlink_set_routing_id`, `zlink_get_routing_id`, `zlink_set_tls_server`, `zlink_set_tls_client` | typed option facades, routing-id methods/properties, TLS methods |
| Dealer channel metadata | `zlink_socket_set_channel_name`, `zlink_socket_get_channel_name` | `DealerSocket.SetChannelName`, `DealerSocket.GetChannelName` |
| Typed socket options | `zlink_set_router_option`, `zlink_get_router_option`, `zlink_set_dealer_option`, `zlink_set_pub_option`, `zlink_get_pub_option`, `zlink_set_sub_option`, `zlink_get_sub_option`, `zlink_set_stream_option`, `zlink_get_stream_option` | `RouterSocketOptions`, `DealerSocketOptions`, `PubSocketOptions`, `SubSocketOptions`, `StreamSocketOptions` |
| Raw send and recv | `zlink_send_part`, `zlink_send_part_rid`, `zlink_recv_part` | `Send(...)`, routed `Send(...)`, `Recv(...)`, `Received` |
| Raw request and reply | `zlink_dealer_request_part`, `zlink_router_request_part`, `zlink_router_reply_part`, `zlink_router_recv_part` | `DealerSocket.Request*`, `RouterSocket.Request*`, `RouterSocket.Reply`, `RouterSocket.Recv` |
| Pub/sub | `zlink_publish_part`, `zlink_subscribe_part`, `zlink_xpub_recv_part`, `zlink_set_subscription`, `zlink_unset_subscription`, `zlink_subscription_at` | `Publish(...)`, `Subscribe(...)`, `ReceiveSubscriptionEvent(...)`, subscription methods, topic-count/subscription introspection |
| STREAM and actor bridge | `zlink_stream_packet_handler`, `zlink_stream_bind_actor`, `zlink_stream_unbind_actor`, `zlink_stream_send_bound_actor_part`; `zlink_recv_handler` is internal-only | `StreamSocket.OnPacket`, `StreamSocket.Recv`, `BindActor`, `UnbindActor`, `SendBoundActor` |
| Send-ready callbacks | `zlink_send_ready_handler` | `OnSendReady(...)` on send-capable handles |
| Socket monitoring | `zlink_socket_monitor_open`, `zlink_socket_monitor_handler`, `zlink_socket_monitor_recv`, `zlink_monitor_snapshot`, `zlink_monitor_close`, `zlink_monitor_ignore_handler` | `SocketMonitor`, `MonitorEvent`, `MonitorSnapshot`, `SocketMonitor.IgnoreHandler` |
| Registry and Discovery | `zlink_registry_*`, `zlink_discovery_*`, `zlink_registry_query_*`, `zlink_set_tls_server`, `zlink_set_tls_client` on service handles | `Registry`, `Discovery`, `RegistryQueryClient`, service entry/filter records, service TLS methods |
| SPOT node topology | `zlink_spot_node_new`, `zlink_spot_node_destroy`, `zlink_spot_node_bind`, peer connect/disconnect, discovery/channel attachments, publish-ingress attachment, entry spot, spot lookup, snapshots, `zlink_set_spot_node_option`, `zlink_get_spot_node_option` | `SpotNode`, `SpotNodeMode`, attachment APIs including `AttachPubIngress`, snapshot/query APIs, `CreateSpot`, `EntrySpot`, `SpotLookup`, `DisconnectPeerRid` |
| SPOT messaging | `zlink_spot_new`, `zlink_spot_destroy`, `zlink_spot_send_channel_part`, `zlink_spot_publish_part`, `zlink_spot_subscribe_part`, `zlink_spot_subscription_event_recv`, `zlink_spot_request_*_part`, `zlink_spot_send_spot_part`, `zlink_spot_reply_*_part`, `zlink_spot_recv_part`, `zlink_spot_handler`, `zlink_spot_dispatch_event_handler`, `zlink_spot_channel_reply_progress_from`, `zlink_set_spot_option`, `zlink_get_spot_option` | `Spot`, direct request-timeout property, channel send/request, SPOT topic publish/subscribe, routed send/request/reply/recv, dispatch callbacks, internal channel reply progress |
| SPOT actor lifecycle | `zlink_spot_node_actor_*`, `zlink_spot_actor_join_recv`, `zlink_spot_actor_join_reply`, `zlink_remote_actor_get_ref`, `zlink_spot_actors_snapshot` | `Actor`, `ActorRef`, `ActorCreateResult`, join/leave/create/destroy/admission APIs, actor receive/send/bound-session APIs, actor snapshots |
| Polling and timers | `zlink_poll`, `zlink_poller_*`, `zlink_timer_*`, `zlink_spot_timer_new` | `Poller`, `PollEvent`, `Timer`; legacy array `zlink_poll` is intentionally not exposed |
| Utilities | `zlink_proxy`, `zlink_proxy_steerable`, `zlink_multipart_close`, `zlink_sleep`, `zlink_stopwatch_*`, `zlink_thread_*`, `zlink_atomic_counter_*` | `Zlink.Proxy`, `Zlink.ProxySteerable`, `Zlink.MultipartClose`, `Zlink.Sleep(TimeSpan)`, `ZlinkStopwatch`, `ZlinkThread`, `AtomicCounter` |

## Core API Surface 6.0.0 Alignment

Actor create and join payloads use aggregate multipart payloads. Public binding APIs accept a message collection for remote actor create, actor join, actor join receive, and actor join reply. A single-message convenience path may remain, but it must call the multipart path internally so empty payload and one empty message stay distinguishable. Admission handlers receive a borrowed payload view that is valid only during the callback.

Registry scalar configuration uses the registry option surface as the canonical API. Bindings expose typed options for registry id, heartbeat interval, heartbeat timeout, and broadcast interval. Existing named setters may remain as compatibility aliases and must delegate to the option API.
