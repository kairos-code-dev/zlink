[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Go Binding Implementation Blueprint

This document defines the expected Go library shape. It is not an exhaustive
list of every exported identifier. The concrete public contract is
`bindings/go/src/zlink/Contracts/`.

A Go implementation is aligned when `Contracts/`, exported package
projections, tests, samples, perf runners, and runtime behavior follow this
blueprint and map the stable capabilities of `core/include/zlink.h` into
Go-idiomatic APIs.

## Public Contract Source

- Public contract: `bindings/go/src/zlink/Contracts/`.
- Exported package projection: exported identifiers in package `zlink`.
- Internal implementation: unexported identifiers, cgo bridge code, callback
  trampolines, request progress helpers, and any `internal/` packages.
- Documentation role: this README defines shape and semantic coverage.
  `Contracts/` owns the exact member list; GoDoc for package `zlink` must
  project it intentionally.

Perf, samples, and tests must import the public package only and must not rely
on internal packages.

## Repository Layout

Use these paths consistently when changing the Go binding.

- Public contract: `bindings/go/src/zlink/Contracts/`.
- Runtime implementation: `bindings/go/src/zlink/Runtime/`.
- Native bridge/artifacts: `bindings/go/src/zlink/Runtime/Native/`,
  `bindings/go/native/`, and `bindings/go/include/`.
- Codec extensions: `bindings/go/codec/`.
- Tests: `bindings/go/tests/` and `bindings/go/*_test.go`.
- Samples: `bindings/go/samples/`.
- Perf: `bindings/go/perf/`.

`Contracts/` and `Runtime/` are fixed repository folders. The exported
`zlink` package may still project the public API as a mostly flat Go package
when that is easier for Go users to scan. Create subpackages only for real
optional extensions.
Do not make `zlink/Contracts` or `zlink/Runtime` public import paths.

```text
bindings/go/
+-- src/
|   +-- zlink/
|   |   +-- Contracts/
|   |   |   +-- Core/
|   |   |   +-- Messaging/
|   |   |   +-- Sockets/
|   |   |   +-- Monitoring/
|   |   |   +-- Service/
|   |   |   +-- Errors/
|   |   |   +-- Enums/
|   |   +-- Runtime/
|   |   |   +-- Core/
|   |   |   +-- Messaging/
|   |   |   +-- Sockets/
|   |   |   +-- Monitoring/
|   |   |   +-- Service/
|   |   |   +-- Errors/
|   |   |   +-- Enums/
|   |   |   +-- Native/
+-- include/
+-- native/
+-- codec/
+-- tests/
+-- samples/
+-- perf/
```

## API Change Workflow

When mapping a new core capability:

1. Add the public type or method to the correct `Contracts/` category.
2. Update the exported package projection in package `zlink`.
3. Keep cgo calls and native state in unexported code.
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
  belong in `Contracts/`.
- Exported package functions, helper methods, and builder convenience helpers
  belong in `Contracts/` when callers can use them directly.
- cgo handle owners, request pumps, callback adapters, and part-loop helpers
  belong in `Runtime/`.
- cgo declarations, raw pointers, C struct mirrors, marshalling helpers, and
  platform loading code belong in `Runtime/Native/`.
- The exported `zlink` package must project `Contracts/`, not expose
  `Runtime/` as an import path.
- If a runtime concrete type is exported for construction, its public behavior
  must still be described by `Contracts/`.

## Contract Folder Layout

`Contracts/` is the source ownership map for the exported Go package.

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
  contract and is declared in `Contracts/`.
- Do not add operation-start method families such as `SendNoWait`,
  `PublishWithFlags`, or `RequestAsync`; keep one operation name and let the
  builder absorb the variation. Terminal builder methods may use idiomatic
  names. Pass `context.Context` at submit time, not at builder start.

## Package Shape

Keep package `zlink` easy to scan.

- Core: context, version/capability helpers, options, and runtime utilities.
- Messaging: message, routing id, received metadata, topic message, and
  subscription event types.
- Sockets: pair, dealer, router, pub, sub, xpub, xsub, stream, options,
  callbacks, request/reply, publish/subscribe, and stream packet APIs.
- Monitoring: monitor, monitor snapshot/event, poller, poll event, and timer.
- Service: registry, discovery, SPOT node, SPOT handle, topology snapshots,
  actor refs, actor lifecycle, and operation builders.
- Errors: exported error values or typed errors that preserve core result
  domains.

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

- Public APIs are exported from package `zlink`.
- cgo details do not leak into public signatures.
- Resource lifecycle is explicit through `Close`.
- Provider-owned interfaces are used sparingly.
- Exported helper functions and builder convenience methods are declared in
  `Contracts/`, not only in runtime helpers.
- Receive/subscription semantics match the shared binding policy.
- Service control/admission receive exceptions are documented where they differ
  from data-plane caller-provided storage.
- Perf, samples, and tests use only exported package APIs.
