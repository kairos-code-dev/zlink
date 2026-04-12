[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Go Binding Specification

This document defines the complete public API surface of the zlink Go binding.
Every type, method, and function listed here is part of the contract that the
binding must expose. Unexported methods and internal helpers are omitted.

---

## Core

### Context

```go
func NewContext() (*Context, error)
func (c *Context) Close() error
func (c *Context) Shutdown() error
func (c *Context) Options() *ContextOptions

// Socket factories
func (c *Context) PairSocket() (*PairSocket, error)
func (c *Context) PubSocket() (*PubSocket, error)
func (c *Context) SubSocket() (*SubSocket, error)
func (c *Context) DealerSocket() (*DealerSocket, error)
func (c *Context) RouterSocket() (*RouterSocket, error)
func (c *Context) XPubSocket() (*XPubSocket, error)
func (c *Context) XSubSocket() (*XSubSocket, error)
func (c *Context) StreamSocket() (*StreamSocket, error)
func (c *Context) SpotNode() (*SpotNode, error)
func (c *Context) Registry() (*Registry, error)
func (c *Context) Discovery(serviceType ServiceType, serviceName string) (*Discovery, error)
func (c *Context) RegistryQueryClient() (*RegistryQueryClient, error)
```

### ContextOptions

```go
func (o *ContextOptions) SetIOThreads(value int) error
func (o *ContextOptions) IOThreads() (int, error)
func (o *ContextOptions) SetMaxSockets(value int) error
func (o *ContextOptions) MaxSockets() (int, error)
func (o *ContextOptions) SocketLimit() (int, error)
func (o *ContextOptions) SetThreadPriority(value int) error
func (o *ContextOptions) ThreadPriority() (int, error)
func (o *ContextOptions) SetThreadSchedulingPolicy(value int) error
func (o *ContextOptions) ThreadSchedulingPolicy() (int, error)
func (o *ContextOptions) SetMaxMessageSize(value int) error
func (o *ContextOptions) MaxMessageSize() (int, error)
func (o *ContextOptions) MessageStructSize() (int, error)
func (o *ContextOptions) SetBlocky(value bool) error
func (o *ContextOptions) Blocky() (bool, error)
func (o *ContextOptions) AddThreadAffinity(cpu int) error
func (o *ContextOptions) RemoveThreadAffinity(cpu int) error
```

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

All sockets listed below share common connection and option methods
inherited from internal base types. Only the public methods are shown.

### PairSocket

```go
func (s *PairSocket) Bind(endpoint string) error
func (s *PairSocket) Unbind(endpoint string) error
func (s *PairSocket) Connect(endpoint string) error
func (s *PairSocket) Disconnect(endpoint string) error
func (s *PairSocket) Send(flags SendFlags, parts ...*Message) error
func (s *PairSocket) Recv(flags RecvFlags) (*Received, error)
func (s *PairSocket) OnReceive(handler func(*Received)) error
func (s *PairSocket) OnSendReady(handler func()) error
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
func (s *PairSocket) SetTLSServer(certPath, keyPath string, requireClientCert bool) error
func (s *PairSocket) SetTLSClient(caCertPath, hostname string, trustSystem bool) error
func (s *PairSocket) Close() error
```

### PubSocket

```go
func (s *PubSocket) Bind(endpoint string) error
func (s *PubSocket) Unbind(endpoint string) error
func (s *PubSocket) Connect(endpoint string) error
func (s *PubSocket) Disconnect(endpoint string) error
func (s *PubSocket) Publish(topic string, flags SendFlags, parts ...*Message) error
func (s *PubSocket) OnSendReady(handler func()) error
func (s *PubSocket) SetNoDrop(value bool) error
func (s *PubSocket) NoDrop() (bool, error)
func (s *PubSocket) SetVerbose(value bool) error
func (s *PubSocket) Verbose() (bool, error)
func (s *PubSocket) SetVerboser(value bool) error
func (s *PubSocket) Verboser() (bool, error)
func (s *PubSocket) SetManual(value bool) error
func (s *PubSocket) Manual() (bool, error)
func (s *PubSocket) AttachDiscovery(discovery *Discovery) error
func (s *PubSocket) Close() error
```

### SubSocket

```go
func (s *SubSocket) Bind(endpoint string) error
func (s *SubSocket) Unbind(endpoint string) error
func (s *SubSocket) Connect(endpoint string) error
func (s *SubSocket) Disconnect(endpoint string) error
func (s *SubSocket) SetSubscription(filter string) error
func (s *SubSocket) UnsetSubscription(filter string) error
func (s *SubSocket) Subscribe(flags RecvFlags) (*TopicMessage, error)
func (s *SubSocket) OnSubscribe(handler func(*TopicMessage)) error
func (s *SubSocket) AttachDiscovery(discovery *Discovery) error
func (s *SubSocket) SubscriptionAt(index int) (string, bool, error)
func (s *SubSocket) TopicsCount() (int, error)
func (s *SubSocket) Close() error
```

### DealerSocket

```go
func (s *DealerSocket) Bind(endpoint string) error
func (s *DealerSocket) Unbind(endpoint string) error
func (s *DealerSocket) Connect(endpoint string) error
func (s *DealerSocket) Disconnect(endpoint string) error
func (s *DealerSocket) SetRoutingID(id RoutingID) error
func (s *DealerSocket) RoutingID() (RoutingID, error)
func (s *DealerSocket) SetProbe(value bool) error
func (s *DealerSocket) Send(flags SendFlags, parts ...*Message) error
func (s *DealerSocket) Recv(flags RecvFlags) (*Received, error)
func (s *DealerSocket) OnReceive(handler func(*Received)) error
func (s *DealerSocket) OnSendReady(handler func()) error
func (s *DealerSocket) AttachDiscovery(discovery *Discovery) error
func (s *DealerSocket) Close() error
```

### RouterSocket

```go
func (s *RouterSocket) Bind(endpoint string) error
func (s *RouterSocket) Unbind(endpoint string) error
func (s *RouterSocket) Connect(endpoint string) error
func (s *RouterSocket) Disconnect(endpoint string) error
func (s *RouterSocket) SetRoutingID(id RoutingID) error
func (s *RouterSocket) RoutingID() (RoutingID, error)
func (s *RouterSocket) SetMandatory(value bool) error
func (s *RouterSocket) SetHandover(value bool) error
func (s *RouterSocket) SetProbe(value bool) error
func (s *RouterSocket) SetConnectRoutingID(id RoutingID) error
func (s *RouterSocket) SendTo(target RoutingID, flags SendFlags, parts ...*Message) error
func (s *RouterSocket) Recv(flags RecvFlags) (*Received, error)
func (s *RouterSocket) OnReceive(handler func(*Received)) error
func (s *RouterSocket) OnSendReady(handler func()) error
func (s *RouterSocket) AttachDiscovery(discovery *Discovery) error

// --- router → spot routed send ---
func (s *RouterSocket) SendToSpot(destNodeRid, destSpotRid RoutingID,
    flags SendFlags, parts ...*Message) error

// --- router → spot routed request (callback) ---
func (s *RouterSocket) RequestToSpot(destNodeRid, destSpotRid RoutingID,
    callback RequestReplyCallback, flags SendFlags,
    timeout time.Duration, parts ...*Message) error

// --- router → spot routed reply ---
func (s *RouterSocket) ReplyToSpot(destNodeRid, destSpotRid RoutingID,
    requestSeq uint64, flags SendFlags, parts ...*Message) error

// --- router spot receive ---
func (s *RouterSocket) RecvSpot(flags RecvFlags) (*Received, error)
func (s *RouterSocket) OnSpotReceive(handler func(sourceNodeRid,
    sourceSpotRid RoutingID, requestSeq uint64, parts []*Message)) error

func (s *RouterSocket) Close() error
```

### XPubSocket

```go
func (s *XPubSocket) Bind(endpoint string) error
func (s *XPubSocket) Unbind(endpoint string) error
func (s *XPubSocket) Connect(endpoint string) error
func (s *XPubSocket) Disconnect(endpoint string) error
func (s *XPubSocket) Publish(topic string, flags SendFlags, parts ...*Message) error
func (s *XPubSocket) ReceiveSubscriptionEvent(flags RecvFlags) (*SubscriptionEvent, error)
func (s *XPubSocket) OnSendReady(handler func()) error
func (s *XPubSocket) SetNoDrop(value bool) error
func (s *XPubSocket) NoDrop() (bool, error)
func (s *XPubSocket) SetVerbose(value bool) error
func (s *XPubSocket) Verbose() (bool, error)
func (s *XPubSocket) SetVerboser(value bool) error
func (s *XPubSocket) Verboser() (bool, error)
func (s *XPubSocket) SetManual(value bool) error
func (s *XPubSocket) Manual() (bool, error)
func (s *XPubSocket) Close() error
```

### XSubSocket

```go
func (s *XSubSocket) Bind(endpoint string) error
func (s *XSubSocket) Unbind(endpoint string) error
func (s *XSubSocket) Connect(endpoint string) error
func (s *XSubSocket) Disconnect(endpoint string) error
func (s *XSubSocket) SetSubscription(filter string) error
func (s *XSubSocket) UnsetSubscription(filter string) error
func (s *XSubSocket) Subscribe(flags RecvFlags) (*TopicMessage, error)
func (s *XSubSocket) OnSubscribe(handler func(*TopicMessage)) error
func (s *XSubSocket) SubscriptionAt(index int) (string, bool, error)
func (s *XSubSocket) TopicsCount() (int, error)
func (s *XSubSocket) Close() error
```

### StreamSocket

```go
func (s *StreamSocket) Bind(endpoint string) error
func (s *StreamSocket) Unbind(endpoint string) error
func (s *StreamSocket) SetRoutingID(id RoutingID) error
func (s *StreamSocket) RoutingID() (RoutingID, error)
func (s *StreamSocket) SendTo(target RoutingID, flags SendFlags, parts ...*Message) error
func (s *StreamSocket) Recv(flags RecvFlags) (*Received, error)
func (s *StreamSocket) OnReceive(handler func(*Received)) error
func (s *StreamSocket) OnSendReady(handler func()) error
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
func (s *StreamSocket) SetTLSServer(certPath, keyPath string, requireClientCert bool) error
func (s *StreamSocket) SetTLSClient(caCertPath, hostname string, trustSystem bool) error
func (s *StreamSocket) Close() error
```

---

## Message / Domain

### Message

```go
func NewMessage(data []byte) (*Message, error)
func (m *Message) Data() []byte
func (m *Message) Size() int
func (m *Message) RefCount() int
func (m *Message) GetProperty(name string) (string, bool, error)
func (m *Message) Close() error
```

### RoutingID

```go
func NewRoutingID(data []byte) (RoutingID, error)
func (r RoutingID) Bytes() []byte
func (r RoutingID) String() string
func (r RoutingID) Equal(other RoutingID) bool
```

### Received

```go
func (r *Received) RoutingID() RoutingID
func (r *Received) Parts() []*Message
func (r *Received) RequestSeq() (uint64, bool)
func (r *Received) SinglePartOrError() (*Message, error)
func (r *Received) Close() error
```

### TopicMessage

```go
func (t *TopicMessage) RoutingID() RoutingID
func (t *TopicMessage) Topic() string
func (t *TopicMessage) Parts() []*Message
func (t *TopicMessage) SinglePartOrError() (*Message, error)
func (t *TopicMessage) Close() error
```

### SubscriptionEvent

```go
func (s *SubscriptionEvent) RoutingID() RoutingID
func (s *SubscriptionEvent) Subscribed() bool
func (s *SubscriptionEvent) Topic() string
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
The code is globally unique across all result enums (0-703).

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
)
```

### RequestResult

Result codes for request completion callbacks.

```go
type RequestResult int

const (
    RequestOK            RequestResult = 0
    RequestTimedOut      RequestResult = 101
    RequestNotFound      RequestResult = 102
    RequestTerminated    RequestResult = 103
    RequestProtocolError RequestResult = 104
)
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
)
```

### HandlerResult

Result codes for handler registration operations (OnReceive, OnSubscribe, etc.).

```go
type HandlerResult int

const (
    HandlerOK              HandlerResult = 0
    HandlerInvalidArgument HandlerResult = 301
    HandlerBusy            HandlerResult = 302
    HandlerNotSupported    HandlerResult = 303
    HandlerDeadlock        HandlerResult = 304
    HandlerInvalidHandle   HandlerResult = 305
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
)
```

### ZlinkError

All failures are returned as `error`. The `Code() int` method returns
a globally unique code that spans all result enum ranges (0-703).
The code alone identifies the error without needing to know which
enum it belongs to.

```go
type ZlinkError struct {
    // internal fields
}

func (e *ZlinkError) Code() int
func (e *ZlinkError) Errno() int
func (e *ZlinkError) Error() string
```

---

## Request-Reply

### RequestDealer

```go
func NewRequestDealer(socket *DealerSocket) *RequestDealer
func (r *RequestDealer) Socket() *DealerSocket

// Synchronous request — blocks until reply or timeout.
// timeout = 0 uses the socket default timeout.
func (r *RequestDealer) Request(timeout time.Duration,
    parts ...*Message) (*Received, error)

// Callback request — submit may fail (returned as error).
// timeout = 0 uses the socket default timeout.
func (r *RequestDealer) RequestCallback(callback RequestReplyCallback,
    flags SendFlags, timeout time.Duration,
    parts ...*Message) error

func (r *RequestDealer) Recv(flags RecvFlags) (*Received, error)
func (r *RequestDealer) OnReceive(handler func(*Received)) error
func (r *RequestDealer) Close() error

type RequestReplyCallback func(RequestResult, *Received)
```

### RequestRouter

```go
func NewRequestRouter(socket *RouterSocket) *RequestRouter
func (r *RequestRouter) Socket() *RouterSocket

// Synchronous request — blocks until reply or timeout.
// timeout = 0 uses the socket default timeout.
func (r *RequestRouter) Request(routingID RoutingID, timeout time.Duration,
    parts ...*Message) (*Received, error)

// Callback request — submit may fail (returned as error).
// timeout = 0 uses the socket default timeout.
func (r *RequestRouter) RequestCallback(routingID RoutingID,
    callback RequestReplyCallback, flags SendFlags, timeout time.Duration,
    parts ...*Message) error

func (r *RequestRouter) Reply(routingID RoutingID, requestSeq uint64,
    flags SendFlags, parts ...*Message) error
func (r *RequestRouter) Recv(flags RecvFlags) (*Received, error)
func (r *RequestRouter) OnReceive(handler func(*Received)) error
func (r *RequestRouter) Close() error
```

---

## Monitoring

### SocketMonitor

```go
func OpenSocketMonitor(socket SocketTarget, events ...MonitorEventMask) (*SocketMonitor, error)
func (m *SocketMonitor) Recv() (*MonitorEvent, error)
func (m *SocketMonitor) Snapshot() (*MonitorSnapshot, error)
func (m *SocketMonitor) OnEvent(handler func(*MonitorEvent)) error
func (m *SocketMonitor) Close() error
```

### ServiceMonitor

```go
func (m *ServiceMonitor) Recv() (*ServiceMonitorEvent, error)
func (m *ServiceMonitor) Snapshot() (*MonitorSnapshot, error)
func (m *ServiceMonitor) OnEvent(handler func(*ServiceMonitorEvent)) error
func (m *ServiceMonitor) Close() error
```

### MonitorSnapshot

```go
type MonitorSnapshot struct {
    StateFlags     uint32
    DetailFlags    uint32
    SendPendingMsg uint64
    RecvPendingMsg uint64
}

func (s *MonitorSnapshot) IsReady() bool
```

### MonitorEvent

```go
type MonitorEvent struct {
    Event      uint64
    Value      uint64
    RoutingID  RoutingID
    LocalAddr  string
    RemoteAddr string
}

func (e *MonitorEvent) IsConnected() bool
func (e *MonitorEvent) IsDisconnected() bool
func (e *MonitorEvent) IsListening() bool
func (e *MonitorEvent) IsAccepted() bool
func (e *MonitorEvent) IsConnectionReady() bool
```

### ServiceMonitorEvent

```go
type ServiceMonitorEvent struct {
    ServiceKind uint32
    EventType   uint32
    Status      int32
    ErrorCode   int32
    Value       uint32
    DetailFlags uint32
    ServiceName string
    Endpoint    string
    RoutingID   RoutingID
    Subject     string
    SubjectKind uint32
}

type ServiceEvent = ServiceMonitorEvent
```

---

## Services

### Registry

```go
func (r *Registry) Bind(pubEndpoint, routerEndpoint string) error
func (r *Registry) SetId(registryID uint32) error
func (r *Registry) AddPeer(peerPubEndpoint string) error
func (r *Registry) SetTLSServer(certPath, keyPath string, requireClientCert bool) error
func (r *Registry) SetTLSClient(caCertPath, hostname string, trustSystem bool) error
func (r *Registry) SetHeartbeat(intervalMS, timeoutMS uint32) error
func (r *Registry) SetBroadcastInterval(intervalMS uint32) error
func (r *Registry) StatusSnapshot() (*RegistryStatus, error)
func (r *Registry) ServiceSummarySnapshot(filter *RegistryServiceSummaryFilter) ([]RegistryServiceSummaryEntry, error)
func (r *Registry) MemberPeers(serviceType ServiceType, serviceName string) ([]MemberPeerEntry, error)
func (r *Registry) MemberPeerMetadata(serviceType ServiceType, serviceName string,
    serviceRole ServiceRole, endpoint string) (*Message, error)
func (r *Registry) TopologySnapshot() ([]RegistryTopologyEntry, error)
func (r *Registry) TopologyQuery(filter *RegistryTopologyFilter) ([]RegistryTopologyEntry, error)
func (r *Registry) Close() error
```

### Discovery

```go
func (d *Discovery) ConnectRegistry(endpoint string) error
func (d *Discovery) SetValue(value int64) error
func (d *Discovery) GetValue() (int64, error)
func (d *Discovery) SetMetadata(data []byte) error
func (d *Discovery) GetMetadata() (*Message, error)
func (d *Discovery) MemberPeers() ([]MemberPeerEntry, error)
func (d *Discovery) MemberPeerMetadata(serviceRole ServiceRole, endpoint string) (*Message, error)
func (d *Discovery) MonitorOpen(events ...ServiceMonitorEventMask) (*ServiceMonitor, error)
func (d *Discovery) SetTLSClient(caCertPath, hostname string, trustSystem bool) error
func (d *Discovery) Close() error
```

### SpotNode

```go
func (n *SpotNode) Bind(endpoint string) error
func (n *SpotNode) ConnectPeer(endpoint string) error
func (n *SpotNode) DisconnectPeer(endpoint string) error
func (n *SpotNode) AttachDiscovery(discovery *Discovery) error
func (n *SpotNode) SetTLSServer(certPath, keyPath string, requireClientCert bool) error
func (n *SpotNode) SetTLSClient(caCertPath, hostname string, trustSystem bool) error
func (n *SpotNode) Spot() (*Spot, error)
func (n *SpotNode) StatusSnapshot() (*SpotNodeStatus, error)
func (n *SpotNode) PeersSnapshot() ([]SpotNodePeerEntry, error)
func (n *SpotNode) PeersQuery(filter *SpotNodePeerFilter) ([]SpotNodePeerEntry, error)
func (n *SpotNode) SubjectsSnapshot(filters ...*SpotNodeSubjectFilter) ([]SpotNodeSubjectEntry, error)
func (n *SpotNode) Close() error
```

### Spot

```go
func (s *Spot) Publish(topic string, flags SendFlags, parts ...*Message) error
func (s *Spot) SetSubscription(filter string) error
func (s *Spot) UnsetSubscription(filter string) error
func (s *Spot) Subscribe(flags RecvFlags) (*TopicMessage, error)
func (s *Spot) OnSubscribe(handler func(*TopicMessage)) error
func (s *Spot) OnSendReady(handler func()) error
func (s *Spot) SetSendHWM(value int) error
func (s *Spot) SetRecvHWM(value int) error
func (s *Spot) SetLinger(value time.Duration) error
func (s *Spot) SetRecvTimeout(value time.Duration) error
func (s *Spot) SetSendTimeout(value time.Duration) error
func (s *Spot) SetNoDrop(value bool) error

// --- routed send (spot → spot) ---
func (s *Spot) SendToSpot(destNodeRid, destSpotRid RoutingID,
    flags SendFlags, parts ...*Message) error

// --- routed request (spot → spot, callback) ---
// timeout = 0 uses the socket default timeout.
func (s *Spot) RequestToSpot(destNodeRid, destSpotRid RoutingID,
    callback RequestReplyCallback, flags SendFlags,
    timeout time.Duration, parts ...*Message) error

// --- routed reply (spot → spot) ---
func (s *Spot) ReplyToSpot(destNodeRid, destSpotRid RoutingID, requestSeq uint64,
    flags SendFlags, parts ...*Message) error

// --- routed send (spot → router) ---
func (s *Spot) SendToRouter(peerRid RoutingID, flags SendFlags, parts ...*Message) error

// --- routed request (spot → router, callback) ---
// timeout = 0 uses the socket default timeout.
func (s *Spot) RequestToRouter(peerRid RoutingID,
    callback RequestReplyCallback, flags SendFlags,
    timeout time.Duration, parts ...*Message) error

// --- routed reply (spot → router) ---
func (s *Spot) ReplyToRouter(peerRid RoutingID, requestSeq uint64,
    flags SendFlags, parts ...*Message) error

// --- routed receive ---
func (s *Spot) RecvRouted(flags RecvFlags) (*Received, error)
func (s *Spot) OnRoutedReceive(handler func(sourceRid, spotRid RoutingID,
    requestSeq uint64, parts []*Message)) error
func (s *Spot) OnDispatchEvent(handler func(event SpotDispatchEvent)) error

func (s *Spot) Close() error
```

### RegistryQueryClient

```go
func (c *RegistryQueryClient) Connect(endpoint string) error
func (c *RegistryQueryClient) Snapshot(filter *RegistryTopologyFilter) ([]RegistryTopologyEntry, error)
func (c *RegistryQueryClient) Close() error
```

---

## Timer

### Timer

```go
func NewTimer() (*Timer, error)
func NewTimerFromSpot(spot *Spot) (*Timer, error)
func (t *Timer) Start(intervalNs, repeatCount uint64) error
func (t *Timer) Stop() error
func (t *Timer) Recv(flags int) (uint64, error)
func (t *Timer) OnFire(handler func(timer *Timer, fireCount uint64)) error
func (t *Timer) Close() error
```

---

## Poller

### Poller

Event poller for multiplexing socket, file descriptor, and timer readiness.

```go
func NewPoller() (*Poller, error)
func (p *Poller) AddSocket(socket SocketTarget, events int16, userData ...interface{}) error
func (p *Poller) ModifySocket(socket SocketTarget, events int16) error
func (p *Poller) RemoveSocket(socket SocketTarget) error
func (p *Poller) AddFd(fd int, events int16, userData ...interface{}) error
func (p *Poller) ModifyFd(fd int, events int16) error
func (p *Poller) RemoveFd(fd int) error
func (p *Poller) AddTimer(timer *Timer, userData ...interface{}) error
func (p *Poller) RemoveTimer(timer *Timer) error
func (p *Poller) Size() int
func (p *Poller) Wait(timeout time.Duration) (*PollerEvent, error)
func (p *Poller) WaitAll(timeout time.Duration) ([]PollerEvent, error)
func (p *Poller) Close() error
```

### Legacy Poll

```go
func Poll(items []PollItem, timeout time.Duration) (int, error)

type PollItem struct {
    Socket  SocketTarget
    Fd      int
    Events  int16
    REvents int16
}
```

### PollerEvent

```go
type PollerEvent struct {
    SourceKind int
    Socket     SocketTarget
    Fd         int
    Timer      *Timer
    UserData   interface{}
    Events     int16
}
```

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
func Has(capability string) (bool, error)
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
