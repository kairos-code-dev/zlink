[Spec Index](https://kairos-code-dev.github.io/zlink/en/spec/) · [Bindings Policy](../README.en.md)

# Rust Binding Implementation Blueprint

This document defines the expected Rust crate shape. It is not an exhaustive
list of every public item. The concrete public contract source is
`bindings/rust/src/contracts/`. `bindings/rust/src/lib.rs` is the crate
projection that re-exports the intended public API.

A Rust implementation is aligned when the `contracts` source tree, private
runtime tree, public export projection, rustdoc, tests, samples, perf runners,
and runtime behavior follow this blueprint and map stable `core/include/zlink.h`
capabilities into Rust-idiomatic APIs.

This README describes the completed Rust binding shape after it is aligned to
the shared policy in `../README.md`, and it is also the guide for Rust
refactoring work. During the refactor, use this document to decide where each
public contract, runtime implementation, native bridge helper, test, sample,
and perf import belongs. Once the Rust binding is declared aligned, source
layout, public re-exports, rustdoc, tests, samples, perf, and runtime behavior
must match this document.

The Rust refactor is a breaking cleanup. Do not keep compatibility shims,
deprecated wrappers, duplicate construction paths, or old re-export aliases
only to preserve the pre-refactor public surface.

This binding follows the shared bindings architecture map with Rust naming:
`contracts` and private `runtime` modules organize source ownership, while
`lib.rs` decides which module paths become public crate API.

Rust should keep the physical source tree close to the .NET category map.
`contracts/core`, `contracts/messaging`, `contracts/sockets`,
`contracts/eventing`, `contracts/service`, and `contracts/errors` are the
public contract owners. `runtime/` mirrors those categories and also keeps
implementation-only support folders such as `handles`, `buffers`, `options`,
and `native`. This is a directory and responsibility alignment, not a request
to turn every native-backed resource into a trait.

## Public Contract Source

- Public contract source: `bindings/rust/src/contracts/`.
- Crate projection: public re-exports from `lib.rs` and rustdoc for public
  modules.
- Runtime implementation: private modules under `bindings/rust/src/runtime/`.
- Native bridge: private modules under `bindings/rust/src/runtime/native/`,
  raw handles, callback trampolines, request progress helpers, and part-loop
  helpers.
- Concrete crate-private resource storage is kept in
  `bindings/rust/src/internal.rs`. Contract files may refer to these storage
  types, but they must not import runtime resource types. FFI declarations and
  native calls remain under `runtime/`.
- Documentation role: this README defines shape and semantic coverage.
  Public crate exports own the exact member list. Each public item must still
  map to one of the shared contract categories.

Applications, perf, and samples must not depend on private modules or raw FFI
bindings.

## Repository Layout

Use these paths consistently when changing the Rust binding.

- Public contract: `bindings/rust/src/contracts/`.
- Crate projection: `bindings/rust/src/lib.rs`.
- Runtime implementation: private modules under `bindings/rust/src/runtime/`.
- Crate-private concrete storage: `bindings/rust/src/internal.rs`.
- Native bridge/artifacts: private modules under
  `bindings/rust/src/runtime/native/`, `bindings/rust/native/`, and
  `bindings/rust/include/`.
- Codec crates: not provided. Rust bindings keep only raw `Message` and byte
  payload APIs.
- Tests: `bindings/rust/tests/`.
- Samples: `bindings/rust/samples/`.
- Perf: `bindings/rust/perf/`.

Public re-exports in `lib.rs` must be intentional. Rust module paths are part of
the public API when they are exported. The `contracts` and `runtime` source
trees are implementation organization unless `lib.rs` explicitly exposes a
module. Do not expose `zlink::runtime` or raw native bridge modules.

The following tree is the aligned implementation structure. Public structs,
enums, traits, errors, free functions, and builder contracts belong in
`contracts/` and are re-exported intentionally from `lib.rs`. FFI bindings,
native struct mirrors, callback trampolines, request progress helpers,
marshalling, and unsafe part loops stay private under `runtime/`. The
crate-private storage module contains only the concrete ownership state needed
by public wrappers; it does not declare or call the FFI surface.

File granularity follows the common policy in `../README.md`: keep one file
per independent public concept or tight operation/model group. Very small
marker traits, callback aliases, enum-only modules, or pass-through helper
modules should be merged into the nearby contract file when that makes the
public shape easier to read.

```text
bindings/rust/
+-- src/
|   +-- lib.rs
|   +-- internal.rs
|   +-- contracts/
|   |   +-- core/
|   |   |   +-- context.rs
|   |   |   +-- routing_id.rs
|   |   +-- messaging/
|   |   |   +-- message.rs
|   |   |   +-- received.rs
|   |   |   +-- topic_message.rs
|   |   |   +-- subscription_event.rs
|   |   |   +-- operation_contracts.rs
|   |   +-- sockets/
|   |   |   +-- socket.rs
|   |   |   +-- message_socket_contracts.rs
|   |   |   +-- routed_socket_contracts.rs
|   |   |   +-- pubsub_socket_contracts.rs
|   |   |   +-- stream_socket.rs
|   |   |   +-- socket_options.rs
|   |   +-- eventing/
|   |   |   +-- monitor.rs
|   |   |   +-- poller.rs
|   |   +-- service/
|   |   |   +-- spot/
|   |   |   |   +-- spot_node.rs
|   |   |   |   +-- spot.rs
|   |   |   |   +-- actor.rs
|   |   |   |   +-- spot_operations.rs
|   |   |   |   +-- spot_models.rs
|   |   +-- errors/
|   |   |   +-- errors.rs
|   |   |   +-- results.rs
|   +-- runtime/
|   |   +-- handles/
|   |   |   +-- ctx.rs
|   |   +-- messaging/
|   |   |   +-- message.rs
|   |   |   +-- domain.rs
|   |   |   +-- request_progress.rs
|   |   +-- sockets/
|   |   |   +-- socket_base.rs
|   |   |   +-- pair_socket.rs
|   |   |   +-- dealer_socket.rs
|   |   |   +-- router_socket.rs
|   |   |   +-- pub_socket.rs
|   |   |   +-- sub_socket.rs
|   |   |   +-- xpub_socket.rs
|   |   |   +-- xsub_socket.rs
|   |   |   +-- stream_socket.rs
|   |   +-- eventing/
|   |   |   +-- poller.rs
|   |   |   +-- timer.rs
|   |   +-- service/
|   |   |   +-- spot/
|   |   |   |   +-- spot_node.rs
|   |   |   |   +-- spot.rs
|   |   |   |   +-- actor.rs
|   |   +-- errors/
|   |   |   +-- native_errors.rs
|   |   +-- options/
|   |   |   +-- options.rs
|   |   +-- native/
|   |   |   +-- native.rs
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

When refactoring existing code to this shape:

1. Move public behavior declarations to `src/contracts/<category>/`.
2. Move native-backed implementations to `src/runtime/<category>/`.
3. Keep unsafe FFI, native loading, raw handles, and callback trampolines under
   `src/runtime/native/`.
4. Replace direct runtime construction in public code with crate-root
   constructors or contract methods.
5. Remove compatibility re-exports that expose runtime modules as public API.
6. Remove deprecated wrappers, duplicate operation-start names, and old naming
   aliases instead of preserving them as shims.
7. Update tests, samples, and perf to use public crate exports only.
8. Regenerate/check rustdoc so private implementation modules do not appear as
   public API.

The refactor is complete only when Rust-specific shortcuts below are removed.

- `contracts` modules must not re-export `runtime` or `runtime::native`.
- Contract files must not import runtime resource types to describe public
  service models.
- Public re-export barrels must not remain the source of resource behavior.
  Split declarations into named contract modules and runtime implementation
  modules.
- `lib.rs` must export contract names and constructors, not runtime modules.
- Public rustdoc must not expose runtime implementation module paths as public
  types.

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

### Safe FFI RAII Wrapper Placement

Native-backed Rust resources use the standard safe FFI RAII wrapper pattern:
public `struct` handles own native lifetime, public inherent `impl` methods
expose safe Rust operations, and `Drop` releases native resources.

The public inherent `impl` surface belongs in the matching `contracts/` owner
file, even when the implementation delegates immediately to runtime helpers.
Runtime modules hide C API calls, `unsafe` blocks, raw handles, downcasts,
errno mapping, and native struct conversion behind `pub(crate)` helper
functions. Runtime modules must not be the only place where public methods on
a public resource can be discovered.

Use traits only when callers need substitutable behavior or generic bounds.
Do not create a trait just to make a native-backed handle look like an
interface. For a single concrete C-handle wrapper, prefer `pub struct` plus
public inherent methods in `contracts/` and private runtime helpers under
`runtime/`.

## Contract / Runtime Placement Rules

- Public structs, enums, traits, errors, and builder contracts belong in the
  matching `contracts/` category and are re-exported by `lib.rs` when public.
- Public free functions, associated helper functions, convenience methods, and
  builder helper methods belong in public modules when callers can use them
  directly.
- Public inherent `impl` blocks for native-backed public resources belong in
  the contract owner file. Their bodies may be thin delegations into
  `pub(crate)` runtime helpers.
- Runtime handle owners, request pumps, callback adapters, and part-loop
  helpers stay private or `pub(crate)`.
- FFI bindings, raw pointers, native struct mirrors, marshalling helpers, and
  platform loading code stay in private FFI/runtime owners.
- `lib.rs` and public rustdoc modules must project the contract categories, not
  expose runtime modules.
- Runtime concrete types are construction targets behind crate-root
  constructors or contract methods. `lib.rs` may import runtime modules only to
  wire those constructors, but public signatures must use contract names.

## Contract File Layout

The contract source must use the same classification as the
[.NET binding blueprint](../dotnet/README.en.md), with Rust naming. Keep the same
conceptual file grouping so a developer who knows another binding can find the
same public concept in Rust quickly.

- `core/`: `context.rs`, `routing_id.rs`, and core option/value files.
- `messaging/`: `message.rs`, `received.rs`, `topic_message.rs`,
  `subscription_event.rs`, `operation_contracts.rs`, and common operation
  payload types.
- `sockets/`: `socket.rs`, `message_socket_contracts.rs`,
  `routed_socket_contracts.rs`, `pubsub_socket_contracts.rs`,
  `stream_socket.rs`, socket option types, stream packet handler contracts, and
  socket flags.
- `eventing/`: monitor, monitor event/status, poller, poll events, timer, and
  event handler contracts.
- `service/`: `spot/` submodule for SPOT node, Spot, Actor, topology models,
  and service operation builders.
- `errors/`: public error types, result domains, and error-code mapping.

Avoid a single aggregate `models.rs` or runtime-export barrel for public
resource behavior. Small DTO-like structs and enums may be grouped with the
contract that gives them meaning, but native-backed resources and operation
builders need named contract files.

## Runtime File Layout

Runtime source mirrors the runtime classification in the
[.NET binding blueprint](../dotnet/README.en.md) but contains only implementation.

- `core/`: context implementation and context option helpers.
- `handles/`: native handle ownership, close state, lifetime checks, and
  reference tracking.
- `messaging/`: message materialization, request progress, request execution,
  and multipart progress helpers.
- `buffers/`: native buffer conversion, copy/borrow policy, and any pooled or
  pinned storage helpers.
- `sockets/`: socket base types, socket kernels, socket implementations for
  every socket family, callback adapters, and operation implementation types.
- `eventing/`: poller/timer/monitor implementations and event
  materialization helpers.
- `service/`: SPOT node, Spot, Actor, topology, and
  service operation implementations.
- `options/`: option validation and native option id/value mapping.
- `errors/`: native error translation and validation helpers.
- `native/`: FFI bindings, native loading, raw handles, and unsafe boundary
  code.

Runtime modules may import contract types, but contract modules must not import
runtime modules. The crate root may instantiate runtime implementations in
constructors, but it must export contract names, not runtime implementation
modules.

## Construction Entry Points

Public construction is provided by crate-root constructors and public contract
methods.

- `Context::new(...)` creates the native-backed context implementation.
- `Context::create_pair_socket()`, `create_dealer_socket()`,
  `create_router_socket()`, `create_pub_socket()`, `create_sub_socket()`,
  `create_xpub_socket()`, `create_xsub_socket()`, and
  `create_stream_socket()` create native-backed socket implementations.
- `Context::create_spot_node(...)` creates the service-layer implementation.
- `Spot` handles are obtained through `SpotNode::create_spot(...)`,
  `entry_spot()`, `get_or_create_spot(...)`, or `spot_lookup(...)`; direct
  `Spot` construction is not public.
- Actor handles are created through `SpotNode::create_actor(...)`; direct Actor
  construction is not public.
- `Poller::new(...)`, `Timer::new(...)`, and timer-on-SPOT helpers create
  eventing resources.
- `AtomicCounter::new()`, `Stopwatch::start()`, and `Thread::start(...)`
  create utility resources owned by the caller.
- Version, capability, strerror, proxy, sleep, and multipart cleanup helpers
  are public crate functions. FFI calls behind those functions stay private.

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
- `service/`: SPOT node, SPOT handle, topology models,
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
- Routing id construction uses standard `From` implementations.
  Public helpers named `from_bytes`, `from_string`, `from_u32`, or
  `from_uuid_bytes` are not part of the public contract; hex decoding may keep
  `from_hex` / `try_from_hex`.
- Do not add operation-start method families such as `send_no_wait`,
  `publish_with_flags`, or `request_async`; keep one operation name and let
  the builder absorb the variation. Async surfaces are expressed through a
  builder terminator or `Future`-returning surface, not by widening operation
  start names.

## Crate Layout

The crate should expose clear public modules or re-exports.

- Core: context, options, version/capability helpers, and utilities.
- Messaging: message, routing id, received metadata, topic message,
  subscription event, and stream packet data.
- Sockets: pair, dealer, router, pub, sub, xpub, xsub, stream, typed options,
  callbacks, request/reply, publish/subscribe, and stream packet APIs.
- Eventing: monitor, monitor snapshot/event, poller, poll event, and timer.
- Service: SPOT node, SPOT handle, topology snapshots,
  actor refs, actor lifecycle, and operation builders.
- Error: typed error/result domains preserving core semantics.

The public crate may re-export common types at the crate root, but private FFI
modules must stay private.

## Required Capability Coverage

The public crate must cover these stable user-facing capabilities when the
binding is aligned to the shared .NET-standard policy.

- Context lifecycle, options, shutdown, auto-HWM recalculation, version,
  capability, and strerror.
- Message ownership, multipart payloads, routing ids, received metadata, topic
  messages, subscription events, and stream packet callbacks.
- All socket families and their typed options. `SubSocket::subscription_at(index)`
  and `XSubSocket::subscription_at(index)` return the subscription filter and
  pattern flag for the index, or `None` when the index is absent.
- Monitor, poller, timer, and readiness semantics.
- SPOT node, SPOT handle, topology snapshots, actors, and
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
- `src/contracts` has no import or export dependency on `src/runtime`.
- `lib.rs` imports runtime modules only for constructor wiring and does not
  export runtime modules or runtime implementation type names.
- Tests, samples, and perf do not use private runtime imports.
- Native-backed resources are created through crate-root constructors or
  contract methods and are typed as public contract types.
- No old aliases, duplicate operation-start names, or deprecated wrappers are
  kept only for compatibility.

Required verification after the Rust refactor. Run these commands from
`bindings/rust/`:

- Run `cargo fmt --all --check`.
- Run `cargo test --workspace --all-targets`.
- Run `cargo clippy --workspace --all-targets -- -D warnings` when clippy is
  available.
- Run `./tests/run_tests.sh`.
- Run `./samples/run_samples.sh` when public examples or construction paths
  changed.
- Run `./perf/run_benchmarks.sh` and `./perf/run_benchmarks_multi.sh` as smoke
  gates when hot path, receive, send, request, poller, timer, or service
  behavior changed.
- Inspect rustdoc/public re-exports and confirm crate exports expose contract
  types, not runtime implementation modules.
- Search `src/contracts`, `tests`, `samples`, and `perf` for imports from
  `crate::runtime`, `runtime::native`, raw FFI modules, or generated private
  files. Check `src/lib.rs` separately to confirm runtime imports are
  constructor wiring only and do not appear in public signatures.

## Actor And Spot Route Results

Rust exposes Actor and Spot route lookup results through public value types.

- `ActorRoute` preserves the resolved Actor ref, Actor node RID, current Spot
  RID, and current Spot kind.
- `SpotRoute` preserves Spot RID, owner node RID, and Spot kind.
- `SpotKind` distinguishes Entry Spot from user Spot. Invalid kind is not a
  successful route result.
- SpotNode snapshot entries expose the same Spot kind/current Spot fields as the
  core snapshots.

Rust exposes `SpotNode::send_to_actor(&ActorRef)` and
`SpotNode::request_to_actor(&ActorRef)` for resolved Actor refs. The send
operation consumes one or more message parts on successful submit and completes when the
Actor owner mailbox accepts the handoff. The request operation consumes
request parts on successful submit and delivers the Actor handler reply parts.
Rust must not reintroduce the removed Discovery route table or resolver APIs
as compatibility helpers.
