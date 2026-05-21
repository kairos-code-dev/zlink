[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Go Binding Implementation Blueprint

This document defines the expected Go library shape. It is not an exhaustive
list of every exported identifier. The concrete public contract source is
`bindings/go/contracts/`.

A Go implementation is aligned when the `contracts` projection, implementation
owner files, tests, samples, perf runners, and runtime behavior follow this
blueprint and map the stable capabilities of `core/include/zlink.h` into
Go-idiomatic APIs.

## Public Contract Source

- Public contract source: the public package under `bindings/go/contracts/`.
- Module projection: `bindings/go/go.mod`, the public
  `zlink.systems/zlink/contracts` package, and GoDoc for exported identifiers.
- Runtime implementation: root implementation files in `bindings/go/`, plus
  unexported helpers where a separate package would add more complexity than it
  removes.
- Native bridge: cgo bridge code, callback trampolines, request progress
  helpers, `bindings/go/include/`, and platform native artifacts under
  `bindings/go/native/`.
- Documentation role: this README defines shape and semantic coverage.
  Exported Go identifiers own the exact public member list. Each public
  identifier must still map to one of the shared contract categories.

Perf, samples, and tests must import the public `contracts` package and must not
reach into implementation-only helpers or native bridge details.

## Repository Layout

Use these paths consistently when changing the Go binding.

- Public contract: `bindings/go/contracts/`.
- Runtime implementation: root implementation files in `bindings/go/`.
- Native bridge/artifacts: root cgo bridge files, `bindings/go/native/`, and
  `bindings/go/include/`.
- Codec extensions: `bindings/go/codec/`.
- Tests: `bindings/go/tests/` and `bindings/go/*_test.go`.
- Samples: `bindings/go/samples/`.
- Perf: `bindings/go/perf/`.

Go import paths are part of the public API. The current public consumer
projection is the aggregate `zlink.systems/zlink/contracts` package. Do not
create a top-level `runtime/` package, because that would expose runtime
implementation as `zlink.systems/zlink/runtime`. If the root implementation is
later moved into `internal/`, update the `contracts` projection, samples, perf,
tests, and this README in the same change.

The following tree is normative for implementation work. Exported types,
functions, errors, enums, and builder contracts belong in the public
`contracts` package. Root implementation files own the current cgo bridge and
runtime details. cgo declarations, raw pointers, native struct mirrors,
callback trampolines, request progress helpers, and marshalling must not become
the consumer-facing entrypoint.

```text
bindings/go/
+-- go.mod
+-- doc.go
+-- *.go
+-- contracts/
|   +-- contracts.go
+-- include/
+-- native/
+-- codec/
+-- tests/
+-- samples/
+-- perf/
```

The public consumer projection is the `contracts` package. Tests, samples, and
perf must import that public package only. If an exported symbol is added,
reviewers must be able to point to its contract category owner. If code only
exists to call cgo or manage native lifetime, keep it unexported in the
implementation owner files. If that code is split into another package later,
place it under `internal/`.

## API Change Workflow

When mapping a new core capability:

1. Choose the shared contract category that owns the public behavior.
2. Add the exported type, method, or function to the public `contracts`
   package and keep its category ownership clear in source review.
3. Keep cgo calls and native state out of public signatures and consumer code.
4. Return `error` or typed errors in the normal Go style.
5. Avoid defining a provider-owned interface unless it removes real caller
   complexity.
6. Add public package tests and update samples/perf only through exported APIs.
7. Run `go vet` style checks where available and keep cgo pointer ownership
   explicit.

## Library Shape

The Go binding should follow normal Go conventions.

- Prefer concrete exported types for resources and values.
- Do not define large provider-owned interfaces for every resource type.
- Small interfaces may be defined by consumers or by the package only when
  they make call sites simpler.
- Methods return `(value, error)` or `error` where Go callers expect failure.
- No-data and temporary backpressure must be represented distinctly from hard
  errors.
- cgo handles, raw pointers, callback userdata, part-loop sequencing, and
  request pumps stay unexported.
- Use `Close()` for resource lifecycle. If a type starts background goroutines,
  its close semantics must be explicit and tested.

DTOs and values such as message, routing id, received metadata, topic message,
result values, snapshots, and option structs stay concrete.

## Contract / Runtime Placement Rules

- Exported public types, method contracts, enums, errors, and builder contracts
  belong in the public `contracts` package.
- Exported package functions, helper methods, and builder convenience helpers
  belong in the public package when callers can use them directly.
- cgo handle owners, request pumps, callback adapters, and part-loop helpers
  stay unexported in implementation files or under `internal/` if split later.
- cgo declarations, raw pointers, C struct mirrors, marshalling helpers, and
  platform loading code stay in unexported files or private native helpers.
- The exported contract package must project the contract categories, not expose
  runtime packages as import paths.
- If a runtime concrete type is exported for construction, its public behavior
  must still be described by the shared contract category.

## Contract Category Map

These categories map to exported identifiers in `bindings/go/contracts/` and are
the review ownership map for the aggregate public package.

- `Core/`: context, context options, routing id, version/capability helpers, and
  runtime utility contracts.
- `Messaging/`: message, received metadata, topic messages, subscription events,
  stream packet callbacks, and builder payload helpers.
- `Sockets/`: socket behavior, socket families, typed options, request/reply,
  and publish/subscribe surfaces.
- `Monitoring/`: monitor, monitor snapshot/event, poller, poll event, timer, and
  public poll helpers.
- `Service/`: registry, discovery, SPOT node, SPOT handle, topology models,
  actor refs, actor lifecycle, and operation builders.
- `Errors/`: exported error values or typed error domains.
- `Enums/`: public enum domains shared across the binding.

## Canonical Interface Rules

- Data-plane `Recv`, routed recv, `Subscribe`, and subscription-event receive
  fill caller-provided `*Received`, `*TopicMessage`, or `*SubscriptionEvent`
  values and return `(bool, error)`.
- Send, routed send, publish, request, reply, SPOT operations, and Actor
  location/session operations return fluent builders.
- Builder start methods take only the target identity, topic, channel, routing
  id, or request sequence. Payload, flags, timeout, callback, and async submit
  choices are builder steps.
- Multipart payload is accumulated by repeated `Message(...)` calls.
  `Messages(...)` convenience is allowed when it delegates to the same builder
  contract and is declared in the public package category.
- Do not add operation-start method families such as `SendNoWait`,
  `PublishWithFlags`, or `RequestAsync`; keep one operation name and let the
  builder absorb the variation. Terminal builder methods may use idiomatic
  names. Pass `context.Context` at submit time, not at builder start.

## Package Shape

Keep the public `contracts` package tree easy to scan.

- Core identifiers cover context, version/capability helpers, options, and
  runtime utilities.
- Messaging identifiers cover message, routing id, received metadata, topic
  message, and subscription event types.
- Socket identifiers cover pair, dealer, router, pub, sub, xpub, xsub, stream,
  options, callbacks, request/reply, publish/subscribe, and stream packet APIs.
- Monitoring identifiers cover monitor, monitor snapshot/event, poller, poll
  event, and timer.
- Service identifiers cover registry, discovery, SPOT node, SPOT handle,
  topology snapshots, actor refs, actor lifecycle, and operation builders.
- Error identifiers preserve core result domains.
- Enum identifiers cover public enum domains shared across packages.

If a helper exists only to call cgo, manage native memory, or advance request
progress, it is not exported.

## Required Capability Coverage

The Go package must cover stable user-facing core capabilities.

- Context lifecycle, context options, shutdown, auto-HWM recalculation,
  version, capability, and strerror.
- Message ownership, multipart payloads, routing ids, received metadata, topic
  messages, subscription events, and stream packet callbacks.
- All socket families and their typed options.
- Monitor, poller, timer, and readiness semantics.
- Registry, discovery, SPOT node, SPOT handle, topology snapshots, actors, and
  stream actor binding.

The public Go shape may group or rename APIs idiomatically, but it must not
change the meaning of core operations.

## Spot Get-Or-Create

Go exposes `SpotNode.GetOrCreateSpot(spotRID RoutingID) (*Spot, bool, error)`.
It maps directly to `zlink_spot_node_spot_get_or_new(...)`; it must not be
implemented by composing `SpotLookup` and `CreateSpot`.

The returned `*Spot` is caller-owned and must be closed normally. The boolean
is `true` only for the call that created the logical spot.

## Receive And Subscribe Shape

- Data-plane receive and subscribe APIs must use reusable caller-owned result
  storage.
- Nonblocking no-data must not be confused with hard failure.
- SPOT dispatch readable events are readiness notifications. Callers drain the
  matching receive API until no-data.
- Monitor and timer control-plane APIs may use value-return forms when they
  are more idiomatic and do not add hot-path allocation.
- Service control/admission receive paths such as Actor join request receive may
  use value-return forms such as `(value, bool, error)` or an equivalent typed
  result. They must still distinguish no-data from hard receive failure.

## Error And Validation Policy

- Validate routing ids, actor ids, endpoints, channel names, and topics before
  crossing the cgo boundary.
- Do not silently truncate native fixed-size values.
- Preserve submit, request, recv, handler, close, bind, connect, and config
  error domains.
- Public errors should be inspectable without native errno knowledge.

## Performance Policy

- Hot paths must not use reflection, string-based dynamic dispatch, avoidable
  allocation, avoidable byte-slice copies, hidden sleeps, busy waits, broad
  locks, or goroutine joins.
- cgo bridge code should materialize public Go values directly from the core
  part substrate.
- Do not start one goroutine or timer per request when per-handle progress can
  be shared.
- Perf measurement meaning must match `bindings/c/perf`.

## Implementation Checklist

- Public APIs are exported from the `contracts` package.
- cgo details do not leak into public signatures.
- Resource lifecycle is explicit through `Close`.
- Provider-owned interfaces are used sparingly.
- Exported helper functions and builder convenience methods are declared in the
  matching public contract package, not only in runtime helpers.
- Receive/subscription semantics match the shared binding policy.
- Service control/admission receive exceptions are documented where they differ
  from data-plane caller-provided storage.
- Perf, samples, and tests use only exported package APIs.

## Actor And Spot Route Results

Go exposes Actor and Spot route lookup results through exported value types.

- `ActorRoute` preserves the resolved Actor ref, Actor node RID, current Spot
  RID, and current Spot kind.
- `SpotRoute` preserves Spot RID, owner node RID, and Spot kind.
- `SpotKind` distinguishes Entry Spot from user Spot. Invalid kind is not a
  successful route result.
- SpotNode snapshot entries expose the same Spot kind/current Spot fields as the
  core snapshots.

Go must not add ROUTER-to-Actor or Actor-to-ROUTER direct messaging methods.
Callers compose `ResolveActor` or `ResolveSpot` with the existing Spot routed
send/request APIs.

## SpotNode Router Channel Peers

Go exposes router channel peer wiring as public `SpotNode` methods:
`ConnectRouterChannelPeer(channelName string, endpoint string) error`,
`DisconnectRouterChannelPeer(channelName string, endpoint string) error`,
`DisconnectRouterChannelPeerRID(channelName string, peerRID RoutingID) error`,
and `AttachSpotRouteChannelDiscovery(channelName string, discovery *Discovery) error`.
These methods call the matching core C APIs and use the established Go error
mapping.
