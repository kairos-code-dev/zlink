[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Rust Binding Implementation Blueprint

This document defines the expected Rust crate shape. It is not an exhaustive
list of every public item. The concrete public contract is
`bindings/rust/src/zlink/Contracts/`.

A Rust implementation is aligned when `Contracts/`, crate export projections,
rustdoc, tests, samples, perf runners, and runtime behavior follow this
blueprint and map stable `core/include/zlink.h` capabilities into
Rust-idiomatic APIs.

## Public Contract Source

- Public contract: `bindings/rust/src/zlink/Contracts/`.
- Crate projection: items exported from `lib.rs` and documented public modules.
- Internal implementation: private modules, `pub(crate)` helpers, FFI bindings,
  raw handles, callback trampolines, request progress helpers, and part-loop
  helpers.
- Documentation role: this README defines shape and semantic coverage.
  `Contracts/` owns the exact member list; rustdoc for the public crate must
  project it intentionally.

Applications, perf, and samples must not depend on private modules or raw FFI
bindings.

## Repository Layout

Use these paths consistently when changing the Rust binding.

- Public contract: `bindings/rust/src/zlink/Contracts/`.
- Runtime implementation: `bindings/rust/src/zlink/Runtime/`.
- Native bridge/artifacts: `bindings/rust/src/zlink/Runtime/Native/`,
  `bindings/rust/native/`, and `bindings/rust/include/`.
- Codec extensions: `bindings/rust/crates/`.
- Tests: `bindings/rust/tests/`.
- Samples: `bindings/rust/samples/`.
- Perf: `bindings/rust/perf/`.

Public re-exports in `lib.rs` must be intentional. A module can be used by
implementation code without becoming public.
`Contracts/` and `Runtime/` are fixed repository folders. `lib.rs` re-exports
and public rustdoc modules are the Rust crate projection of that contract.
Do not expose `Contracts` or `Runtime` as public crate modules. Use idiomatic
lowercase modules at the crate boundary.

```text
bindings/rust/
+-- src/
|   +-- lib.rs
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
+-- crates/
+-- tests/
+-- samples/
+-- perf/
+-- native/
+-- include/
```

## API Change Workflow

When mapping a new core capability:

1. Add the public type or method to the correct `Contracts/` category.
2. Update the safe public owner module and `lib.rs` re-export projection.
3. Add concrete public types or methods first; add traits only for real
   substitutable behavior.
4. Keep `unsafe`, raw handles, callback userdata, and part loops inside private
   modules.
5. Return `Result` with typed error information for fallible operations.
6. Add tests that use the public crate projection of `Contracts/`.
7. Update samples and perf only through public APIs.
8. Run formatting and clippy-style checks where available.

## Library Shape

The binding should feel like a safe Rust crate over a native runtime.

- Public resource types own native lifetime and release resources through
  `Drop`.
- Fallible operations return `Result<T, ZlinkError>` or a more specific typed
  result where that improves clarity.
- Concrete values such as message, routing id, received metadata, topic
  message, snapshots, options, enums, and errors stay concrete.
- Traits are used only when callers need substitutable behavior or generic
  bounds. Do not define a trait for every concrete handle by default.
- Builders are required for multipart send, publish, request, reply, SPOT, and
  actor operations so native state stays hidden.
- `unsafe` and raw FFI are confined to private modules.

## Contract / Runtime Placement Rules

- Public structs, enums, traits, errors, and builder contracts belong in
  `Contracts/`.
- Public free functions, associated helper functions, convenience methods, and
  builder helper methods belong in `Contracts/` when callers can use them
  directly.
- Runtime handle owners, request pumps, callback adapters, and part-loop
  helpers belong in `Runtime/`.
- FFI bindings, raw pointers, native struct mirrors, marshalling helpers, and
  platform loading code belong in `Runtime/Native/`.
- `lib.rs` and public rustdoc modules must project `Contracts/`, not expose
  `Runtime/` modules.
- If a runtime concrete type is re-exported for construction, its public
  behavior must still be described by `Contracts/`.

## Contract Folder Layout

`Contracts/` is the source ownership map for public crate items and re-exports.

- `Core/`: context, context options, routing id, version/capability helpers, and
  utility contracts.
- `Messaging/`: message, received metadata, topic messages, subscription events,
  stream packet data, and builder payload helpers.
- `Sockets/`: socket behavior, socket families, typed options, request/reply,
  and publish/subscribe surfaces.
- `Monitoring/`: monitor, monitor snapshot/event, poller, poll event, timer, and
  public poll helpers.
- `Service/`: registry, discovery, SPOT node, SPOT handle, topology models,
  actor refs, actor lifecycle, and operation builders.
- `Errors/`: typed error/result domains.
- `Enums/`: public enum domains shared across the binding.

## Canonical Interface Rules

- Data-plane `recv`, routed recv, `subscribe`, and subscription-event receive
  fill caller-provided `&mut Received`, `&mut TopicMessage`, or
  `&mut SubscriptionEvent` values and return `Result<bool, RecvError>`.
- Send, routed send, publish, request, reply, SPOT operations, and Actor
  location/session operations return typestate builders.
- Builder start methods take only the target identity, topic, channel, routing
  id, or request sequence. Payload, flags, timeout, callback, and async submit
  choices are builder states or steps.
- Multipart payload is accumulated by repeated `message(...)` calls.
  `messages(...)` convenience is allowed when it delegates to the same builder
  contract and is declared in `Contracts/`.
- Do not add operation-start method families such as `send_no_wait`,
  `publish_with_flags`, or `request_async`; keep one operation name and let
  the builder absorb the variation. Terminal builder methods may use idiomatic
  names such as `submit_async`.

## Crate Layout

The crate should expose clear public modules or re-exports.

- Core: context, options, version/capability helpers, and utilities.
- Messaging: message, routing id, received metadata, topic message,
  subscription event, and stream packet data.
- Sockets: pair, dealer, router, pub, sub, xpub, xsub, stream, typed options,
  callbacks, request/reply, publish/subscribe, and stream packet APIs.
- Monitoring: monitor, monitor snapshot/event, poller, poll event, and timer.
- Service: registry, discovery, SPOT node, SPOT handle, topology snapshots,
  actor refs, actor lifecycle, and operation builders.
- Error: typed error/result domains preserving core semantics.

The public crate may re-export common types at the crate root, but private FFI
modules must stay private.

## Required Capability Coverage

The public crate must cover stable user-facing core capabilities.

- Context lifecycle, options, shutdown, auto-HWM recalculation, version,
  capability, and strerror.
- Message ownership, multipart payloads, routing ids, received metadata, topic
  messages, subscription events, and stream packet callbacks.
- All socket families and their typed options.
- Monitor, poller, timer, and readiness semantics.
- Registry, discovery, SPOT node, SPOT handle, topology snapshots, actors, and
  stream actor binding.

Rust names and ownership models may differ from C, but behavior must match the
core capability meaning.

## Receive And Subscribe Shape

- Data-plane receive and subscribe APIs must use reusable caller-owned result
  storage.
- Nonblocking no-data must be distinct from hard receive failure.
- SPOT readable dispatch events are readiness notifications. Callers drain the
  matching receive API until no-data.
- Returned message data must have clear ownership and lifetime; borrowed data
  must not outlive the native owner.
- Service control/admission receive paths such as Actor join request receive may
  use `Option`, nullable-equivalent, or typed result-return forms when they are
  clearer than reusable data-plane storage. They must still distinguish no-data
  from hard receive failure.

## Error And Validation Policy

- Validate native fixed-size ids and strings before crossing the FFI boundary.
- Do not silently truncate routing ids, actor ids, endpoints, channel names, or
  topics.
- Preserve submit, request, recv, handler, close, bind, connect, and config
  error domains.
- Public errors should be inspectable through Rust types, not parsed strings.

## Performance Policy

- Hot paths must not use avoidable dynamic dispatch, avoidable allocation,
  avoidable byte copies, hidden sleeps, busy waits, broad locks, or thread
  joins.
- FFI bridge code should materialize public Rust values directly from the core
  part substrate.
- Avoid one thread or timer per request when progress can be shared by handle.
- Perf, samples, and tests use public crate APIs only.

## Implementation Checklist

- Public exports are intentional and documented.
- Raw FFI and unsafe state do not leak through public APIs.
- Resource ownership is enforced by Rust types and `Drop`.
- Traits are used only for real abstraction points.
- Public free functions and builder convenience methods are declared in
  `Contracts/`, not only in runtime helpers.
- Receive/subscription semantics match the shared binding policy.
- Service control/admission receive exceptions are documented where they differ
  from data-plane caller-provided storage.
- Perf meaning matches `bindings/c/perf`.
