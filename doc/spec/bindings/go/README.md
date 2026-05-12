[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Go Binding Specification

This document defines the complete public API surface of the zlink Go binding.
Every type, method, and function listed here is part of the contract that the
binding must expose. Unexported methods and internal helpers are omitted.

Only exported identifiers from the public `zlink` package are part of the
contract. Go `internal/` packages, cgo bridge helpers, and source-tree-only
utilities are internal. Perf, samples, and tests must import the public
`zlink` package only and must not rely on internal packages.

## Design Basis

The Go binding follows the repository POSD design policy. Exported types must
hide native sequencing, ownership, and option encoding behind typed, deep
interfaces so callers do not need core implementation details.

The exported Go surface must model stable domain concepts, not cgo call
steps. Exported types are justified when they own context/socket lifetime,
message ownership, receive metadata, service membership, callbacks, or typed
options. cgo handles, part-loop sequencing, request tokens, callback userdata,
and raw option encoding stay inside unexported packages or unexported fields.

Design review uses these POSD constraints:

- send/recv, nonblocking, ownership, and error rules are centralized instead of
  repeated across socket types
- canonical result and facade methods do not ask callers to pass state already
  captured by the receiver, such as a source socket, request sequence, or
  service address
- compatibility helpers, if retained, are not the canonical API and are not
  used by new docs, samples, or tests
- an exported wrapper that only forwards to cgo without adding validation,
  ownership, lifetime, or result-shape semantics is too shallow and must be
  removed or made unexported

---

## High-Performance Requirements

The Go binding is part of a high-performance messaging library. Hot paths must
not use reflection, dynamic dispatch by string, unnecessary allocation,
avoidable byte-slice copies, coarse lock contention, hidden waits, sleeps, busy
waits, or goroutine joins. cgo bridge code must construct public `Message` and
result values directly from the core `*_part` substrate and must not create
native aggregate arrays only to copy them into Go slices.

## Core Alignment Rules

The detailed sections below are the canonical Go binding contract. This
section states cross-cutting constraints once so the per-type API lists can
stay focused on signatures.

- `PairSocket`, `DealerSocket`, and `RouterSocket` keep their documented
  send, recv, request, and reply methods, but they do not expose direct
  data-plane receive callbacks such as `OnReceive(...)`.
- `SubSocket` and `XSubSocket` are receive-only topic sockets and do not
  expose direct topic callbacks such as `OnSubscribe(...)`.
- `StreamSocket` keeps `Recv(...)` and exposes a packet callback surface
  mapped to `zlink_stream_packet_handler()` as `OnPacket(...)`.
- `SpotNode` must expose channel-aware attachment APIs:
  `AttachDiscovery(...)`,
  `AttachChannelDealer(...)`,
  `AttachChannelDealerManual(...)`, and
  `AttachPubIngress(...)`.
- `Spot` must expose channel-aware data-plane operation builders:
  `SendChannel(...)`, `SendToSpot(...)`, `RequestChannel(...)`, and
  `Publish(topic)`.
- `Spot.Subscribe(...)` returns a `TopicMessage`.
  `TopicMessage` exposes topic, parts, and optional routing id.
- `Spot` must not expose `OnSubscribe(...)`. Use `OnDispatchEvent(...)` plus
  `Subscribe(...)` / routed recv / timer recv.
- `SpotDispatchEventSubscribeReadable` and
  `SpotDispatchEventRoutedReadable` are readiness notifications, not
  one-event-per-message delivery counters. Binding docs and samples must drain
  until `EAGAIN`.
- `Spot.OnRoutedReceive(...)` and `OnDispatchEvent(...)` are mutually exclusive
  on the routed axis.
- Peer weight is exposed only on `RouterSocket` and `DealerSocket` through
  typed option/property surfaces. The value range is `0..100`, default
  `100`; `0` drains new outbound selection. Submit attempts to a weight-`0`
  peer return `*SubmitError` whose `Code()` is `SubmitResultNotAdmitted`.
- `POLLOUT` is a send-recovery readiness signal, shared with
  `OnSendReady(...)`. It is not a "transport writable" bit.
- ROUTER / PUB socket option defaults follow the core header: `Mandatory =
  true`, `Handover = false`, `NoDrop = true`.
- SPOT admission HWM defaults follow the core header. Router and pubsub
  admission profile/numeric options are exposed through `SpotNode`; relay and
  delivery HWM stay `0` and are not public Go options.
- SPOT dispatch worker min/max are `SpotNode` callback worker-pool options.
  They are not context options and must not be described as transport I/O
  threads.
- Internal pairing rule: when auto-connect pairs two same-service ROUTERs
  via Discovery, the library picks one initiator per pair by a total order
  on `(routingID, advertiseEndpoint)`. Users do not configure this.

## Actor Dispatch Public Surface

Go exposes Actor dispatch through exported identifiers in package `zlink`.

```go
type ActorRef struct { NodeRID RoutingID; ActorID string; Generation uint64 }
type ActorCreateResult struct { Status ActorCreateStatus; Actor ActorRef }
type ActorRoute struct { Actor ActorRef; Joined bool; JoinedSpotRID *RoutingID }
type ActorRecvInfo struct { Actor ActorRef; SourceNodeRID RoutingID; SourceSessionRID RoutingID; Flags uint32 }
type ActorJoinInfo struct {
    SourceActor ActorRef
    TargetActor ActorRef
    SourceNodeRID RoutingID
    SourceSpotRID RoutingID
    TargetNodeRID RoutingID
    TargetSpotRID RoutingID
    JoinEpoch uint64
    Flags uint32
}
type ActorPart struct { Info ActorRecvInfo; Message *Message; More bool }
type ActorJoinRequest struct { Info ActorJoinInfo; Message *Message }
type ActorAdmissionResult int
const (
    ActorAdmissionAccept ActorAdmissionResult = 1
    ActorAdmissionReject ActorAdmissionResult = 2
)
func RemoteActorRef(targetNodeRID RoutingID, actorID string) (ActorRef, error)
```

`SpotNode` exposes `Actor`, `ActorLookup`, `CreateRemoteActor`,
`DestroyRemoteActor`, `OnActorAdmission`, `JoinActor`, `LeaveActor`,
`SpotsSnapshot`, and `ActorsSnapshot`. `Spot` exposes `RecvActorJoin`,
`ReplyActorJoin`, and `ActorsSnapshot`. `StreamSocket` exposes `BindActor`,
`UnbindActor`, and `SendBoundActor`. `Discovery` exposes `ResolveActor`.

`Generation == 0` is an unchecked remote ref. Actor join requests and replies
carry a single `Message` payload.
Actor IDs are non-empty UTF-8 strings up to 255 bytes and must not contain NUL.
Leaving a Spot does not drain unread Actor messages. Remote actor creation is
create-or-get: the admission callback runs only when the target actor is missing.

## Core

### Context

```go
// NewContext creates a new zlink context. Returns *ConfigError on failure.
func NewContext() (*Context, error)
// Close terminates the context. Returns *CloseError on failure.
func (c *Context) Close() error
// Shutdown requests a graceful shutdown. Returns *CloseError on failure.
func (c *Context) Shutdown() error
// RecalculateAutoHwm forces an automatic HWM recalculation. Returns *ConfigError on failure.
func (c *Context) RecalculateAutoHwm() error
func (c *Context) Options() *ContextOptions

// Socket factories — each returns *ConfigError on failure.
func (c *Context) PairSocket() (*PairSocket, error)
func (c *Context) PubSocket() (*PubSocket, error)
func (c *Context) SubSocket() (*SubSocket, error)
func (c *Context) DealerSocket() (*DealerSocket, error)
func (c *Context) RouterSocket() (*RouterSocket, error)
func (c *Context) XPubSocket() (*XPubSocket, error)
func (c *Context) XSubSocket() (*XSubSocket, error)
func (c *Context) StreamSocket() (*StreamSocket, error)
func (c *Context) SpotNode() (*SpotNode, error)
func (c *Context) SpotNodeWithOptions(options SpotNodeOptions) (*SpotNode, error)
func (c *Context) Registry() (*Registry, error)
func (c *Context) Discovery(autoConnectType AutoConnectType, channelName string) (*Discovery, error)
func (c *Context) RegistryQueryClient() (*RegistryQueryClient, error)
```

### ContextOptions

```go
// All ContextOptions getters and setters return *ConfigError on failure.
func (o *ContextOptions) SetIOThreads(value int) error
func (o *ContextOptions) IOThreads() (int, error)
func (o *ContextOptions) SetMaxSockets(value int) error
func (o *ContextOptions) MaxSockets() (int, error)
func (o *ContextOptions) SocketLimit() (int, error)
func (o *ContextOptions) SetThreadPriority(value int) error
func (o *ContextOptions) ThreadPriority() (int, error)
func (o *ContextOptions) SetThreadSchedulingPolicy(value int) error
func (o *ContextOptions) ThreadSchedulingPolicy() (int, error)
func (o *ContextOptions) SetThreadNamePrefix(value string) error
func (o *ContextOptions) ThreadNamePrefix() (string, error)
func (o *ContextOptions) SetMaxMessageSize(value int) error
func (o *ContextOptions) MaxMessageSize() (int, error)
func (o *ContextOptions) MessageStructSize() (int, error)
func (o *ContextOptions) SetBlocky(value bool) error
func (o *ContextOptions) Blocky() (bool, error)
func (o *ContextOptions) SetAutoHwmEnabled(value bool) error
func (o *ContextOptions) AutoHwmEnabled() (bool, error)
func (o *ContextOptions) SetAutoHwmRecalcDebounce(value time.Duration) error
func (o *ContextOptions) AutoHwmRecalcDebounce() (time.Duration, error)
func (o *ContextOptions) SetAutoHwmProfile(value AutoHwmProfile) error
func (o *ContextOptions) AutoHwmProfile() (AutoHwmProfile, error)
func (o *ContextOptions) AddThreadAffinity(cpu int) error
func (o *ContextOptions) RemoveThreadAffinity(cpu int) error
```

```go
type AutoHwmProfile int

const (
    AutoHwmProfileCompact    AutoHwmProfile = 0
    AutoHwmProfileLowLatency AutoHwmProfile = 1
    AutoHwmProfileBalanced   AutoHwmProfile = 2
    AutoHwmProfileThroughput AutoHwmProfile = 3
)
```

The native context memory-budget and bootstrap auto-HWM options are
deprecated no-op compatibility options. The Go binding does not expose typed
getters or setters for them.

### Version

```go
func RuntimeVersion() Version

type Version struct {
    Major int
    Minor int
    Patch int
}
```

---

## Socket Types

All sockets listed below expose common connection and option method groups.
Only the public methods are shown; the implementation structure behind those
methods is not part of this contract.

Go nonblocking data-plane helpers follow this rule:

- Submit methods that take `SendFlags` return `(false, nil)` only for temporary
  backpressure when `SendFlagsDontWait` is used.
- Route-not-ready and other submit failures return a non-nil error.
- Receive methods that take `RecvFlags` return `(nil, nil)` when no message is
  currently available and a non-nil error for real recv failures.

Peer weight is not a common-socket accessor. Bindings expose it only on the
implemented weight-bearing handles (`RouterSocket` and `DealerSocket`) through
their typed option/property surfaces.
`RIDDuplicatePolicy` and `AutoHwmMsgUnitBytes` are common typed socket options.

```go
// No common peer-weight accessor.
type RIDDuplicatePolicy int
const (
    RIDDuplicateReject   RIDDuplicatePolicy = 0
    RIDDuplicateHandover RIDDuplicatePolicy = 1
)

type CommonSocketOptions struct { /* typed facade over common socket options */ }
type PubSocketOptions struct { /* typed facade over publisher options */ }

func (o *PubSocketOptions) SetNoDrop(value bool) error
func (o *PubSocketOptions) NoDrop() (bool, error)
func (o *PubSocketOptions) SetVerbose(value bool) error
func (o *PubSocketOptions) Verbose() (bool, error)
func (o *PubSocketOptions) SetVerboser(value bool) error
func (o *PubSocketOptions) Verboser() (bool, error)
func (o *PubSocketOptions) SetManual(value bool) error
func (o *PubSocketOptions) Manual() (bool, error)
func (o *PubSocketOptions) TopicsCount() (int, error)
func (o *PubSocketOptions) SetManualLastValue(value bool) error
func (o *PubSocketOptions) ManualLastValue() (bool, error)
func (o *PubSocketOptions) SetWelcomeMessage(message *Message) error
func (o *PubSocketOptions) WelcomeMessage() (*Message, error)
func (o *PubSocketOptions) ApproveSubscribe(routingID RoutingID) error
func (o *PubSocketOptions) RejectSubscribe(routingID RoutingID) error

func (o *CommonSocketOptions) SetLinger(value time.Duration) error
func (o *CommonSocketOptions) Linger() (time.Duration, error)
func (o *CommonSocketOptions) SetSendHWM(value int) error
func (o *CommonSocketOptions) SendHWM() (int, error)
func (o *CommonSocketOptions) SetRecvHWM(value int) error
func (o *CommonSocketOptions) RecvHWM() (int, error)
func (o *CommonSocketOptions) SetSendTimeout(value time.Duration) error
func (o *CommonSocketOptions) SendTimeout() (time.Duration, error)
func (o *CommonSocketOptions) SetRecvTimeout(value time.Duration) error
func (o *CommonSocketOptions) RecvTimeout() (time.Duration, error)
func (o *CommonSocketOptions) SetImmediate(value bool) error
func (o *CommonSocketOptions) Immediate() (bool, error)
func (o *CommonSocketOptions) SetRIDDuplicatePolicy(value RIDDuplicatePolicy) error
func (o *CommonSocketOptions) RIDDuplicatePolicy() (RIDDuplicatePolicy, error)
func (o *CommonSocketOptions) SetAutoHwmMsgUnitBytes(value int) error
func (o *CommonSocketOptions) AutoHwmMsgUnitBytes() (int, error)
func (o *CommonSocketOptions) SetConnectTimeout(value time.Duration) error
func (o *CommonSocketOptions) ConnectTimeout() (time.Duration, error)
func (o *CommonSocketOptions) SetIPv6(value bool) error
func (o *CommonSocketOptions) IPv6() (bool, error)
func (o *CommonSocketOptions) SetTCPNoDelay(value bool) error
func (o *CommonSocketOptions) TCPNoDelay() (bool, error)
func (o *CommonSocketOptions) SetTCPKeepalive(value bool) error
func (o *CommonSocketOptions) TCPKeepalive() (bool, error)
func (o *CommonSocketOptions) SetHeartbeatInterval(value time.Duration) error
func (o *CommonSocketOptions) HeartbeatInterval() (time.Duration, error)
func (o *CommonSocketOptions) SetHeartbeatTTL(value time.Duration) error
func (o *CommonSocketOptions) HeartbeatTTL() (time.Duration, error)
func (o *CommonSocketOptions) SetHeartbeatTimeout(value time.Duration) error
func (o *CommonSocketOptions) HeartbeatTimeout() (time.Duration, error)
func (o *CommonSocketOptions) SetMaxMsgSize(value int64) error
func (o *CommonSocketOptions) MaxMsgSize() (int64, error)
func (o *CommonSocketOptions) SetBacklog(value int) error
func (o *CommonSocketOptions) Backlog() (int, error)
func (o *CommonSocketOptions) SetReconnectInterval(value time.Duration) error
func (o *CommonSocketOptions) ReconnectInterval() (time.Duration, error)
func (o *CommonSocketOptions) SetReconnectIntervalMax(value time.Duration) error
func (o *CommonSocketOptions) ReconnectIntervalMax() (time.Duration, error)
func (o *CommonSocketOptions) LastEndpoint() (string, error)

func (s *PairSocket) CommonOptions() *CommonSocketOptions
func (s *PubSocket) CommonOptions() *CommonSocketOptions
func (s *SubSocket) CommonOptions() *CommonSocketOptions
func (s *DealerSocket) CommonOptions() *CommonSocketOptions
func (s *RouterSocket) CommonOptions() *CommonSocketOptions
func (s *XPubSocket) CommonOptions() *CommonSocketOptions
func (s *XSubSocket) CommonOptions() *CommonSocketOptions
func (s *StreamSocket) CommonOptions() *CommonSocketOptions
```

### PairSocket

```go
// Bind binds the socket to endpoint. Returns *BindError on failure.
func (s *PairSocket) Bind(endpoint string) error
// Unbind detaches a previously bound endpoint. Returns *ConnectError on failure.
func (s *PairSocket) Unbind(endpoint string) error
// Connect connects the socket to endpoint. Returns *ConnectError on failure.
func (s *PairSocket) Connect(endpoint string) error
// Disconnect closes the connection to endpoint. Returns *ConnectError on failure.
func (s *PairSocket) Disconnect(endpoint string) error
// DisconnectRID closes the peer selected by routing id. Returns *ConnectError on failure.
func (s *PairSocket) DisconnectRID(rid RoutingID) error
// Send submits parts on the socket. Returns (false, nil) only for temporary backpressure.
func (s *PairSocket) Send(flags SendFlags, parts ...*Message) (bool, error)
// Recv is the canonical caller-provided storage recv. Returns
// (true, nil) on success, (false, nil) when RecvFlagsDontWait finds no
// data, (false, *RecvError) on hard error.
func (s *PairSocket) Recv(out *Received, flags RecvFlags) (bool, error)
// OnSendReady registers a send-ready handler. Returns *HandlerError on failure.
func (s *PairSocket) OnSendReady(handler func()) error
// Option setters/getters return *ConfigError on failure.
func (s *PairSocket) SetSendHWM(value int) error
func (s *PairSocket) SendHWM() (int, error)
func (s *PairSocket) SetRecvHWM(value int) error
func (s *PairSocket) RecvHWM() (int, error)
func (s *PairSocket) SetLinger(value time.Duration) error
func (s *PairSocket) SetRecvTimeout(value time.Duration) error
func (s *PairSocket) SetSendTimeout(value time.Duration) error
func (s *PairSocket) SetTCPKeepalive(value bool) error
func (s *PairSocket) SetTCPNoDelay(value bool) error
func (s *PairSocket) SetIPv6(value bool) error
func (s *PairSocket) LastEndpoint() (string, error)
// TLS configuration returns *ConfigError on failure.
func (s *PairSocket) SetTLSServer(certPath, keyPath string, requireClientCert bool) error
func (s *PairSocket) SetTLSClient(caCertPath, hostname string, trustSystem bool) error
// Close closes the socket. Returns *CloseError on failure.
func (s *PairSocket) Close() error
```

### PubSocket

```go
// Bind binds the socket to endpoint. Returns *BindError on failure.
func (s *PubSocket) Bind(endpoint string) error
// Unbind detaches a previously bound endpoint. Returns *ConnectError on failure.
func (s *PubSocket) Unbind(endpoint string) error
// Connect connects the socket to endpoint. Returns *ConnectError on failure.
func (s *PubSocket) Connect(endpoint string) error
// Disconnect closes the connection to endpoint. Returns *ConnectError on failure.
func (s *PubSocket) Disconnect(endpoint string) error
// DisconnectRID closes the peer selected by routing id. Returns *ConnectError on failure.
func (s *PubSocket) DisconnectRID(rid RoutingID) error
// Publish sends parts on the given topic. Returns (false, nil) only for temporary backpressure.
func (s *PubSocket) Publish(topic string, flags SendFlags, parts ...*Message) (bool, error)
// OnSendReady registers a send-ready handler. Returns *HandlerError on failure.
func (s *PubSocket) OnSendReady(handler func()) error
// Option setters/getters return *ConfigError on failure.
func (s *PubSocket) SetNoDrop(value bool) error
func (s *PubSocket) NoDrop() (bool, error)
func (s *PubSocket) SetVerbose(value bool) error
func (s *PubSocket) Verbose() (bool, error)
func (s *PubSocket) SetVerboser(value bool) error
func (s *PubSocket) Verboser() (bool, error)
func (s *PubSocket) SetManual(value bool) error
func (s *PubSocket) Manual() (bool, error)
func (s *PubSocket) SetManualLastValue(value bool) error
func (s *PubSocket) ManualLastValue() (bool, error)
func (s *PubSocket) SetWelcomeMessage(message *Message) error
func (s *PubSocket) WelcomeMessage() (*Message, error)
func (s *PubSocket) ApproveSubscribe(routingID RoutingID) error
func (s *PubSocket) RejectSubscribe(routingID RoutingID) error
func (s *PubSocket) PubOptions() *PubSocketOptions
// AttachDiscovery binds a discovery handle. Returns *ConfigError on failure.
func (s *PubSocket) AttachDiscovery(discovery *Discovery) error
// Close closes the socket. Returns *CloseError on failure.
func (s *PubSocket) Close() error
```

### SubSocket

```go
// Bind binds the socket to endpoint. Returns *BindError on failure.
func (s *SubSocket) Bind(endpoint string) error
// Unbind detaches a previously bound endpoint. Returns *ConnectError on failure.
func (s *SubSocket) Unbind(endpoint string) error
// Connect connects the socket to endpoint. Returns *ConnectError on failure.
func (s *SubSocket) Connect(endpoint string) error
// Disconnect closes the connection to endpoint. Returns *ConnectError on failure.
func (s *SubSocket) Disconnect(endpoint string) error
// DisconnectRID closes the peer selected by routing id. Returns *ConnectError on failure.
func (s *SubSocket) DisconnectRID(rid RoutingID) error
// Subscription filter mutation returns *ConfigError on failure.
func (s *SubSocket) SetSubscription(filter string) error
func (s *SubSocket) UnsetSubscription(filter string) error
// Subscribe receives the next topic message. Returns *RecvError on failure.
func (s *SubSocket) Subscribe(flags RecvFlags) (*TopicMessage, error)
// AttachDiscovery binds a discovery handle. Returns *ConfigError on failure.
func (s *SubSocket) AttachDiscovery(discovery *Discovery) error
// SubscriptionAt / TopicsCount are snapshot queries. Return *ConfigError on failure.
func (s *SubSocket) SubscriptionAt(index int) (string, bool, error)
func (s *SubSocket) TopicsCount() (int, error)
// Close closes the socket. Returns *CloseError on failure.
func (s *SubSocket) Close() error
```

### DealerSocket

```go
// Bind binds the socket to endpoint. Returns *BindError on failure.
func (s *DealerSocket) Bind(endpoint string) error
// Unbind detaches a previously bound endpoint. Returns *ConnectError on failure.
func (s *DealerSocket) Unbind(endpoint string) error
// Connect connects the socket to endpoint. Returns *ConnectError on failure.
func (s *DealerSocket) Connect(endpoint string) error
// Disconnect closes the connection to endpoint. Returns *ConnectError on failure.
func (s *DealerSocket) Disconnect(endpoint string) error
// DisconnectRID closes the peer selected by routing id. Returns *ConnectError on failure.
func (s *DealerSocket) DisconnectRID(rid RoutingID) error
// RoutingID / probe configuration returns *ConfigError on failure.
func (s *DealerSocket) SetRoutingID(id RoutingID) error
func (s *DealerSocket) RoutingID() (RoutingID, error)
func (s *DealerSocket) SetProbe(value bool) error
func (s *DealerSocket) SetRequestTimeout(value time.Duration) error
func (s *DealerSocket) SetWeight(value int) error
func (s *DealerSocket) Weight() (int, error)
// ChannelName metadata is a fixed logical tag used by attached channel dealers.
// It must be set before attach and becomes read-only after attach.
func (s *DealerSocket) SetChannelName(value string) error
func (s *DealerSocket) ChannelName() (string, error)
// Send submits parts on the socket. Returns (false, nil) only for temporary backpressure.
func (s *DealerSocket) Send(flags SendFlags, parts ...*Message) (bool, error)
// Recv is the canonical caller-provided storage recv.
func (s *DealerSocket) Recv(out *Received, flags RecvFlags) (bool, error)
// Request performs a synchronous request — blocks until reply or timeout.
// timeout = 0 uses the socket default timeout.
// Returns *SubmitError on submit failure, *RequestError on reply failure
// (e.g. timeout, protocol error).
func (s *DealerSocket) Request(parts [][]byte, timeout time.Duration) ([]*Message, error)
// RequestCallback performs a callback-based request submit.
// timeout = 0 uses the socket default timeout.
// Returns (false, nil) only for temporary backpressure. The callback receives a
// RequestResult which maps to *RequestError for failures.
// The reply parts slice is nil/empty on failure.
func (s *DealerSocket) RequestCallback(parts [][]byte, cb func(RequestResult, []*Message),
    flags SendFlags, timeout time.Duration) (bool, error)
// OnSendReady registers a send-ready handler. Returns *HandlerError on failure.
func (s *DealerSocket) OnSendReady(handler func()) error
// AttachDiscovery binds a discovery handle. Returns *ConfigError on failure.
func (s *DealerSocket) AttachDiscovery(discovery *Discovery) error
// Close closes the socket. Returns *CloseError on failure.
func (s *DealerSocket) Close() error
```

### RouterSocket

```go
// Bind binds the socket to endpoint. Returns *BindError on failure.
func (s *RouterSocket) Bind(endpoint string) error
// Unbind detaches a previously bound endpoint. Returns *ConnectError on failure.
func (s *RouterSocket) Unbind(endpoint string) error
// Connect connects the socket to endpoint. Returns *ConnectError on failure.
func (s *RouterSocket) Connect(endpoint string) error
// Disconnect closes the connection to endpoint. Returns *ConnectError on failure.
func (s *RouterSocket) Disconnect(endpoint string) error
// DisconnectRID closes the peer selected by routing id. Returns *ConnectError on failure.
func (s *RouterSocket) DisconnectRID(rid RoutingID) error
// RoutingID and router-specific flags return *ConfigError on failure.
func (s *RouterSocket) SetRoutingID(id RoutingID) error
func (s *RouterSocket) RoutingID() (RoutingID, error)
func (s *RouterSocket) SetMandatory(value bool) error
func (s *RouterSocket) SetHandover(value bool) error
func (s *RouterSocket) SetProbe(value bool) error
func (s *RouterSocket) SetConnectRoutingID(id RoutingID) error
func (s *RouterSocket) SetRequestTimeout(value time.Duration) error
func (s *RouterSocket) RequestTimeout() (time.Duration, error)
func (s *RouterSocket) SetWeight(value int) error
func (s *RouterSocket) Weight() (int, error)
// SendTo submits parts to a specific peer. Returns (false, nil) only for temporary backpressure.
func (s *RouterSocket) SendTo(target RoutingID, flags SendFlags, parts ...*Message) (bool, error)
// Recv is the canonical caller-provided storage recv.
func (s *RouterSocket) Recv(out *Received, flags RecvFlags) (bool, error)
// Request performs a synchronous request to a specific peer — blocks until
// reply or timeout. timeout = 0 uses the socket default timeout.
// Returns *SubmitError on submit failure, *RequestError on reply failure
// (e.g. timeout, protocol error).
func (s *RouterSocket) Request(peerRid RoutingID, parts [][]byte, timeout time.Duration) ([]*Message, error)
// RequestCallback performs a callback-based request submit to a specific peer.
// timeout = 0 uses the socket default timeout.
// Returns (false, nil) only for temporary backpressure. The callback receives a RequestResult
// which maps to *RequestError for failures.
// The reply parts slice is nil/empty on failure.
func (s *RouterSocket) RequestCallback(peerRid RoutingID, parts [][]byte,
    cb func(RequestResult, []*Message), flags SendFlags, timeout time.Duration) (bool, error)
// Reply submits a reply to a request from peer rid.
func (s *RouterSocket) Reply(rid RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) (bool, error)
// OnSendReady registers a send-ready handler. Returns *HandlerError on failure.
func (s *RouterSocket) OnSendReady(handler func()) error
// AttachDiscovery binds a discovery handle. Returns *ConfigError on failure.
func (s *RouterSocket) AttachDiscovery(discovery *Discovery) error

// --- router → spot routed send ---
// SendToSpot submits parts routed to a spot.
func (s *RouterSocket) SendToSpot(destNodeRid, destSpotRid RoutingID,
    flags SendFlags, parts ...*Message) (bool, error)

// --- router → spot routed request (callback, blocking submit) ---
// RequestToSpot submits a routed request;
// callback receives RequestResult (maps to *RequestError on completion failure).
func (s *RouterSocket) RequestToSpot(destNodeRid, destSpotRid RoutingID,
    callback RequestReplyCallback, flags SendFlags,
    timeout time.Duration, parts ...*Message) (bool, error)

// --- router → spot routed reply ---
// ReplyToSpot submits a routed reply.
func (s *RouterSocket) ReplyToSpot(destNodeRid, destSpotRid RoutingID,
    requestSeq uint64, flags SendFlags, parts ...*Message) (bool, error)

// NOTE: RouterSocket has one routed receive surface. Recv receives both
// regular ROUTER traffic and spot-origin routed traffic. Received carries
// RoutingID (source_node_rid), SpotRoutingID (source_spot_rid; set only for
// spot-origin traffic), and RequestSeq. RecvSpot and OnSpotReceive are not
// public API.

// Close closes the socket. Returns *CloseError on failure.
func (s *RouterSocket) Close() error
```

### XPubSocket

```go
// Bind binds the socket to endpoint. Returns *BindError on failure.
func (s *XPubSocket) Bind(endpoint string) error
// Unbind detaches a previously bound endpoint. Returns *ConnectError on failure.
func (s *XPubSocket) Unbind(endpoint string) error
// Connect connects the socket to endpoint. Returns *ConnectError on failure.
func (s *XPubSocket) Connect(endpoint string) error
// Disconnect closes the connection to endpoint. Returns *ConnectError on failure.
func (s *XPubSocket) Disconnect(endpoint string) error
// DisconnectRID closes the peer selected by routing id. Returns *ConnectError on failure.
func (s *XPubSocket) DisconnectRID(rid RoutingID) error
// Publish sends parts on the given topic. Returns (false, nil) only for temporary backpressure.
func (s *XPubSocket) Publish(topic string, flags SendFlags, parts ...*Message) (bool, error)
// ReceiveSubscriptionEvent receives an XPub subscription event. Returns *RecvError on failure.
func (s *XPubSocket) ReceiveSubscriptionEvent(flags RecvFlags) (*SubscriptionEvent, error)
// OnSendReady registers a send-ready handler. Returns *HandlerError on failure.
func (s *XPubSocket) OnSendReady(handler func()) error
// Option setters/getters return *ConfigError on failure.
func (s *XPubSocket) SetNoDrop(value bool) error
func (s *XPubSocket) NoDrop() (bool, error)
func (s *XPubSocket) SetVerbose(value bool) error
func (s *XPubSocket) Verbose() (bool, error)
func (s *XPubSocket) SetVerboser(value bool) error
func (s *XPubSocket) Verboser() (bool, error)
func (s *XPubSocket) SetManual(value bool) error
func (s *XPubSocket) Manual() (bool, error)
func (s *XPubSocket) SetManualLastValue(value bool) error
func (s *XPubSocket) ManualLastValue() (bool, error)
func (s *XPubSocket) SetWelcomeMessage(message *Message) error
func (s *XPubSocket) WelcomeMessage() (*Message, error)
func (s *XPubSocket) ApproveSubscribe(routingID RoutingID) error
func (s *XPubSocket) RejectSubscribe(routingID RoutingID) error
func (s *XPubSocket) PubOptions() *PubSocketOptions
// Close closes the socket. Returns *CloseError on failure.
func (s *XPubSocket) Close() error
```

### XSubSocket

```go
// Bind binds the socket to endpoint. Returns *BindError on failure.
func (s *XSubSocket) Bind(endpoint string) error
// Unbind detaches a previously bound endpoint. Returns *ConnectError on failure.
func (s *XSubSocket) Unbind(endpoint string) error
// Connect connects the socket to endpoint. Returns *ConnectError on failure.
func (s *XSubSocket) Connect(endpoint string) error
// Disconnect closes the connection to endpoint. Returns *ConnectError on failure.
func (s *XSubSocket) Disconnect(endpoint string) error
// DisconnectRID closes the peer selected by routing id. Returns *ConnectError on failure.
func (s *XSubSocket) DisconnectRID(rid RoutingID) error
// Subscription filter mutation returns *ConfigError on failure.
func (s *XSubSocket) SetSubscription(filter string) error
func (s *XSubSocket) UnsetSubscription(filter string) error
// Subscribe receives the next topic message. Returns *RecvError on failure.
func (s *XSubSocket) Subscribe(flags RecvFlags) (*TopicMessage, error)
// SubscriptionAt / TopicsCount are snapshot queries. Return *ConfigError on failure.
func (s *XSubSocket) SubscriptionAt(index int) (string, bool, error)
func (s *XSubSocket) TopicsCount() (int, error)
// Close closes the socket. Returns *CloseError on failure.
func (s *XSubSocket) Close() error
```

### StreamSocket

```go
// Bind binds the socket to endpoint. Returns *BindError on failure.
func (s *StreamSocket) Bind(endpoint string) error
// Unbind detaches a previously bound endpoint. Returns *ConnectError on failure.
func (s *StreamSocket) Unbind(endpoint string) error
// RoutingID configuration returns *ConfigError on failure.
func (s *StreamSocket) SetRoutingID(id RoutingID) error
func (s *StreamSocket) RoutingID() (RoutingID, error)
// DisconnectRID closes the peer selected by routing id. Returns *ConnectError on failure.
func (s *StreamSocket) DisconnectRID(rid RoutingID) error
// SendTo submits parts to a specific peer. Returns (false, nil) only for temporary backpressure.
func (s *StreamSocket) SendTo(target RoutingID, flags SendFlags, parts ...*Message) (bool, error)
// Two mutually-exclusive receive modes on the same StreamSocket:
//   (1) Recv, (2) OnPacket(handler). Second attach on the same stream
//   returns *HandlerError{Code: HandlerResultBusy}.
// Recv is the canonical caller-provided storage recv.
func (s *StreamSocket) Recv(out *Received, flags RecvFlags) (bool, error)
// OnPacket registers the framed packet callback mapped to
// zlink_stream_packet_handler. The wire frame is big-endian uint16
// header_size + uint32 body_size + header + body. The handler receives the
// source routing id, a header Message, and a body Message; both messages
// transfer ownership to the handler. Returns *HandlerError on failure.
func (s *StreamSocket) OnPacket(handler func(source RoutingID, header *Message, body *Message)) error
// OnSendReady registers a send-ready handler. Returns *HandlerError on failure.
func (s *StreamSocket) OnSendReady(handler func()) error
func (s *StreamSocket) BindActor(node *SpotNode, sessionRID RoutingID,
    actor ActorRef, timeout time.Duration) error
func (s *StreamSocket) UnbindActor(node *SpotNode, sessionRID RoutingID,
    timeout time.Duration) error
func (s *StreamSocket) SendBoundActor(node *SpotNode, sessionRID RoutingID,
    message *Message, flags SendFlags) (bool, error)
// Option setters/getters return *ConfigError on failure.
func (s *StreamSocket) SetNotify(value bool) error
func (s *StreamSocket) Notify() (bool, error)
func (s *StreamSocket) SetSendHWM(value int) error
func (s *StreamSocket) SendHWM() (int, error)
func (s *StreamSocket) SetRecvHWM(value int) error
func (s *StreamSocket) RecvHWM() (int, error)
func (s *StreamSocket) SetLinger(value time.Duration) error
func (s *StreamSocket) SetRecvTimeout(value time.Duration) error
func (s *StreamSocket) SetSendTimeout(value time.Duration) error
func (s *StreamSocket) SetTCPKeepalive(value bool) error
func (s *StreamSocket) SetTCPNoDelay(value bool) error
func (s *StreamSocket) SetIPv6(value bool) error
func (s *StreamSocket) LastEndpoint() (string, error)
// TLS configuration returns *ConfigError on failure.
func (s *StreamSocket) SetTLSServer(certPath, keyPath string, requireClientCert bool) error
func (s *StreamSocket) SetTLSClient(caCertPath, hostname string, trustSystem bool) error
// Close closes the socket. Returns *CloseError on failure.
func (s *StreamSocket) Close() error
```

---

## Message / Domain

### Message

```go
// NewMessage copies data into an owned message. Generic external-buffer attach
// with a release hook is not part of the Go public surface.
// Returns *ConfigError on failure.
func NewMessage(data []byte) (*Message, error)
func (m *Message) Data() []byte
func (m *Message) Size() int
func (m *Message) RefCount() int
// GetProperty reads a message property. Returns *ConfigError on failure.
func (m *Message) GetProperty(name string) (string, bool, error)
// Close releases the message. Returns *CloseError on failure.
func (m *Message) Close() error
```

### Codec Extensions

Codec adapters are separate public extension modules layered on top of the
core package. Their contract lives in
[Go Codec Extension Specification](codec.md). The root `zlink` package does
not expose codec entrypoints or require codec dependencies.

### RoutingID

Immutable binary-safe routing id value (1-255 bytes).

```go
// NewRoutingID builds a routing id from raw bytes (1-255 bytes).
// Binary-safe: the input is copied verbatim and may contain NUL.
func NewRoutingID(bytes []byte) RoutingID

// NewRoutingIDFromString parses the hex string returned by String / Hex.
// Hex input must be at most 510 chars and decode to 1-255 bytes.
// Invalid input or decoded length above 255 bytes returns the empty RoutingID value.
func NewRoutingIDFromString(value string) RoutingID

// ParseRoutingIDString parses the hex string returned by String / Hex.
// Hex input must be at most 510 chars and decode to 1-255 bytes.
// Invalid input or decoded length above 255 bytes returns *ConfigError.
func ParseRoutingIDString(value string) (RoutingID, error)

// Bytes returns the raw byte view. The returned slice must not be mutated.
func (r RoutingID) Bytes() []byte
// Size returns the byte length (1-255; 0 when empty/unset).
func (r RoutingID) Size() int

// Equal compares two routing ids byte-for-byte.
func (r RoutingID) Equal(other RoutingID) bool
// Hash returns a stable hash suitable for map keys.
func (r RoutingID) Hash() uint64

// String / Hex are convenience renderings. NewRoutingIDFromString parses
// this hex form; it does not encode arbitrary text as routing id bytes.
func (r RoutingID) String() string
func (r RoutingID) Hex() string
```

### Received

Non-topic recv result used by PAIR / DEALER / ROUTER / STREAM paths.

```go
// RoutingID returns the sender routing id (peer_rid on Router,
// source_node_rid on Spot). Empty when transport carries no source id.
func (r *Received) RoutingID() RoutingID
func (r *Received) HasRoutingID() bool
// SpotRID returns the source spot routing id. Set only on SPOT routed recv.
func (r *Received) SpotRID() RoutingID
func (r *Received) HasSpotRID() bool
// RequestSeq returns the request-reply sequence number. Zero when the
// received message is not a request (check with HasRequestSeq).
func (r *Received) RequestSeq() uint64
func (r *Received) HasRequestSeq() bool
func (r *Received) Parts() []*Message

func (r *Received) IsSinglePart() bool
// FirstPart returns parts[0] or *RecvError when parts is empty.
func (r *Received) FirstPart() (*Message, error)
// SinglePartOrError returns the only part or *RecvError when parts != 1.
	func (r *Received) SinglePartOrError() (*Message, error)

	// Send sends a regular routed message to the sender of this Received.
	func (r *Received) Send(parts []*Message) (bool, error)
	func (r *Received) SendWithFlags(parts []*Message, flags SendFlags) (bool, error)

	// Reply sends a reply for this received request. Only valid when
// HasRequestSeq() is true; otherwise returns *SubmitError for invalid
// reply context. Submit failures also return *SubmitError.
// RoutingID/SpotRID/RequestSeq are encapsulated — caller does not pass
// them again.
func (r *Received) Reply(parts []*Message) error
func (r *Received) ReplyWithFlags(parts []*Message, flags SendFlags) error

// Close releases the received bundle. Returns *CloseError on failure.
func (r *Received) Close() error
```

### TopicMessage

Topic-aware recv result used by SUB / XSUB / Spot subscribe paths.

```go
// RoutingID returns the sender routing id. Empty when the transport does
// not carry a source id (check with HasRoutingID).
func (t *TopicMessage) RoutingID() RoutingID
func (t *TopicMessage) HasRoutingID() bool
// Topic is the matched topic (UTF-8).
func (t *TopicMessage) Topic() string
func (t *TopicMessage) Parts() []*Message

func (t *TopicMessage) IsSinglePart() bool
// FirstPart returns parts[0] or *RecvError when parts is empty.
func (t *TopicMessage) FirstPart() (*Message, error)
// SinglePartOrError returns the only part or *RecvError when parts != 1.
func (t *TopicMessage) SinglePartOrError() (*Message, error)
// Close releases the topic message. Returns *CloseError on failure.
func (t *TopicMessage) Close() error
```

### SubscriptionEvent

XPub-facing subscribe/unsubscribe event and Spot subscription event recv
result. Value struct (no lifecycle).

```go
// RoutingID returns the subscriber routing id. Empty when the transport
// does not carry a source id (check with HasRoutingID).
func (s SubscriptionEvent) RoutingID() RoutingID
func (s SubscriptionEvent) HasRoutingID() bool
// Topic is the subscribed/unsubscribed topic (UTF-8).
func (s SubscriptionEvent) Topic() string
// Subscribed is true for subscribe, false for unsubscribe.
func (s SubscriptionEvent) Subscribed() bool
```

### SendFlags

```go
type SendFlags int

const (
    SendFlagsNone     SendFlags = 0
    SendFlagsDontWait SendFlags = 1
)
```

### RecvFlags

```go
type RecvFlags int

const (
    RecvFlagsNone     RecvFlags = 0
    RecvFlagsDontWait RecvFlags = 1
)
```

### SubmitResult

Submit result codes for send/request/reply/publish operations.
All failures are conveyed through `error` with a `Code() int` method.
The code is globally unique across all result enums (0-706).

```go
type SubmitResult int

const (
    SubmitOK              SubmitResult = 0
    SubmitBackpressured   SubmitResult = 1
    SubmitNotConnected    SubmitResult = 2
    SubmitNotFound        SubmitResult = 3
    SubmitTerminated      SubmitResult = 4
    SubmitInvalidHandle   SubmitResult = 5
    SubmitInvalidArgument SubmitResult = 6
    SubmitNotSupported    SubmitResult = 7
    SubmitInvalidState    SubmitResult = 8
    SubmitThreadViolation SubmitResult = 9
    SubmitOutOfMemory     SubmitResult = 10
    SubmitSeqExhausted    SubmitResult = 11
    SubmitInternalError   SubmitResult = 12
    SubmitNotAdmitted     SubmitResult = 13   // target peer has weight 0
)
```

### RequestResult

Result codes for request completion callbacks.

```go
type RequestResult int

const (
    RequestOK              RequestResult = 0
    RequestTimedOut        RequestResult = 101
    RequestNotFound        RequestResult = 102
    RequestTerminated      RequestResult = 103
    RequestProtocolError   RequestResult = 104
    RequestInternalError   RequestResult = 105
    RequestRejected        RequestResult = 106
    RequestConflict        RequestResult = 107
    RequestBusy            RequestResult = 108
    RequestNotConnected    RequestResult = 109
    RequestInvalidArgument RequestResult = 110
    RequestInvalidState    RequestResult = 111
    RequestNotSupported    RequestResult = 112
)

// RequestReplyCallback is invoked on completion of a callback-based request
// (e.g. RouterSocket.RequestToSpot, Spot.RequestChannel).
// The RequestResult conveys completion status; the []*Message slice carries
// reply parts (nil/empty on failure; non-empty only for RequestOK).
type RequestReplyCallback func(RequestResult, []*Message)
```

### RecvResult

Result codes for recv, subscribe, and subscription event operations.

```go
type RecvResult int

const (
    RecvOK            RecvResult = 0
    RecvNoData        RecvResult = 201
    RecvBusy          RecvResult = 202
    RecvTerminated    RecvResult = 203
    RecvInvalidHandle RecvResult = 204
    RecvNotSupported  RecvResult = 205
    RecvInternalError RecvResult = 206
)
```

### HandlerResult

Result codes for handler registration operations (`OnPacket`,
`OnSendReady`, `OnRoutedReceive`, `OnDispatchEvent`, `OnEvent`, etc.).

```go
type HandlerResult int

const (
    HandlerOK              HandlerResult = 0
    HandlerInvalidArgument HandlerResult = 301
    HandlerBusy            HandlerResult = 302
    HandlerNotSupported    HandlerResult = 303
    HandlerDeadlock        HandlerResult = 304
    HandlerInvalidHandle   HandlerResult = 305
    HandlerInternalError   HandlerResult = 306
)
```

### CloseResult

Result codes for close and destroy operations.

```go
type CloseResult int

const (
    CloseOK            CloseResult = 0
    CloseBusy          CloseResult = 401
    CloseShutdown      CloseResult = 402
    CloseInvalidHandle CloseResult = 403
    CloseInternalError CloseResult = 404
)
```

### BindResult

Result codes for bind operations.

```go
type BindResult int

const (
    BindOK              BindResult = 0
    BindInvalidArgument BindResult = 501
    BindAddrInUse       BindResult = 502
    BindNotSupported    BindResult = 503
    BindInvalidHandle   BindResult = 504
    BindInternalError   BindResult = 505
)
```

### ConnectResult

Result codes for connect, disconnect, and unbind operations.

```go
type ConnectResult int

const (
    ConnectOK              ConnectResult = 0
    ConnectInvalidArgument ConnectResult = 601
    ConnectNotSupported    ConnectResult = 602
    ConnectInvalidHandle   ConnectResult = 603
    ConnectInternalError   ConnectResult = 604
    ConnectNotFound        ConnectResult = 605
    ConnectConflict        ConnectResult = 606
    ConnectBusy            ConnectResult = 607
)
```

### ConfigResult

Result codes for configuration, option, and snapshot operations.

```go
type ConfigResult int

const (
    ConfigOK              ConfigResult = 0
    ConfigInvalidHandle   ConfigResult = 701
    ConfigInvalidArgument ConfigResult = 702
    ConfigNotSupported    ConfigResult = 703
    ConfigInternalError   ConfigResult = 704
    ConfigInvalidState    ConfigResult = 705
    ConfigNotFound        ConfigResult = 706
)
```

### ZlinkError

All failures are returned as `error`. The Go binding mirrors the C API's
per-function typed result enums as **eight concrete error struct types**,
one per function category. Each struct implements the `error` interface
and the common `ZlinkError` interface so callers may catch any zlink
failure with a single type assertion or narrow to a specific category.

`ZlinkError` is the common interface. The `Code() int` method returns
a globally unique code that spans all result enum ranges (0-706); the
code alone identifies the error without needing to know which enum it
belongs to. `InternalErrno() int` returns the underlying OS errno (0 if
not applicable).

```go
type ZlinkError interface {
    error
    Code() int
    InternalErrno() int
}
```

The eight concrete error types follow the pattern below. Each carries
its category-specific result code enum plus the OS errno. All implement
`ZlinkError` and (optionally) `Unwrap() error` for compatibility with
`errors.Is` / `errors.As`.

```go
type SubmitError struct {
    Result SubmitResult
    InternalErrno  int
}

func (e *SubmitError) Error() string
func (e *SubmitError) Code() int
func (e *SubmitError) Unwrap() error

type RequestError struct {
    Result RequestResult
    InternalErrno  int
}

func (e *RequestError) Error() string
func (e *RequestError) Code() int
func (e *RequestError) Unwrap() error

type RecvError struct {
    Result RecvResult
    InternalErrno  int
}

func (e *RecvError) Error() string
func (e *RecvError) Code() int
func (e *RecvError) Unwrap() error

type HandlerError struct {
    Result HandlerResult
    InternalErrno  int
}

func (e *HandlerError) Error() string
func (e *HandlerError) Code() int
func (e *HandlerError) Unwrap() error

type CloseError struct {
    Result CloseResult
    InternalErrno  int
}

func (e *CloseError) Error() string
func (e *CloseError) Code() int
func (e *CloseError) Unwrap() error

type BindError struct {
    Result BindResult
    InternalErrno  int
}

func (e *BindError) Error() string
func (e *BindError) Code() int
func (e *BindError) Unwrap() error

type ConnectError struct {
    Result ConnectResult
    InternalErrno  int
}

func (e *ConnectError) Error() string
func (e *ConnectError) Code() int
func (e *ConnectError) Unwrap() error

type ConfigError struct {
    Result ConfigResult
    InternalErrno  int
}

func (e *ConfigError) Error() string
func (e *ConfigError) Code() int
func (e *ConfigError) Unwrap() error
```

Each fallible function's Go doc comment names the concrete error type
it returns. Callers may type-assert to the concrete type for category
handling, or treat the value as `ZlinkError` / plain `error`.

---

## Monitoring

### SocketMonitor

Starts in recv model. `OnEvent(...)` transitions one-way to callback-only
model; after that `Recv()` returns a busy recv error and `Snapshot()` still works.

```go
// OpenSocketMonitor creates a monitor on the given socket. Returns *ConfigError on failure.
func OpenSocketMonitor(socket SocketTarget, events ...MonitorEventMask) (*SocketMonitor, error)
// Recv receives the next monitor event. Returns *RecvError on failure.
func (m *SocketMonitor) Recv(flags RecvFlags) (*MonitorEvent, error)
// Snapshot captures the monitor snapshot. Returns *ConfigError on failure.
func (m *SocketMonitor) Snapshot() (*MonitorSnapshot, error)
// OnEvent registers an event handler. Returns *HandlerError on failure.
func (m *SocketMonitor) OnEvent(handler func(*MonitorEvent)) error
// IgnoreMonitorHandler is a no-op callback for callback-only model. Pass it to
// (*SocketMonitor).OnEvent to keep a valid handler when the application does
// not care about events; once installed the monitor is in callback-only model
// and Recv() returns a busy recv error (Snapshot() still works). To drive the
// monitor via Snapshot() / Recv() instead, leave OnEvent unset. Maps to
// zlink_monitor_ignore_handler.
var IgnoreMonitorHandler func(MonitorEvent)
// Close closes the monitor. Returns *CloseError on failure.
func (m *SocketMonitor) Close() error
```

### MonitorSnapshot

Canonical socket monitor runtime snapshot.

```go
type MonitorSnapshot struct {
    SourceKind                          MonitorSourceKind
    StateFlags                          uint32
    DetailFlags                         uint32
    SndPendingMsgs                      uint64
    RcvPendingMsgs                      uint64
    AutoHwmEnabled                      bool
    AutoHwmProfile                      uint32
    AutoHwmRole                         uint32
    AutoHwmPolicyClass                  uint32
    AutoHwmUnitBudgetBytes              uint64
    AutoHwmSizeCap                      uint32
    AutoHwmSocketMessageSlots           uint64
    AutoHwmEffectiveMessageBytes        uint64
    AutoHwmAppliedSndHwm                int32
    AutoHwmAppliedRcvHwm                int32
    AutoHwmEffectiveSndBuf              int32
    AutoHwmEffectiveRcvBuf              int32
    AutoHwmLastRecalcMs                 uint64
    AutoHwmLastRecalcReason             uint32
    AutoHwmSendBlockedRatioPPM          uint32
    AutoHwmDeferredSndHwm               int32
    AutoHwmDeferredRcvHwm               int32
}

// IsReady returns true when the ready bit is set in StateFlags.
// Use this only for raw socket monitor sources.
func (s *MonitorSnapshot) IsReady() bool
```

### MonitorEvent

Canonical socket monitor event. `RoutingID` is the empty value when the
event has no peer (check with `HasRoutingID`).

```go
type MonitorEvent struct {
    Event      MonitorEventType // event kind (CONNECTION_READY, CONNECTED, DISCONNECTED, PEER_WEIGHT_CHANGED, ...)
    Value      uint32           // event-specific detail (PEER_WEIGHT_CHANGED carries the new 0..100 weight)
    RoutingID  RoutingID        // peer routing id (empty when not applicable)
    LocalAddr  string           // local endpoint
    RemoteAddr string           // remote endpoint
}

// MonitorEventType includes MonitorEventPeerWeightChanged (bit 15).

func (e *MonitorEvent) HasRoutingID() bool

// Convenience predicates over the Event field.
func (e *MonitorEvent) IsConnected() bool
func (e *MonitorEvent) IsDisconnected() bool
func (e *MonitorEvent) IsListening() bool
func (e *MonitorEvent) IsAccepted() bool
func (e *MonitorEvent) IsConnectionReady() bool
```

---

## Services

### Registry

```go
// Bind binds the registry endpoints. Returns *BindError on failure.
func (r *Registry) Bind(pubEndpoint, routerEndpoint string) error
// Registry configuration returns *ConfigError on failure.
func (r *Registry) SetId(registryID uint32) error
func (r *Registry) AddPeer(peerPubEndpoint string) error
func (r *Registry) SetTLSServer(certPath, keyPath string, requireClientCert bool) error
func (r *Registry) SetTLSClient(caCertPath, hostname string, trustSystem bool) error
func (r *Registry) SetHeartbeat(intervalMS, timeoutMS uint32) error
func (r *Registry) SetBroadcastInterval(intervalMS uint32) error
// Snapshot and query methods return *ConfigError on failure.
func (r *Registry) StatusSnapshot() (*RegistryStatus, error)
func (r *Registry) ServiceSummarySnapshot(filter *RegistryServiceSummaryFilter) ([]RegistryServiceSummaryEntry, error)
func (r *Registry) MemberPeers(channelName string) ([]MemberPeerEntry, error)
func (r *Registry) TopologySnapshot() ([]RegistryTopologyEntry, error)
func (r *Registry) TopologyQuery(filter *RegistryTopologyFilter) ([]RegistryTopologyEntry, error)
// Close closes the registry. Returns *CloseError on failure.
func (r *Registry) Close() error
```

### Discovery

```go
// AutoConnectType is the fixed auto-connect channel contract selected when
// a Discovery handle is created.
type AutoConnectType int
const (
    AutoConnectInvalid AutoConnectType = 0
    AutoConnectRouteMesh AutoConnectType = 1
    AutoConnectClientServer AutoConnectType = 2
    AutoConnectDealerMesh AutoConnectType = 3
    AutoConnectFanout AutoConnectType = 4
    AutoConnectSpotMesh AutoConnectType = 5
)

// ConnectRegistry connects discovery to a registry. Returns *ConnectError on failure.
func (d *Discovery) ConnectRegistry(endpoint string) error
// Discovery option getters/setters and snapshot queries return *ConfigError on failure.
func (d *Discovery) SetValue(value int64) error
func (d *Discovery) GetValue() (int64, error)
func (d *Discovery) MemberPeers() ([]MemberPeerEntry, error)
func (d *Discovery) SetTLSClient(caCertPath, hostname string, trustSystem bool) error
// SetSpotOwnerSyncEnabled enables or disables publishing SPOT owner rows to Registry.
func (d *Discovery) SetSpotOwnerSyncEnabled(enabled bool) error
// SpotOwnerSyncEnabled reports whether this Discovery publishes SPOT owner rows.
func (d *Discovery) SpotOwnerSyncEnabled() (bool, error)
// ResolveSpot resolves the current owner node routing id for a logical spot
// routing id. Intended for send/request destination lookup. Maps to
// zlink_discovery_resolve_spot. Registry-backed lookup requires the
// publishing Discovery to enable ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC.
func (d *Discovery) ResolveSpot(spotRid RoutingID) (RoutingID, error)
// ResolveActor resolves the current actor route for an actor id. Maps to
// zlink_discovery_resolve_actor.
func (d *Discovery) ResolveActor(actorID string) (ActorRoute, error)
// SetActorRouteSyncEnabled enables or disables publishing actor routes to Registry.
func (d *Discovery) SetActorRouteSyncEnabled(enabled bool) error
// ActorRouteSyncEnabled reports whether this Discovery publishes actor routes.
func (d *Discovery) ActorRouteSyncEnabled() (bool, error)
// Close closes the discovery handle. Returns *CloseError on failure.
func (d *Discovery) Close() error
```

### SpotNode

```go
type SpotNodeMode int

const (
    SpotNodeModePubSub SpotNodeMode = 1
    SpotNodeModeRouted SpotNodeMode = 2
    SpotNodeModeAll    SpotNodeMode = 3
)

type SpotNodeOptions struct {
    Mode SpotNodeMode // zero value maps to SpotNodeModeAll
}

// Bind binds the spot node endpoint. Returns *BindError on failure.
func (n *SpotNode) Bind(endpoint string) error
// ConnectPeer / DisconnectPeer manage peer links. Return *ConnectError on failure.
func (n *SpotNode) ConnectPeer(endpoint string) error
func (n *SpotNode) DisconnectPeer(endpoint string) error
func (n *SpotNode) DisconnectPeerRID(targetNodeRID RoutingID) error
// AttachDiscovery and TLS setters return *ConfigError on failure.
func (n *SpotNode) AttachDiscovery(discovery *Discovery) error
func (n *SpotNode) AttachChannelDealer(discovery *Discovery, dealer *DealerSocket) error
func (n *SpotNode) AttachChannelDealerManual(channelName string, dealer *DealerSocket) error
func (n *SpotNode) AttachPubIngress(pub *PubSocket) error
// SpotNode admission and dispatch-worker options map to the six public
// zlink_spot_node_option_t values. No raw option bag is public.
func (n *SpotNode) SetRouterHWMProfile(profile AutoHwmProfile) error
func (n *SpotNode) RouterHWMProfile() (AutoHwmProfile, error)
func (n *SpotNode) SetRouterHWM(value int) error
func (n *SpotNode) RouterHWM() (int, error)
func (n *SpotNode) SetPubSubHWMProfile(profile AutoHwmProfile) error
func (n *SpotNode) PubSubHWMProfile() (AutoHwmProfile, error)
func (n *SpotNode) SetPubSubHWM(value int) error
func (n *SpotNode) PubSubHWM() (int, error)
func (n *SpotNode) SetDispatchWorkersMin(value int) error
func (n *SpotNode) DispatchWorkersMin() (int, error)
func (n *SpotNode) SetDispatchWorkersMax(value int) error
func (n *SpotNode) DispatchWorkersMax() (int, error)
func (n *SpotNode) SetTLSServer(certPath, keyPath string, requireClientCert bool) error
func (n *SpotNode) SetTLSClient(caCertPath, hostname string, trustSystem bool) error
// SetRoutingID sets the spot node's logical address. Maps to
// zlink_set_routing_id(node, ...).
func (n *SpotNode) SetRoutingID(rid RoutingID) error
// RoutingID returns the spot node's current logical address. Maps to
// zlink_get_routing_id(node, ...).
func (n *SpotNode) RoutingID() (RoutingID, error)
// Spot factory and snapshot queries return *ConfigError on failure.
// Spot must be created only through this SpotNode factory.
func (n *SpotNode) Spot() (*Spot, error)
// EntrySpot returns an owned facade for the node-owned Entry Spot.
func (n *SpotNode) EntrySpot() (*Spot, error)
// SpotLookup returns an owned facade for a live node-local Spot routing id.
func (n *SpotNode) SpotLookup(spotRID RoutingID) (*Spot, error)
// Actor dispatch methods return typed request/config/submit errors.
func (n *SpotNode) Actor(actorID string) (*Actor, error)
func (n *SpotNode) ActorLookup(actorID string) (ActorRef, error)
func (n *SpotNode) CreateRemoteActor(targetNodeRID RoutingID, actorID string,
    message *Message, timeout time.Duration) (ActorCreateResult, error)
func (n *SpotNode) DestroyRemoteActor(actor ActorRef, timeout time.Duration) error
func (n *SpotNode) OnActorAdmission(handler func(actorID string, message *Message) ActorAdmissionResult) error
func (n *SpotNode) JoinActor(actor ActorRef, destNodeRID RoutingID,
    destSpotRID RoutingID, message *Message, callback RequestReplyCallback,
    flags SendFlags, timeout time.Duration) (bool, error)
func (n *SpotNode) LeaveActor(actor ActorRef, destSpotRID RoutingID,
    timeout time.Duration) error
func (n *SpotNode) StatusSnapshot() (*SpotNodeStatus, error)
func (n *SpotNode) PeersSnapshot() ([]SpotNodePeerEntry, error)
func (n *SpotNode) PeersQuery(filter *SpotNodePeerFilter) ([]SpotNodePeerEntry, error)
func (n *SpotNode) SubjectsSnapshot(filters ...*SpotNodeSubjectFilter) ([]SpotNodeSubjectEntry, error)
func (n *SpotNode) InternalSocketsSnapshot(filter *SpotNodeSocketSnapshotFilter) ([]SpotNodeSocketSnapshotEntry, error)
func (n *SpotNode) SpotsSnapshot() ([]SpotNodeSpotEntry, error)
func (n *SpotNode) ActorsSnapshot() ([]SpotNodeActorEntry, error)
// Close closes the spot node after cascading close to all live Spot handles.
// Returns *CloseError on failure.
func (n *SpotNode) Close() error
```

`SpotNode` owns the lifecycle. User `Spot` handles are created through
`SpotNode.Spot()`, Entry Spot facades through `SpotNode.EntrySpot()`, and
lookup facades through `SpotNode.SpotLookup()`. A returned `Spot` remains valid
only while the parent node lives.
There is no standalone `NewSpot` constructor in the public API.

`DispatchWorkersMin` must be at least `1`; `DispatchWorkersMax` must be at
least `DispatchWorkersMin`. If unset, core defaults are CPU count `1`:
`min=max=1`; otherwise `min=2`, `max=cpuCount`. These values size only the
SpotNode dispatch callback worker pool.

### Spot

```go
type SendOp interface {
    Message(message *Message) SendSubmitOp
}

type SendSubmitOp interface {
    Message(message *Message) SendSubmitOp
    Flags(flags SendFlags) SendSubmitOp
    Submit(ctx context.Context) (bool, error)
}

type RequestOp interface {
    Message(message *Message) RequestSubmitOp
}

type RequestSubmitOp interface {
    Message(message *Message) RequestSubmitOp
    Timeout(timeout time.Duration) RequestSubmitOp
    Flags(flags SendFlags) RequestCallbackSubmitOp
    Submit(ctx context.Context) ([]*Message, error)
    SubmitCallback(ctx context.Context, callback RequestReplyCallback) (bool, error)
}

type RequestCallbackSubmitOp interface {
    Message(message *Message) RequestCallbackSubmitOp
    Timeout(timeout time.Duration) RequestCallbackSubmitOp
    Flags(flags SendFlags) RequestCallbackSubmitOp
    SubmitCallback(ctx context.Context, callback RequestReplyCallback) (bool, error)
}

type ReplyOp interface {
    Message(message *Message) ReplySubmitOp
}

type ReplySubmitOp interface {
    Message(message *Message) ReplySubmitOp
    Flags(flags SendFlags) ReplySubmitOp
    Submit(ctx context.Context) error
}

// Spot is a pub/sub facade owned by SpotNode. Public Spot handles come from
// SpotNode.Spot(), SpotNode.EntrySpot(), or SpotNode.SpotLookup(...).
func (s *Spot) Publish(topic string) SendOp
func (s *Spot) SendChannel(channelName string) SendOp
func (s *Spot) RequestChannel(channelName string) RequestOp
// Subscription filter mutation returns *ConfigError on failure.
func (s *Spot) SetSubscription(filter string) error
func (s *Spot) UnsetSubscription(filter string) error
// Subscribe receives the next topic message. Returns *RecvError on failure.
func (s *Spot) Subscribe(flags RecvFlags) (*TopicMessage, error)
// ReceiveSubscriptionEvent receives the next subscription event.
// Returns *RecvError on failure.
func (s *Spot) ReceiveSubscriptionEvent(flags RecvFlags) (*SubscriptionEvent, error)
// RecvActorJoin receives the next actor join request. Returns *RecvError on failure.
func (s *Spot) RecvActorJoin(flags RecvFlags) (*ActorJoinRequest, error)
// ReplyActorJoin replies to an actor join request. Returns *SubmitError on submit failure.
func (s *Spot) ReplyActorJoin(request *ActorJoinRequest, result ActorAdmissionResult,
    flags SendFlags, message *Message) (bool, error)
// ActorsSnapshot lists actors currently joined to this Spot. Returns *ConfigError on failure.
func (s *Spot) ActorsSnapshot() ([]ActorRef, error)
// OnSendReady registers a send-ready handler. Returns *HandlerError on failure.
func (s *Spot) OnSendReady(handler func()) error
// Request timeout option returns *ConfigError on failure.
func (s *Spot) SetRequestTimeout(value time.Duration) error
func (s *Spot) RequestTimeout() (time.Duration, error)
// SetRoutingID sets the spot's logical address. Maps to
// zlink_set_routing_id(spot, ...).
func (s *Spot) SetRoutingID(rid RoutingID) error
// RoutingID returns the spot's current logical address. Maps to
// zlink_get_routing_id(spot, ...).
func (s *Spot) RoutingID() (RoutingID, error)

// --- routed send (spot → spot) ---
func (s *Spot) SendToSpot(destNodeRid, destSpotRid RoutingID) SendOp

// --- routed request (spot → spot) ---
func (s *Spot) RequestToSpot(destNodeRid, destSpotRid RoutingID) RequestOp

// --- routed request (spot → router) ---
func (s *Spot) RequestToRouter(peerRid RoutingID) RequestOp

// --- routed reply (spot → spot) ---
func (s *Spot) ReplyToSpot(destNodeRid, destSpotRid RoutingID, requestSeq uint64) ReplyOp

// --- routed reply (spot → router) ---
func (s *Spot) ReplyToRouter(peerRid RoutingID, requestSeq uint64) ReplyOp

// --- routed receive ---
// RecvRouted receives a routed message. Returns *RecvError on failure.
func (s *Spot) RecvRouted(flags RecvFlags) (*Received, error)
// OnRoutedReceive registers a routed receive handler. Returns *HandlerError on failure.
func (s *Spot) OnRoutedReceive(handler func(*Received)) error
// OnDispatchEvent registers a source-aware dispatch handler. Returns *HandlerError on failure.
func (s *Spot) OnDispatchEvent(handler func(*Spot, SpotDispatchInfo)) error
// DrainChannelReplyFrom drains pending channel reply completions for an attached dealer subject.
func (s *Spot) DrainChannelReplyFrom(dealer *DealerSocket) error

// Close closes the spot. Returns *CloseError on failure.
func (s *Spot) Close() error
```

`SendOp`, `RequestOp`, and `ReplyOp` are Go fluent operation builders.
`Message(...)` appends one multipart payload part. Submit without any payload
returns a validation error before calling native code. `Submit(ctx)` receives
the context at execution time, so the operation start methods do not take
`context.Context`. Request `Submit(ctx)` is the reply-producing form and does
not use submit flags. Callback submission uses `SubmitCallback(ctx, callback)`
and may use `Flags(...)`; it returns `(false, nil)` only for temporary
backpressure. Submit consumes the operation; reusing the same operation object
after submit returns a validation error.

### Actor

```go
// Actor is owned by a SpotNode and represents one local actor identity.
func (a *Actor) Ref() ActorRef
func (a *Actor) Join(spot *Spot, message *Message,
    callback RequestReplyCallback, flags SendFlags,
    timeout time.Duration) (bool, error)
func (a *Actor) Leave(spot *Spot, timeout time.Duration) error
func (a *Actor) RecvPart(flags RecvFlags) (*ActorPart, error)
func (a *Actor) SendBoundSession(message *Message, flags SendFlags) (bool, error)
// CloseBoundSession closes the bound session. Returns *RequestError on failure.
func (a *Actor) CloseBoundSession(timeout time.Duration) error
// Close destroys the Actor through its ActorRef. Returns *RequestError on failure.
func (a *Actor) Close() error
```

### RegistryQueryClient

```go
// Connect connects the query client. Returns *ConnectError on failure.
func (c *RegistryQueryClient) Connect(endpoint string) error
// Snapshot queries the registry topology. Returns *ConfigError on failure.
func (c *RegistryQueryClient) Snapshot(filter *RegistryTopologyFilter) ([]RegistryTopologyEntry, error)
// Close closes the query client. Returns *CloseError on failure.
func (c *RegistryQueryClient) Close() error
```

### Service-Layer Entry Types

Value structs returned by service-layer snapshot/query methods.
Each field maps 1:1 to the corresponding `zlink_*_t` C struct field,
named in Go `PascalCase`. Fixed-size C `char[N]` fields are exposed as
Go `string`; numeric ids and enums use their typed Go equivalents.

Primary entry types used in the default service flow:

```go
// MemberPeerEntry — entry from Registry.MemberPeers / Discovery.MemberPeers.
type MemberPeerEntry struct {
    AutoConnectType AutoConnectType
    ServiceRole    ServiceRole
    ChannelName    string
    Endpoint       string
    RoutingID      RoutingID
    Value          int64
    Weight uint32
}

func (e *MemberPeerEntry) HasRoutingID() bool

// RegistryTopologyEntry — entry from Registry.TopologySnapshot /
// Registry.TopologyQuery / RegistryQueryClient.Snapshot.
type RegistryTopologyEntry struct {
    AutoConnectType AutoConnectType
    RoutingID       RoutingID
    ServiceKind     ServiceKind
    ServiceRole     ServiceRole
    ChannelName     string
    Endpoint        string
    Source          TopologySource
    State           TopologyState
    DesiredCount    uint32
    ReadyCount      uint32
    ErrorCode       uint32
    LastReportedMs  uint64
}

func (e *RegistryTopologyEntry) HasRoutingID() bool

// SpotNodeStatus — status snapshot from SpotNode.StatusSnapshot.
type SpotNodeStatus struct {
    ChannelName          string
    LocalEndpoint        string
    NodeRoutingID        RoutingID
    State                SpotNodeState
    ConfiguredPeerCount  uint32
    ActivePeerCount      uint32
    ConnectedPeerCount   uint32
    SubjectCount         uint32
    ReadySubjectCount    uint32
    DisconnectedSubTargetCount    uint32
    DisconnectedRoutedTargetCount uint32
    LastError            int32
    LastChangedMs        uint64
}

func (s *SpotNodeStatus) HasNodeRoutingID() bool

// SpotDispatchInfo — source-aware payload delivered to Spot.OnDispatchEvent.
type SpotDispatchInfo struct {
    Event         SpotDispatchEvent
    SubjectKind   SpotDispatchSubjectKind
    Timer         *Timer
    ChannelDealer *DealerSocket
    Actor         *ActorRef
}

func (i *SpotDispatchInfo) RecvActorPart(flags RecvFlags) (*ActorPart, error)
```

For `SpotDispatchEventSubscribeReadable` and
`SpotDispatchEventRoutedReadable`, callers must keep draining with
`Subscribe(...)` / routed recv until the binding reports no data / `EAGAIN`.
For `SpotDispatchEventChannelReplyReadable`, `SubjectKind` is
`SpotDispatchSubjectChannelDealer`; use the attached dealer's `ChannelName()`
metadata to identify the channel and pass `ChannelDealer` to
`DrainChannelReplyFrom(...)`.
For `SpotDispatchEventActorReadable`, `Actor` identifies the readable Actor and
no native Actor pointer is part of the public contract.

Advanced / Diagnostic entry types and filters:

```go
// RegistryServiceSummaryEntry — entry from Registry.ServiceSummarySnapshot.
type RegistryServiceSummaryEntry struct {
    AutoConnectType AutoConnectType
    ServiceRole     ServiceRole
    ChannelName     string
    TotalCount      uint32
    ConnectingCount uint32
    ReadyCount      uint32
    ErrorCount      uint32
    StoppedCount    uint32
    LastReportedMs  uint64
}

// RegistryStatus — status snapshot from Registry.StatusSnapshot.
type RegistryStatus struct {
    RegistryID                 uint32
    BindEndpoint               string
    State                      RegistryState
    TopologyEntryCount         uint32
    PeerRegistryCount          uint32
    ConnectedPeerRegistryCount uint32
    ListSeq                    uint64
    LastError                  int32
    LastChangedMs              uint64
}

// SpotNodePeerEntry — entry from SpotNode.PeersSnapshot / PeersQuery.
type SpotNodePeerEntry struct {
    ChannelName      string
    LocalEndpoint    string
    PeerEndpoint     string
    Source           SpotPeerSource
    State            SpotPeerState
    Weight           uint32
    ConnectedSinceMs uint64
    LastChangedMs    uint64
}

// SpotNodeSubjectEntry — entry from SpotNode.SubjectsSnapshot.
type SpotNodeSubjectEntry struct {
    Role             SpotRole
    Subject          string
    SubjectKind      SubjectKind
    ReadyPeerCount   uint32
    ActivePeerCount  uint32
    LastChangedMs    uint64
}

// RegistryServiceSummaryFilter — optional filter for Registry.ServiceSummarySnapshot.
type RegistryServiceSummaryFilter struct {
    AutoConnectType *AutoConnectType
    ServiceRole     *ServiceRole
    ChannelName     *string
}

// RegistryTopologyFilter — optional filter for Registry.TopologyQuery /
// RegistryQueryClient.Snapshot.
type RegistryTopologyFilter struct {
    AutoConnectType *AutoConnectType
    ServiceKind     *ServiceKind
    ServiceRole     *ServiceRole
    ChannelName     *string
    RoutingID       *RoutingID
    State           *TopologyState
    Source          *TopologySource
}

// SpotNodePeerFilter — optional filter for SpotNode.PeersQuery.
type SpotNodePeerFilter struct {
    PeerEndpoint *string
    Source       *SpotPeerSource
    State        *SpotPeerState
}

// SpotNodeSubjectFilter — optional filter for SpotNode.SubjectsSnapshot.
type SpotNodeSubjectFilter struct {
    Role        *SpotRole
    Subject     *string
    SubjectKind *SubjectKind
}

// SpotNodeSocketSnapshotFilter — optional diagnostic filter for
// SpotNode.InternalSocketsSnapshot.
type SpotNodeSocketSnapshotFilter struct {
    Owner      *SpotNodeSocketOwner
    SocketType *SocketType
    SocketName *string
}

// SpotNodeSocketSnapshotEntry — diagnostic internal socket snapshot.
type SpotNodeSocketSnapshotEntry struct {
    Owner          SpotNodeSocketOwner
    OwnerID        uint64
    OwnerName      string
    SocketName     string
    SocketType     SocketType
    AutoHwmVisible bool
    Snapshot       MonitorSnapshot
}

type SpotDispatchEvent int

const (
    SpotDispatchEventSubscribeReadable    SpotDispatchEvent = 1
    SpotDispatchEventRoutedReadable       SpotDispatchEvent = 2
    SpotDispatchEventTimerReadable        SpotDispatchEvent = 3
    SpotDispatchEventChannelReplyReadable SpotDispatchEvent = 4
    SpotDispatchEventActorReadable        SpotDispatchEvent = 5
    SpotDispatchEventActorJoinReadable    SpotDispatchEvent = 6
)

type SpotDispatchSubjectKind int

const (
    SpotDispatchSubjectSpot          SpotDispatchSubjectKind = 1
    SpotDispatchSubjectTimer         SpotDispatchSubjectKind = 2
    SpotDispatchSubjectChannelDealer SpotDispatchSubjectKind = 3
    SpotDispatchSubjectActor         SpotDispatchSubjectKind = 4
)
```

---

## Timer

### Timer

```go
// NewTimer / NewTimerFromSpot allocate a timer. Return *ConfigError on failure.
func NewTimer() (*Timer, error)
func NewTimerFromSpot(spot *Spot) (*Timer, error)
// Start / Stop configure the timer. Return *ConfigError on failure.
func (t *Timer) Start(intervalNs, repeatCount uint64) error
func (t *Timer) Stop() error
// Recv drains the next timer fire. Returns *RecvError on failure.
func (t *Timer) Recv() (uint64, bool, error)
// OnFire registers a fire handler. Returns *HandlerError on failure.
func (t *Timer) OnFire(handler func(timer *Timer, fireCount uint64)) error
// Close closes the timer. Returns *CloseError on failure.
func (t *Timer) Close() error
```

---

## Poller

### Poller

Event poller for multiplexing socket, file descriptor, and timer readiness.

The current public poller contract is still generic. It does not yet return a
Spot-aware result carrying owner `Spot`, dispatch event kind, and drain
subject together.

```go
type PollerEventFlag int16
const (
    PollIn  PollerEventFlag = 1
    PollOut PollerEventFlag = 2
)

type PollerSourceKind int

// NewPoller allocates a poller. Returns *ConfigError on failure.
func NewPoller() (*Poller, error)
// Poller Add/Modify/Remove mutations return *ConfigError on failure.
func (p *Poller) AddSocket(socket SocketTarget, events PollerEventFlag, userData ...interface{}) error
func (p *Poller) ModifySocket(socket SocketTarget, events PollerEventFlag) error
func (p *Poller) RemoveSocket(socket SocketTarget) error
func (p *Poller) AddFd(fd int, events PollerEventFlag, userData ...interface{}) error
func (p *Poller) ModifyFd(fd int, events PollerEventFlag) error
func (p *Poller) RemoveFd(fd int) error
func (p *Poller) AddTimer(timer *Timer, userData ...interface{}) error
func (p *Poller) RemoveTimer(timer *Timer) error
func (p *Poller) Size() int
// Wait / WaitMany block for readiness. Return *RecvError on failure.
func (p *Poller) Wait(timeout time.Duration) (*PollerEvent, error)
func (p *Poller) WaitMany(timeout time.Duration) ([]PollerEvent, error)
// Close closes the poller. Returns *CloseError on failure.
func (p *Poller) Close() error
```

### Legacy Poll

```go
// Poll blocks until any item is ready or timeout elapses. Returns *RecvError on failure.
func Poll(items []PollItem, timeout time.Duration) (int, error)

type PollItem struct {
    Socket  SocketTarget
    Fd      int
    Events  PollerEventFlag
    REvents PollerEventFlag
}
```

### PollerEvent

```go
type PollerEvent struct {
    SourceKind PollerSourceKind
    Socket     SocketTarget
    Fd         int
    Timer      *Timer
    UserData   interface{}
    Events     PollerEventFlag
}
```

`PollOut` is send-recovery readiness shared with `OnSendReady(...)`, not a
general transport-writable bit.

---

## Utilities

### Stopwatch

High-resolution stopwatch for measuring elapsed time.

```go
func NewStopwatch() *Stopwatch
func (s *Stopwatch) Intermediate() uint64
func (s *Stopwatch) Stop() uint64
```

```go
// Has queries a runtime capability. Returns *ConfigError on failure.
func Has(capability string) (bool, error)
// Proxy / ProxySteerable run zlink proxies. Return *ConfigError on failure.
func Proxy(frontend, backend, capture SocketTarget) error
func ProxySteerable(frontend, backend, capture, control SocketTarget) error
func Sleep(seconds int)
func MultipartClose(parts []*Message)
```

### Errno / Strerror

Go errors are used throughout the binding. The C-level `zlink_errno()`
and `zlink_strerror()` are mapped internally to `ZlinkError` values
returned by every fallible function. Direct access is not exposed
because Go's `error` interface already carries the error information.

### Thread

Not wrapped: Go has goroutines and `sync` primitives that are
idiomatic and superior to the C thread API for all use cases.

### AtomicCounter

Not wrapped: Go provides `sync/atomic` in the standard library,
which covers the same use case idiomatically.

## Peer Disconnect by Routing ID

Go bindings expose `Socket.DisconnectRID(rid)` and
`SpotNode.DisconnectPeerRID(targetNodeRID)`. The duplicate policy option and
`NotFound` / `Conflict` / `Busy` connect errors mirror the C core. `Spot`
does not expose a peer-rid disconnect method.

## Core API Surface 6.0.0 Alignment

Actor create and join payloads use aggregate multipart payloads. Public binding APIs accept a message collection for remote actor create, actor join, actor join receive, and actor join reply. A single-message convenience path may remain, but it must call the multipart path internally so empty payload and one empty message stay distinguishable. Admission handlers receive a borrowed payload view that is valid only during the callback.

Registry scalar configuration uses the registry option surface as the canonical API. Bindings expose typed options for registry id, heartbeat interval, heartbeat timeout, and broadcast interval. Existing named setters may remain as compatibility aliases and must delegate to the option API.
