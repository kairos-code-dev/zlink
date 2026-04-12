[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Go Binding Specification

This document defines the complete public API surface of the zlink Go binding.
Every type, method, and function listed here is part of the contract that the
binding must expose. Unexported methods and internal helpers are omitted.

---

## Core

### Context

```go
// NewContext creates a new zlink context. Returns *ConfigError on failure.
func NewContext() (*Context, error)
// Close terminates the context. Returns *CloseError on failure.
func (c *Context) Close() error
// Shutdown requests a graceful shutdown. Returns *CloseError on failure.
func (c *Context) Shutdown() error
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
func (c *Context) Registry() (*Registry, error)
func (c *Context) Discovery(serviceType ServiceType, serviceName string) (*Discovery, error)
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
// Bind binds the socket to endpoint. Returns *BindError on failure.
func (s *PairSocket) Bind(endpoint string) error
// Unbind detaches a previously bound endpoint. Returns *ConnectError on failure.
func (s *PairSocket) Unbind(endpoint string) error
// Connect connects the socket to endpoint. Returns *ConnectError on failure.
func (s *PairSocket) Connect(endpoint string) error
// Disconnect closes the connection to endpoint. Returns *ConnectError on failure.
func (s *PairSocket) Disconnect(endpoint string) error
// Send submits parts on the socket. Returns *SubmitError on failure.
func (s *PairSocket) Send(flags SendFlags, parts ...*Message) error
// Recv receives a message. Returns *RecvError on failure.
func (s *PairSocket) Recv(flags RecvFlags) (*Received, error)
// OnReceive registers a receive handler. Returns *HandlerError on failure.
func (s *PairSocket) OnReceive(handler func(*Received)) error
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
// Publish sends parts on the given topic. Returns *SubmitError on failure.
func (s *PubSocket) Publish(topic string, flags SendFlags, parts ...*Message) error
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
// Subscription filter mutation returns *ConfigError on failure.
func (s *SubSocket) SetSubscription(filter string) error
func (s *SubSocket) UnsetSubscription(filter string) error
// Subscribe receives the next topic message. Returns *RecvError on failure.
func (s *SubSocket) Subscribe(flags RecvFlags) (*TopicMessage, error)
// OnSubscribe registers a subscription handler. Returns *HandlerError on failure.
func (s *SubSocket) OnSubscribe(handler func(*TopicMessage)) error
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
// RoutingID / probe configuration returns *ConfigError on failure.
func (s *DealerSocket) SetRoutingID(id RoutingID) error
func (s *DealerSocket) RoutingID() (RoutingID, error)
func (s *DealerSocket) SetProbe(value bool) error
// Send submits parts on the socket. Returns *SubmitError on failure.
func (s *DealerSocket) Send(flags SendFlags, parts ...*Message) error
// Recv receives a message. Returns *RecvError on failure.
func (s *DealerSocket) Recv(flags RecvFlags) (*Received, error)
// Request performs a synchronous request — blocks until reply or timeout.
// timeout = 0 uses the socket default timeout.
// Returns *SubmitError on submit failure, *RequestError on reply failure
// (e.g. timeout, protocol error).
func (s *DealerSocket) Request(parts [][]byte, timeout time.Duration) (Received, error)
// RequestCallback performs a callback-based request — submit may fail
// (returned as error). timeout = 0 uses the socket default timeout.
// Returns *SubmitError on submit failure; the callback receives a
// RequestResult which maps to *RequestError for failures.
func (s *DealerSocket) RequestCallback(parts [][]byte, cb func(RequestResult, *Received), flags SendFlags, timeout time.Duration) error
// OnReceive registers a receive handler. Returns *HandlerError on failure.
func (s *DealerSocket) OnReceive(handler func(*Received)) error
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
// RoutingID and router-specific flags return *ConfigError on failure.
func (s *RouterSocket) SetRoutingID(id RoutingID) error
func (s *RouterSocket) RoutingID() (RoutingID, error)
func (s *RouterSocket) SetMandatory(value bool) error
func (s *RouterSocket) SetHandover(value bool) error
func (s *RouterSocket) SetProbe(value bool) error
func (s *RouterSocket) SetConnectRoutingID(id RoutingID) error
// SendTo submits parts to a specific peer. Returns *SubmitError on failure.
func (s *RouterSocket) SendTo(target RoutingID, flags SendFlags, parts ...*Message) error
// Recv receives a message. Returns *RecvError on failure.
func (s *RouterSocket) Recv(flags RecvFlags) (*Received, error)
// Request performs a synchronous request to a specific peer — blocks until
// reply or timeout. timeout = 0 uses the socket default timeout.
// Returns *SubmitError on submit failure, *RequestError on reply failure
// (e.g. timeout, protocol error).
func (s *RouterSocket) Request(peerRid RoutingID, parts [][]byte, timeout time.Duration) (Received, error)
// RequestCallback performs a callback-based request to a specific peer —
// submit may fail (returned as error). timeout = 0 uses the socket default
// timeout. Returns *SubmitError on submit failure; the callback receives a
// RequestResult which maps to *RequestError for failures.
func (s *RouterSocket) RequestCallback(peerRid RoutingID, parts [][]byte, cb func(RequestResult, *Received), flags SendFlags, timeout time.Duration) error
// Reply submits a reply to a request from peer rid. Returns *SubmitError on failure.
func (s *RouterSocket) Reply(rid RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) error
// OnReceive registers a receive handler. Returns *HandlerError on failure.
func (s *RouterSocket) OnReceive(handler func(*Received)) error
// OnSendReady registers a send-ready handler. Returns *HandlerError on failure.
func (s *RouterSocket) OnSendReady(handler func()) error
// AttachDiscovery binds a discovery handle. Returns *ConfigError on failure.
func (s *RouterSocket) AttachDiscovery(discovery *Discovery) error

// --- router → spot routed send ---
// SendToSpot submits parts routed to a spot. Returns *SubmitError on failure.
func (s *RouterSocket) SendToSpot(destNodeRid, destSpotRid RoutingID,
    flags SendFlags, parts ...*Message) error

// --- router → spot routed request (callback) ---
// RequestToSpot submits a routed request. Returns *SubmitError on submit failure;
// callback receives RequestResult (maps to *RequestError on completion failure).
func (s *RouterSocket) RequestToSpot(destNodeRid, destSpotRid RoutingID,
    callback RequestReplyCallback, flags SendFlags,
    timeout time.Duration, parts ...*Message) error

// --- router → spot routed reply ---
// ReplyToSpot submits a routed reply. Returns *SubmitError on failure.
func (s *RouterSocket) ReplyToSpot(destNodeRid, destSpotRid RoutingID,
    requestSeq uint64, flags SendFlags, parts ...*Message) error

// --- router spot receive ---
// RecvSpot receives a routed spot message. Returns *RecvError on failure.
func (s *RouterSocket) RecvSpot(flags RecvFlags) (*Received, error)
// OnSpotReceive registers a spot receive handler. Returns *HandlerError on failure.
func (s *RouterSocket) OnSpotReceive(handler func(sourceNodeRid,
    sourceSpotRid RoutingID, requestSeq uint64, parts []*Message)) error

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
// Publish sends parts on the given topic. Returns *SubmitError on failure.
func (s *XPubSocket) Publish(topic string, flags SendFlags, parts ...*Message) error
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
// Subscription filter mutation returns *ConfigError on failure.
func (s *XSubSocket) SetSubscription(filter string) error
func (s *XSubSocket) UnsetSubscription(filter string) error
// Subscribe receives the next topic message. Returns *RecvError on failure.
func (s *XSubSocket) Subscribe(flags RecvFlags) (*TopicMessage, error)
// OnSubscribe registers a subscription handler. Returns *HandlerError on failure.
func (s *XSubSocket) OnSubscribe(handler func(*TopicMessage)) error
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
// SendTo submits parts to a specific peer. Returns *SubmitError on failure.
func (s *StreamSocket) SendTo(target RoutingID, flags SendFlags, parts ...*Message) error
// Recv receives a message. Returns *RecvError on failure.
func (s *StreamSocket) Recv(flags RecvFlags) (*Received, error)
// OnReceive registers a receive handler. Returns *HandlerError on failure.
func (s *StreamSocket) OnReceive(handler func(*Received)) error
// OnSendReady registers a send-ready handler. Returns *HandlerError on failure.
func (s *StreamSocket) OnSendReady(handler func()) error
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
// NewMessage allocates a new message from data. Returns *ConfigError on failure.
func NewMessage(data []byte) (*Message, error)
func (m *Message) Data() []byte
func (m *Message) Size() int
func (m *Message) RefCount() int
// GetProperty reads a message property. Returns *ConfigError on failure.
func (m *Message) GetProperty(name string) (string, bool, error)
// Close releases the message. Returns *CloseError on failure.
func (m *Message) Close() error
```

### RoutingID

```go
// NewRoutingID builds a routing id from bytes. Returns *ConfigError on failure.
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
// SinglePartOrError returns the only part or a *ConfigError when parts != 1.
func (r *Received) SinglePartOrError() (*Message, error)
// Close releases the received bundle. Returns *CloseError on failure.
func (r *Received) Close() error
```

### TopicMessage

```go
func (t *TopicMessage) RoutingID() RoutingID
func (t *TopicMessage) Topic() string
func (t *TopicMessage) Parts() []*Message
// SinglePartOrError returns the only part or a *ConfigError when parts != 1.
func (t *TopicMessage) SinglePartOrError() (*Message, error)
// Close releases the topic message. Returns *CloseError on failure.
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

// RequestReplyCallback is invoked on completion of a callback-based request
// (e.g. RouterSocket.RequestToSpot, Spot.RequestToSpot, Spot.RequestToRouter).
// The RequestResult conveys completion status; the *Received carries reply
// parts (non-nil only for RequestOK).
type RequestReplyCallback func(RequestResult, *Received)
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

All failures are returned as `error`. The Go binding mirrors the C API's
per-function typed result enums as **eight concrete error struct types**,
one per function category. Each struct implements the `error` interface
and the common `ZlinkError` interface so callers may catch any zlink
failure with a single type assertion or narrow to a specific category.

`ZlinkError` is the common interface. The `Code() int` method returns
a globally unique code that spans all result enum ranges (0-703); the
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

```go
// OpenSocketMonitor creates a monitor on the given socket. Returns *ConfigError on failure.
func OpenSocketMonitor(socket SocketTarget, events ...MonitorEventMask) (*SocketMonitor, error)
// Recv receives the next monitor event. Returns *RecvError on failure.
func (m *SocketMonitor) Recv() (*MonitorEvent, error)
// Snapshot captures the monitor snapshot. Returns *ConfigError on failure.
func (m *SocketMonitor) Snapshot() (*MonitorSnapshot, error)
// OnEvent registers an event handler. Returns *HandlerError on failure.
func (m *SocketMonitor) OnEvent(handler func(*MonitorEvent)) error
// Close closes the monitor. Returns *CloseError on failure.
func (m *SocketMonitor) Close() error
```

### ServiceMonitor

```go
// Recv receives the next service event. Returns *RecvError on failure.
func (m *ServiceMonitor) Recv() (*ServiceMonitorEvent, error)
// Snapshot captures the monitor snapshot. Returns *ConfigError on failure.
func (m *ServiceMonitor) Snapshot() (*MonitorSnapshot, error)
// OnEvent registers an event handler. Returns *HandlerError on failure.
func (m *ServiceMonitor) OnEvent(handler func(*ServiceMonitorEvent)) error
// Close closes the monitor. Returns *CloseError on failure.
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
func (r *Registry) MemberPeers(serviceType ServiceType, serviceName string) ([]MemberPeerEntry, error)
func (r *Registry) MemberPeerMetadata(serviceType ServiceType, serviceName string,
    serviceRole ServiceRole, endpoint string) (*Message, error)
func (r *Registry) TopologySnapshot() ([]RegistryTopologyEntry, error)
func (r *Registry) TopologyQuery(filter *RegistryTopologyFilter) ([]RegistryTopologyEntry, error)
// Close closes the registry. Returns *CloseError on failure.
func (r *Registry) Close() error
```

### Discovery

```go
// ConnectRegistry connects discovery to a registry. Returns *ConnectError on failure.
func (d *Discovery) ConnectRegistry(endpoint string) error
// Discovery option getters/setters and snapshot queries return *ConfigError on failure.
func (d *Discovery) SetValue(value int64) error
func (d *Discovery) GetValue() (int64, error)
func (d *Discovery) SetMetadata(data []byte) error
func (d *Discovery) GetMetadata() (*Message, error)
func (d *Discovery) MemberPeers() ([]MemberPeerEntry, error)
func (d *Discovery) MemberPeerMetadata(serviceRole ServiceRole, endpoint string) (*Message, error)
func (d *Discovery) MonitorOpen(events ...ServiceMonitorEventMask) (*ServiceMonitor, error)
func (d *Discovery) SetTLSClient(caCertPath, hostname string, trustSystem bool) error
// Close closes the discovery handle. Returns *CloseError on failure.
func (d *Discovery) Close() error
```

### SpotNode

```go
// Bind binds the spot node endpoint. Returns *BindError on failure.
func (n *SpotNode) Bind(endpoint string) error
// ConnectPeer / DisconnectPeer manage peer links. Return *ConnectError on failure.
func (n *SpotNode) ConnectPeer(endpoint string) error
func (n *SpotNode) DisconnectPeer(endpoint string) error
// AttachDiscovery and TLS setters return *ConfigError on failure.
func (n *SpotNode) AttachDiscovery(discovery *Discovery) error
func (n *SpotNode) SetTLSServer(certPath, keyPath string, requireClientCert bool) error
func (n *SpotNode) SetTLSClient(caCertPath, hostname string, trustSystem bool) error
// Spot factory and snapshot queries return *ConfigError on failure.
func (n *SpotNode) Spot() (*Spot, error)
func (n *SpotNode) StatusSnapshot() (*SpotNodeStatus, error)
func (n *SpotNode) PeersSnapshot() ([]SpotNodePeerEntry, error)
func (n *SpotNode) PeersQuery(filter *SpotNodePeerFilter) ([]SpotNodePeerEntry, error)
func (n *SpotNode) SubjectsSnapshot(filters ...*SpotNodeSubjectFilter) ([]SpotNodeSubjectEntry, error)
// Close closes the spot node. Returns *CloseError on failure.
func (n *SpotNode) Close() error
```

### Spot

```go
// Publish sends parts on the given topic. Returns *SubmitError on failure.
func (s *Spot) Publish(topic string, flags SendFlags, parts ...*Message) error
// Subscription filter mutation returns *ConfigError on failure.
func (s *Spot) SetSubscription(filter string) error
func (s *Spot) UnsetSubscription(filter string) error
// Subscribe receives the next topic message. Returns *RecvError on failure.
func (s *Spot) Subscribe(flags RecvFlags) (*TopicMessage, error)
// OnSubscribe registers a subscription handler. Returns *HandlerError on failure.
func (s *Spot) OnSubscribe(handler func(*TopicMessage)) error
// OnSendReady registers a send-ready handler. Returns *HandlerError on failure.
func (s *Spot) OnSendReady(handler func()) error
// Option setters return *ConfigError on failure.
func (s *Spot) SetSendHWM(value int) error
func (s *Spot) SetRecvHWM(value int) error
func (s *Spot) SetLinger(value time.Duration) error
func (s *Spot) SetRecvTimeout(value time.Duration) error
func (s *Spot) SetSendTimeout(value time.Duration) error
func (s *Spot) SetNoDrop(value bool) error

// --- routed send (spot → spot) ---
// SendToSpot submits parts routed to a spot. Returns *SubmitError on failure.
func (s *Spot) SendToSpot(destNodeRid, destSpotRid RoutingID,
    flags SendFlags, parts ...*Message) error

// --- routed request (spot → spot, callback) ---
// timeout = 0 uses the socket default timeout.
// Returns *SubmitError on submit failure; the callback receives a
// RequestResult which maps to *RequestError for failures.
func (s *Spot) RequestToSpot(destNodeRid, destSpotRid RoutingID,
    callback RequestReplyCallback, flags SendFlags,
    timeout time.Duration, parts ...*Message) error

// --- routed reply (spot → spot) ---
// ReplyToSpot submits a routed reply. Returns *SubmitError on failure.
func (s *Spot) ReplyToSpot(destNodeRid, destSpotRid RoutingID, requestSeq uint64,
    flags SendFlags, parts ...*Message) error

// --- routed send (spot → router) ---
// SendToRouter submits parts routed to a router peer. Returns *SubmitError on failure.
func (s *Spot) SendToRouter(peerRid RoutingID, flags SendFlags, parts ...*Message) error

// --- routed request (spot → router, callback) ---
// timeout = 0 uses the socket default timeout.
// Returns *SubmitError on submit failure; the callback receives a
// RequestResult which maps to *RequestError for failures.
func (s *Spot) RequestToRouter(peerRid RoutingID,
    callback RequestReplyCallback, flags SendFlags,
    timeout time.Duration, parts ...*Message) error

// --- routed reply (spot → router) ---
// ReplyToRouter submits a routed reply. Returns *SubmitError on failure.
func (s *Spot) ReplyToRouter(peerRid RoutingID, requestSeq uint64,
    flags SendFlags, parts ...*Message) error

// --- routed receive ---
// RecvRouted receives a routed message. Returns *RecvError on failure.
func (s *Spot) RecvRouted(flags RecvFlags) (*Received, error)
// OnRoutedReceive registers a routed receive handler. Returns *HandlerError on failure.
func (s *Spot) OnRoutedReceive(handler func(sourceRid, spotRid RoutingID,
    requestSeq uint64, parts []*Message)) error
// OnDispatchEvent registers a dispatch event handler. Returns *HandlerError on failure.
func (s *Spot) OnDispatchEvent(handler func(event SpotDispatchEvent)) error

// Close closes the spot. Returns *CloseError on failure.
func (s *Spot) Close() error
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
func (t *Timer) Recv(flags int) (uint64, error)
// OnFire registers a fire handler. Returns *HandlerError on failure.
func (t *Timer) OnFire(handler func(timer *Timer, fireCount uint64)) error
// Close closes the timer. Returns *CloseError on failure.
func (t *Timer) Close() error
```

---

## Poller

### Poller

Event poller for multiplexing socket, file descriptor, and timer readiness.

```go
// NewPoller allocates a poller. Returns *ConfigError on failure.
func NewPoller() (*Poller, error)
// Poller Add/Modify/Remove mutations return *ConfigError on failure.
func (p *Poller) AddSocket(socket SocketTarget, events int16, userData ...interface{}) error
func (p *Poller) ModifySocket(socket SocketTarget, events int16) error
func (p *Poller) RemoveSocket(socket SocketTarget) error
func (p *Poller) AddFd(fd int, events int16, userData ...interface{}) error
func (p *Poller) ModifyFd(fd int, events int16) error
func (p *Poller) RemoveFd(fd int) error
func (p *Poller) AddTimer(timer *Timer, userData ...interface{}) error
func (p *Poller) RemoveTimer(timer *Timer) error
func (p *Poller) Size() int
// Wait / WaitAll block for readiness. Return *RecvError on failure.
func (p *Poller) Wait(timeout time.Duration) (*PollerEvent, error)
func (p *Poller) WaitAll(timeout time.Duration) ([]PollerEvent, error)
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
