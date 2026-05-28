[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Rust Binding Implementation Blueprint

This document defines the expected Rust crate shape. It is not an exhaustive
list of every public item. The concrete public contract source is
`bindings/rust/src/contracts/`. `bindings/rust/src/lib.rs` is the crate
projection that re-exports the intended public API.

A Rust implementation is aligned when the `contracts` source tree, private
runtime tree, public export projection, rustdoc, tests, samples, perf runners,
and runtime behavior follow this blueprint and map stable `core/include/zlink.h`
capabilities into Rust-idiomatic APIs.

This README is the target blueprint for aligning the Rust binding to the shared
policy in `../README.md`. Existing modules, re-exports, or compatibility
surfaces may lag this target until the Rust alignment work lands; treat those
differences as cleanup targets. When the binding is aligned to this README, the
target contract becomes the acceptance standard for that work.

This binding follows the shared bindings architecture map with Rust naming:
`contracts` and private `runtime` modules organize source ownership, while
`lib.rs` decides which module paths become public crate API.

## Public Contract Source

- Public contract source: `bindings/rust/src/contracts/`.
- Crate projection: public re-exports from `lib.rs` and rustdoc for public
  modules.
- Runtime implementation: private modules under `bindings/rust/src/runtime/`.
- Native bridge: private modules under `bindings/rust/src/runtime/native/`,
  raw handles, callback trampolines, request progress helpers, and part-loop
  helpers.
- Documentation role: this README defines shape and semantic coverage.
  public crate exports own the exact member list. Each public item must still
  map to one of the shared contract categories.

Applications, perf, and samples must not depend on private modules or raw FFI
bindings.

## Repository Layout

Use these target paths consistently when changing the Rust binding.

- Public contract: `bindings/rust/src/contracts/`.
- Crate projection: `bindings/rust/src/lib.rs`.
- Runtime implementation: private modules under `bindings/rust/src/runtime/`.
- Native bridge/artifacts: private modules under
  `bindings/rust/src/runtime/native/`, `bindings/rust/native/`, and
  `bindings/rust/include/`.
- Codec extensions: `bindings/rust/crates/`.
- Tests: `bindings/rust/tests/`.
- Samples: `bindings/rust/samples/`.
- Perf: `bindings/rust/perf/`.

Public re-exports in `lib.rs` must be intentional. Rust module paths are part of
the public API when they are exported. The `contracts` and `runtime` source
trees are implementation organization unless `lib.rs` explicitly exposes a
module. Do not expose `zlink::runtime` or raw native bridge modules.

The following tree is the target implementation structure. Public structs,
enums, traits, errors, free functions, and builder contracts belong in
`contracts/` and are re-exported intentionally from `lib.rs`. FFI bindings, raw
pointers, native struct mirrors, handle owners, callback trampolines, request
progress helpers, marshalling, and unsafe part loops stay private under
`runtime/`.

File granularity follows the common policy in `../README.md`: keep one file
per independent public concept or tight operation/model group. Very small
marker traits, callback aliases, enum-only modules, or pass-through helper
modules should be merged into the nearby contract file when that makes the
public shape easier to read.

```text
bindings/rust/
+-- src/
|   +-- lib.rs
|   +-- contracts/
|   |   +-- core/
|   |   +-- messaging/
|   |   +-- sockets/
|   |   +-- eventing/
|   |   +-- service/
|   |   +-- errors/
|   +-- runtime/
|   |   +-- core/
|   |   +-- messaging/
|   |   +-- sockets/
|   |   +-- eventing/
|   |   +-- service/
|   |   +-- errors/
|   |   +-- native/
+-- crates/
+-- tests/
+-- samples/
+-- perf/
+-- native/
+-- include/
```

The public consumer projection is `lib.rs`. Tests, samples, and perf must use
the public crate projection. If an item is re-exported publicly, reviewers must
be able to point to its shared contract category. If a module exists only to
make native calls or preserve unsafe invariants, it must remain private under
`runtime/`.

## API Change Workflow

When mapping a new core capability:

1. Choose the shared contract category that owns the public behavior.
2. Add the public type, method, or function to the safe owner module and update
   the `lib.rs` re-export projection when it should be visible at the crate
   root.
3. Add concrete public types or methods first; add traits only for real
   substitutable behavior.
4. Keep `unsafe`, raw handles, callback userdata, and part loops inside private
   modules.
5. Return `Result` with typed error information for fallible operations.
6. Add tests that use the public crate projection.
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

- Public structs, enums, traits, errors, and builder contracts belong in the
  matching `contracts/` category and are re-exported by `lib.rs` when public.
- Public free functions, associated helper functions, convenience methods, and
  builder helper methods belong in public modules when callers can use them
  directly.
- Runtime handle owners, request pumps, callback adapters, and part-loop
  helpers stay private or `pub(crate)`.
- FFI bindings, raw pointers, native struct mirrors, marshalling helpers, and
  platform loading code stay in private FFI/runtime owners.
- `lib.rs` and public rustdoc modules must project the contract categories, not
  expose runtime modules.
- If a runtime concrete type is re-exported for construction, its public
  behavior must still be described by the shared contract category.

## Contract Category Map

These categories map to lower-case modules under `bindings/rust/src/contracts/`
and are the review ownership map for public crate items and re-exports.

- `core/`: context, context options, routing id, version/capability helpers, and
  utility contracts.
- `messaging/`: message, received metadata, topic messages, subscription events,
  stream packet data, and builder payload helpers.
- `sockets/`: socket behavior, socket families, typed options, request/reply,
  and publish/subscribe surfaces.
- `eventing/`: monitor, monitor snapshot/event, poller, poll event, timer, and
  public poll helpers.
- `service/`: registry, discovery, SPOT node, SPOT handle, topology models,
  actor refs, actor lifecycle, and operation builders.
- `errors/`: typed error/result domains.
- Enum, flag, and result types live in the category that defines their meaning.
  Do not create an `enums` module just to group declarations by syntax.

## Canonical Interface Rules

- Data-plane `recv`, routed recv, `subscribe`, and subscription-event receive
  fill caller-provided `&mut Received`, `&mut TopicMessage`, or
  `&mut SubscriptionEvent` values and return `Result<bool, RecvError>`.
- Send, routed send, publish, request, reply, SPOT operations, and Actor
  location/session operations return typestate builders.
- Builder start methods take only the target identity, topic, channel, routing
  id, or request sequence. Payload, flags, timeout, callback, and async submit
  choices are builder states or steps.
- SPOT channel-targeted operations use `send_to_channel(...)` and
  `request_to_channel(...)`. SPOT topic publish stays `publish(topic)`.
- Do not add single-payload shortcut methods with the same name as an operation
  start method. `send(message)`, `send(routing_id, message)`,
  `publish(topic, message)`, `send_to_channel(channel, message)`, and
  `send_to_spot(..., message)` are not public contract members; callers use
  `send(...).message(message).submit()`.
- Multipart payload is accumulated by repeated `message(...)` calls.
  `messages(...)` convenience is allowed when it delegates to the same builder
  contract and is declared in the public crate surface.
- Dealer sockets must not expose protocol envelope helpers such as
  `request_frame(...)` or `reply(request_token, parts)`. A dealer can start a
  request through `request()`, but it cannot reply to an arbitrary token
  because it has no API-level peer routing id.
- Message payload factories use the fallible from-source contract:
  `Message::try_from(...)` and `TryFrom` implementations. Copy-specific names
  such as `copy_from` are not part of the public contract.
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
- Eventing: monitor, monitor snapshot/event, poller, poll event, and timer.
- Service: registry, discovery, SPOT node, SPOT handle, topology snapshots,
  actor refs, actor lifecycle, and operation builders.
- Error: typed error/result domains preserving core semantics.

The public crate may re-export common types at the crate root, but private FFI
modules must stay private.

## Target Capability Coverage

The public crate must cover these stable user-facing capabilities when the
binding is aligned to the shared .NET-standard target policy.

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

## Spot Get-Or-Create

Rust exposes `SpotNode::get_or_create_spot(&RoutingId) -> Result<(Spot, bool),
ConfigError>`. It maps directly to `zlink_spot_node_spot_get_or_new(...)`; it
must not be implemented by composing `spot_lookup` and `create_spot`.

The returned `Spot` is owned by the caller and follows normal `Drop` lifetime
rules. The boolean is `true` only for the call that created the logical spot.

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
- Public free functions and builder convenience methods are declared in public
  crate modules, not only in runtime helpers.
- Receive/subscription semantics match the shared binding policy.
- Service control/admission receive exceptions are documented where they differ
  from data-plane caller-provided storage.
- Perf meaning matches `bindings/c/perf`.

## Actor And Spot Route Results

Rust exposes Actor and Spot route lookup results through public value types.

- `ActorRoute` preserves the resolved Actor ref, Actor node RID, current Spot
  RID, and current Spot kind.
- `SpotRoute` preserves Spot RID, owner node RID, and Spot kind.
- `SpotKind` distinguishes Entry Spot from user Spot. Invalid kind is not a
  successful route result.
- SpotNode snapshot entries expose the same Spot kind/current Spot fields as the
  core snapshots.

Rust must not add ROUTER-to-Actor or Actor-to-ROUTER direct messaging methods.
Callers compose `resolve_actor()` or `resolve_spot()` with the existing Spot
routed APIs.

## SpotNode Router Channel Peers

Rust exposes router channel peer wiring as public `SpotNode` methods:
`connect_router_channel_peer(channel_name, endpoint)`,
`disconnect_router_channel_peer(channel_name, endpoint)`,
`disconnect_router_channel_peer_rid(channel_name, peer_rid)`, and
`attach_spot_route_channel_discovery(channel_name, discovery)`. These methods
call the matching core C APIs and use the established Rust error mapping.
