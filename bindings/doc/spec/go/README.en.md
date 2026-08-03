[English](README.en.md) | [한국어](README.ko.md)

[Spec Index](https://kairos-code-dev.github.io/zlink/en/spec/) · [Bindings Policy](../README.en.md)

# Go Binding Core 11 Public Contract

This document defines only the public contract implemented by the current Go
binding. It does not add pre-implementation designs or features that exist
only in another language. The exact Go identifiers and method signatures are
checked against `bindings/go/contracts/` and the equivalent projection in the
module root.

## Module and public packages

The Go module import path is `zlink.systems/zlink/v11`. A normal consumer
imports the root `zlink` package. `zlink.systems/zlink/v11/contracts` is a
public projection that groups the same contract, and the root package exports
that projection.

Runtime handles, cgo declarations, native structures, callback trampolines,
the request progress pump, and buffer marshalling are implementation details
under `internal/native`. These types and packages are not consumer contracts.

The package projects only the Core 11 raw C API. It includes Context, Message,
raw sockets, monitoring, polling, timers, and utilities. It does not include
Spot, Actor, MeshNode, or service operations. The Go module has no
message-specific codec registration API. The binding-provided typed API is the
default path for Message and byte payloads.

## Public contract categories

| Category | Main public concepts |
|----------|---------------------|
| Core | `Context`, `ContextOptions`, version/capability, `RoutingID`, utilities |
| Messaging | `Message`, `Received`, `TopicMessage`, `SubscriptionEvent`, multipart helpers |
| Sockets | Pair, PUB, SUB, DEALER, ROUTER, XPUB, XSUB, STREAM, typed options and operation builders |
| Eventing | `SocketMonitor`, `MonitorEvent`, `MonitorStatus`, `Poller`, `PollEvent`, `Timer` |
| Errors | Function-group error types, results, and result codes |

Socket capabilities belong to the concrete socket type that provides them. A
method is not added to every socket merely to make the surface uniform, and a
socket does not expose a capability absent from the raw Core contract.

## Context and resource lifetime

The Context created by `NewContext` owns sockets and context-wide options.
Closing the Context also requests termination for sockets that remain open.
The caller owns Context, socket, monitor, poller, timer, and utility resources
and closes each resource through `Close` or its specified termination method.
Closing the same resource repeatedly does not release an already-closed
resource again.

Context options configure I/O threads and socket defaults. Auto-HWM message
units are passed using the `uint64` storage required by the Core contract. The
public Go method accepts `int`, but rejects negative values and values outside
the platform `uint64` range before configuration.

## Message and ownership

`NewMessage` and `NewMessageWithSize` create native message storage owned by
the Core. The input bytes passed to `NewMessage` are copied into that storage.
`Message.Data` returns a native payload view that is valid only while the
message is open. `Message.Bytes` creates a snapshot when the data must outlive
the Message.

Adding a `Message` to a builder preserves the caller message when submission
fails and consumes it when submission succeeds. `MoveMessage` explicitly
transfers ownership at submission; after it returns, the caller must not
assume that the original message can be reused. `Bytes` reads the caller slice
during submission and does not retain the slice after submission returns.

Message parts in receive results are owned by the Go wrapper. Parts delivered
through `Received`, `TopicMessage`, `SubscriptionEvent`, and request completion
callbacks must be closed explicitly after use. A receive method that accepts a
caller-provided output first clears its existing parts, then fills it with new
native parts and metadata.

## Socket operations

Send, publish, request, and reply use multipart builders. A builder collects
payloads and flags and runs once at its terminal `Submit` method. Reusing a
builder's terminal method is not supported.

The current terminal signatures are:

```go
// Message, MoveMessage, and Bytes append payload parts.
type SendSubmitOp interface {
    Message(*Message) SendSubmitOp
    MoveMessage(*Message) SendSubmitOp
    Bytes([]byte) SendSubmitOp
    Flags(SendFlags) SendSubmitOp
    Submit(context.Context) (bool, error)
}

// A request chooses either a callback or a completion channel.
type RequestSubmitOp interface {
    Message(*Message) RequestSubmitOp
    Bytes([]byte) RequestSubmitOp
    Timeout(time.Duration) RequestSubmitOp
    Flags(SendFlags) RequestCallbackSubmitOp
    SubmitAsync(context.Context) (<-chan RequestReplyCompletion, error)
    Submit(context.Context, RequestReplyCallback) (bool, error)
}

// The reply builder created by Received.Reply returns only an error on success.
type ReplySubmitOp interface {
    Message(*Message) ReplySubmitOp
    Flags(SendFlags) ReplySubmitOp
    Submit(context.Context) error
}
```

`SendFlagsDontWait` avoids blocking. Temporary backpressure is represented by
`false, nil`; connection loss, invalid arguments, and Core termination are
returned as function-group errors. Only no-data from a non-blocking receive is
represented by `false, nil`. Other receive failures return an error.

Pair and DEALER provide `Send`; PUB and XPUB provide `Publish`; ROUTER and
STREAM provide send operations that accept a destination routing ID. DEALER
and ROUTER provide request operations. When ROUTER receives request metadata,
that metadata is used to create the reply operation. STREAM provides raw TCP
packet callbacks and caller-provided receive.

`RecvPart`, `SubscribePart`, and packet callbacks are public operations over
the Core raw-part substrate. Callers do not handle native `zlink_msg_t` values
or raw pointers directly.

## Receive and eventing

A caller-provided receive method returns `(bool, error)`. `false` with a nil
error means that `RecvFlagsDontWait` found no data. `true` means that one or
more results were filled into the output. Actual failures are represented by
`*RecvError`.

A socket monitor is opened with a typed event mask and reports
`MonitorEvent` and `MonitorStatus`. A Poller reports readiness for sockets,
file descriptors, and timer sources through `PollEvent`. A Timer can provide
interval events through a Poller or direct receive. Monitor, poller, and timer
callbacks or event results do not expose the native callback thread as the
public consumer callback execution location.

## Error contract

Every function-group error implements `error` and the following public
interface:

```go
type ZlinkError interface {
    error
    Code() int
    InternalErrno() int
}
```

The current concrete error types are `SubmitError`, `RequestError`,
`RecvError`, `HandlerError`, `CloseError`, `BindError`, `ConnectError`, and
`ConfigError`. `Code()` returns the Core result code for the function group,
and `InternalErrno()` returns the native failure cause. `Unwrap()` supports
`errors.Is`. A `NativeErrno` field or `NativeErrno()` alias is not public
contract.

If the Context is already canceled or its deadline has elapsed before a
terminal method is called, the method returns `context.Canceled` or
`context.DeadlineExceeded`. These standard errors are not converted into a
function-group Core error. After native submission is accepted, request
completion is delivered through `RequestReplyCompletion` or the callback's
`RequestResult`. The combined policy for submit return values and cancellation
after completion remains a review item until the Go/Rust submit draft is
approved as the common contract.

## FFI and package boundary

The include path for the Go cgo bridge is fixed to the package-local
`include/` directory. A package consumer does not read the repository's
`core/include`. `bindings/go/tests/raw-core11-allowlist.json` fixes the header
file set, SHA-256 values, cgo raw symbols, and local callback helpers in a
machine-readable form. `zlink/service/` and former service symbols are not in
the allowlist.

The module package uses this file-proxy layout:

```text
zlink.systems/zlink/v11/@v/v11.1.0.info
zlink.systems/zlink/v11/@v/v11.1.0.mod
zlink.systems/zlink/v11/@v/v11.1.0.zip
```

Supported platform runtimes are under `native/<platform>/` in the module. A
package consumer must use the runtime from the module cache without a
`replace` directive and without the repository's `core/build` directory.

## Excluded from the public contract

- Spot, Actor, MeshNode, and service operations
- Core 10 compatibility aliases and service headers
- Private cgo types, native pointers, callback userdata, and the progress pump
- Message-specific codec registries or caller-side raw encode/decode workarounds
- `NativeErrno` and the former module path `zlink.systems/zlink`

The current verification entry points for GoDoc and process samples are
`bindings/go/README.godoc.md`, `bindings/go/tests/run_tests.sh`, and
`bindings/go/samples/run_samples.sh`. Before changing this public contract,
check the review state of the common binding specification and related drafts.
