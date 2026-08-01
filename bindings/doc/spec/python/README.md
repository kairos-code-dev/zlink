[Spec Index](https://zlink.systems/core/spec/) · [Bindings Policy](../README.md)

# Python Binding Implementation Blueprint

This document defines the expected Python library shape. It is not an
exhaustive list of every class or method. The concrete public contract source
is `bindings/python/src/zlink/contracts/`. The `zlink` package exported from
`bindings/python/src/zlink/__init__.py` is the public projection that users
import.

A Python implementation is aligned when the `zlink.contracts`, private runtime
packages, type hints, tests, samples, perf runners, and runtime behavior follow
this blueprint and map stable `core/include/zlink.h` capabilities into
Python-idiomatic APIs.

This README describes the completed Python binding shape after it is aligned to
the shared policy in `../README.md`, and it is also the guide for Python
refactoring work. During the refactor, use this document to decide where each
public contract, runtime implementation, native bridge helper, test, sample,
and perf import belongs. Once the Python binding is declared aligned, package
exports, type hints, generated API reference, tests, samples, perf, and runtime
behavior must match this document.

The Python refactor is a breaking cleanup. Do not keep compatibility shims,
deprecated wrappers, duplicate construction paths, or private-module re-export
aliases only to preserve the pre-refactor public surface.

This binding follows the shared bindings architecture map with Python naming:
public names are projected from `zlink`, public contract source lives under
lower-case `contracts`, and implementation details stay in underscore-prefixed
packages such as `_runtime` and `_native`.

Python should keep the physical package tree close to the .NET category map.
`contracts/core`, `contracts/messaging`, `contracts/sockets`,
`contracts/eventing`, `contracts/service`, and `contracts/errors` are the
public contract owners. `_runtime/` mirrors those categories and also keeps
implementation-only support packages such as `handles`, `buffers`, `options`,
and a separate `_native` boundary. Native-backed resource and operation
contracts use `typing.Protocol` as structural interfaces. Concrete runtime
classes live under `_runtime`, and callers create them through explicit
`create_*` factories or public contract methods.

## Public Contract Source

- Public contract source: `bindings/python/src/zlink/contracts/`.
- Package projection: names exported from `zlink`.
- Public resource contracts: native-backed resources, builders, and operation
  handles are `typing.Protocol` declarations. They describe the public surface
  but are not constructors.
- Public construction: package-root factories such as `create_context()`,
  `create_pair_socket(...)`, `create_message(...)`, `create_poller()`,
  and `create_spot_node(...)`, plus public owner methods such as
  `SpotNode.create_spot()`.
- Internal implementation: underscore-prefixed packages such as `_runtime` and
  `_native`, private extension modules, callback bridge code, request progress
  helpers, and raw part-loop helpers.
- Documentation role: this README defines shape and semantic coverage.
  `zlink/contracts/`, `zlink.__init__`, and type hints own the exact public
  member list.

Perf, samples, and tests must import from `zlink`, not from underscore modules.

## Repository Layout

Use these paths consistently when changing the Python binding.

- Public contract: `bindings/python/src/zlink/contracts/`.
- Runtime implementation: `bindings/python/src/zlink/_runtime/`.
- Native bridge/artifacts: `bindings/python/src/zlink/_native/` for private
  bridge code and `bindings/python/src/zlink/native/` for packaged native
  binaries.
- Codec packages: not provided. Python bindings keep only raw `Message` and
  byte payload APIs.
- Tests: `bindings/python/tests/`.
- Samples: `bindings/python/samples/` and `bindings/python/examples/`.
- Perf: `bindings/python/perf/`.

Underscore-prefixed modules are implementation detail. If a user needs a name,
re-export it from `zlink` intentionally and document the public behavior.
`__init__.py`, type hints, and generated API reference are the Python package
projection of the contract. Do not expose `zlink.Contracts` or `zlink.Runtime`
as public import paths. Do not create capitalized `src/zlink/Contracts` or
`src/zlink/Runtime`; Python package names stay lower-case. The following tree
is the aligned implementation structure. Public classes, functions, exceptions,
enums, type aliases, and builder contracts belong in `contracts/` and are
re-exported intentionally from `zlink`. Native extension calls, handle owners,
callback trampolines, marshalling, and request progress helpers belong under
`_runtime` or `_native`. Data-plane hot paths should not loop through core
functions part by part from Python through `ctypes` or CFFI; they should call a
private compiled extension module that batches the core C API work.

File granularity follows the common policy in `../README.md`: keep one file
per independent public concept or tight operation/model group. Very small
protocol, callback, enum, or pass-through helper modules should be merged into
the nearby contract file when that makes the public shape easier to read.

```text
bindings/python/
+-- src/
|   +-- zlink/
|   |   +-- __init__.py
|   |   +-- contracts/
|   |   |   +-- core/
|   |   |   |   +-- context.py
|   |   |   |   +-- zlink.py
|   |   |   |   +-- routing_id.py
|   |   |   +-- messaging/
|   |   |   |   +-- message.py
|   |   |   |   +-- received.py
|   |   |   |   +-- topic_message.py
|   |   |   |   +-- subscription_event.py
|   |   |   +-- sockets/
|   |   |   |   +-- socket.py
|   |   |   |   +-- message_socket_contracts.py
|   |   |   |   +-- routed_socket_contracts.py
|   |   |   |   +-- pubsub_socket_contracts.py
|   |   |   |   +-- stream_socket.py
|   |   |   |   +-- socket_options.py
|   |   |   +-- eventing/
|   |   |   |   +-- monitor.py
|   |   |   |   +-- poller.py
|   |   |   |   +-- timer.py
|   |   |   +-- service/
|   |   |   |   +-- spot/
|   |   |   |   |   +-- spot_node.py
|   |   |   |   |   +-- spot.py
|   |   |   |   |   +-- actor.py
|   |   |   |   |   +-- spot_operations.py
|   |   |   |   |   +-- spot_models.py
|   |   |   +-- errors/
|   |   |   |   +-- errors.py
|   |   |   |   +-- results.py
|   |   +-- _runtime/
|   |   |   +-- core/
|   |   |   |   +-- context.py
|   |   |   +-- handles/
|   |   |   |   +-- native_support.py
|   |   |   +-- messaging/
|   |   |   |   +-- message_materializer.py
|   |   |   +-- buffers/
|   |   |   |   +-- payload_buffers.py
|   |   |   +-- sockets/
|   |   |   |   +-- socket_base.py
|   |   |   |   +-- socket_base_impl.py
|   |   |   +-- eventing/
|   |   |   |   +-- poller.py
|   |   |   |   +-- timer.py
|   |   |   +-- service/
|   |   |   |   +-- spot/
|   |   |   |   |   +-- spot_node.py
|   |   |   |   |   +-- spot.py
|   |   |   |   |   +-- actor.py
|   |   |   +-- errors/
|   |   |   |   +-- native_errors.py
|   |   |   +-- options/
|   |   |   |   +-- option_mapping.py
|   |   +-- _native/
|   |   |   +-- _zlink_native.*
|   |   +-- native/
+-- tests/
+-- samples/
+-- examples/
+-- perf/
```

The public import surface is the `zlink` package projection. The source of that
projection is `zlink/contracts/`. Tests, samples, examples, and perf must import
from `zlink`. Binding-owned JSON, Protobuf, and MessagePack codec packages are
not part of the Python public surface. If a private underscore module becomes
necessary for user code, add a public contract and export it intentionally
instead of documenting the private module.

## API Change Workflow

When mapping a new core capability:

1. Add the public Protocol, concrete value class, function, enum, exception, or
   type alias to the
   correct public package category.
2. Add or wire an explicit `create_*` factory for any native-backed resource
   that callers can construct.
3. Update the `zlink` package export, type hints, and API reference projection.
4. Keep native extension/FFI calls and request progress helpers in private
   modules.
5. Add tests that import `zlink`, not private modules.
6. Update samples and perf only through public exports.
7. Check that private extension objects do not leak through return values or
   exceptions.

When refactoring existing code to this shape:

1. Move public behavior declarations to `zlink/contracts/<category>/`.
2. Move native-backed implementations to `zlink/_runtime/<category>/`.
3. Keep native extension loading and FFI calls under `zlink/_native/`.
4. Replace direct private runtime construction in public code with package-root
   factories or contract methods.
5. Remove compatibility exports that expose private modules as public API.
6. Remove deprecated wrappers, duplicate operation-start names, and old naming
   aliases instead of preserving them as shims.
7. Update tests, samples, examples, and perf to import from `zlink` only.
8. Regenerate or check type hints and API reference so private implementation
   modules do not appear in the public surface.

The refactor is complete only when Python-specific shortcuts below are removed.

- `zlink.contracts` must not re-export `_runtime` or `_native` modules.
- Contract files must not import runtime resource types to describe public
  service models.
- A public private-module aggregate must not remain the source of resource
  behavior. Split declarations into named contract files and runtime
  implementation files.
- `zlink.__init__` must export contract names and factories, not private
  implementation modules.
- Type hints and generated API reference must not mention `_runtime` or
  `_native` implementation paths as public types.

## Library Shape

The binding should feel like a Python package with a native backend.

- Native-backed public resources, builders, handlers, and reusable storage
  surfaces are declared as `typing.Protocol` contracts in `zlink.contracts`.
- Contract classes describe callable shape only. They do not hide factory logic
  in `__new__` and callers do not instantiate them directly.
- Concrete runtime classes own native resource lifetime, provide `close()`, and
  support context manager usage when practical.
- Values and snapshots that are plain Python data, such as routing id,
  topology entries, enum values, result domains, and exceptions, stay concrete.
  Native-backed message and receive storage are created through factories even
  when their runtime implementation is a concrete Python class.
- Type hints describe public call shapes, but private native state remains
  hidden.
- Native handles, raw FFI pointers, callback userdata, request pumps, and
  part-loop sequencing stay in private modules.

Do not expose private extension objects for convenience in perf or samples.

## Contract / Runtime Placement Rules

- Public classes, type aliases, exceptions, enums, and builder contracts belong
  in the matching `zlink/contracts/` category and are re-exported by `zlink`
  when users should import them directly.
- Public module functions, class/static helpers, convenience methods, and
  builder helper functions belong in public package modules when callers can
  use them directly.
- Python runtime implementations, handle owners, request pumps, callback
  adapters, and part-loop helpers belong in `_runtime`.
- Native extension bindings, FFI declarations, native struct mirrors,
  marshalling helpers, and platform loading code belong in `_native`.
- `zlink.__init__`, type hints, and generated API reference must project the
  public package categories, not expose private runtime modules.
- Runtime concrete classes are construction targets behind package-root
  factories; callers should not import private modules directly.
- Contract names must not implement hidden `__new__` factory dispatch. A
  capitalized contract name is a type surface, not a construction shortcut.
- Package-root factories may import `_runtime` only to wire runtime
  implementations. Their public annotations must use contract names, not
  private runtime classes.

## Contract File Layout

The contract source must use the same classification as the
[.NET binding blueprint](../dotnet/README.md), with Python naming. Keep the
same conceptual file grouping so a developer who knows another binding can find
the same public concept in Python quickly.

- `core/`: `context.py`, `zlink.py`, `routing_id.py`, and core option/value
  files.
- `messaging/`: `message.py`, `received.py`, `topic_message.py`,
  `subscription_event.py`, and common operation payload types.
- `sockets/`: `socket.py`, `message_socket_contracts.py`,
  `routed_socket_contracts.py`, `pubsub_socket_contracts.py`,
  `stream_socket.py`, socket option types, stream packet handler contracts, and
  socket flags.
- `eventing/`: monitor, monitor event/status, poller, poll events, timer, and
  event handler contracts.
- `service/`: `spot/` subpackage for SPOT node, Spot, Actor, topology models,
  and service operation builders.
- `errors/`: public exception classes, result domains, and error-code mapping.

Avoid a single aggregate `models.py` or private runtime-export barrel for
public resource behavior. Small DTO-like dataclasses and literal types may be
grouped with the contract that gives them meaning, but native-backed resources
and operation builders need named contract files.

## Runtime File Layout

Runtime source mirrors the runtime classification in the
[.NET binding blueprint](../dotnet/README.md) but contains only implementation.

- `core/`: context implementation and context option helpers.
- `handles/`: native handle ownership, close state, lifetime checks, and
  reference tracking.
- `messaging/`: message materialization, request execution, and multipart
  progress helpers.
- `buffers/`: native buffer conversion, copy/borrow policy, and any pooled or
  pinned storage helpers.
- `sockets/`: socket base classes, socket kernels, socket implementations for
  every socket family, callback adapters, and operation implementation classes.
- `eventing/`: poller/timer/monitor implementations and event
  materialization helpers.
- `service/`: SPOT node, Spot, Actor, topology, and
  service operation implementations.
- `options/`: option validation and native option id/value mapping.
- `errors/`: native error translation and validation helpers.
- `_native/`: extension loading, platform lookup, and native binding surface.

Runtime files may import contract types, but contract files must not import
runtime files. The package root may instantiate runtime implementations in
factories, but it must export contract names, not private implementation
modules.

## Native Bridge Implementation Rules

The target Python implementation is a thin public Python surface backed by a
private compiled native bridge. The `zlink` package and `contracts/` own the
public API, while the compiled extension module under `_native` owns
performance-sensitive calls into the core C API.

`ctypes` or CFFI ABI mode is allowed only for low-frequency paths such as
platform loading, diagnostics, or temporary fallback during migration. Paths
that repeat per message, such as send, routed send, publish, recv, subscribe,
router recv, request/reply, and SPOT data-plane operations, must go through the
compiled extension module. This reduces Python call count, dynamic marshalling,
and per-part object creation.

The compiled extension module is not public API. Module names, capsule types,
native owner types, and error helper functions must not appear in
`zlink.__init__`, type hints, API reference output, samples, or perf code.
Users continue to use only public `zlink` factories and public objects.

### Hot Path Bridge

The native bridge must perform at least the following work inside a single
Python extension call.

- Validate payloads supporting the Python buffer protocol and convert them into
  a `zlink_msg_t` part array.
- Call `zlink_send_part`, `zlink_send_part_rid`, and `zlink_publish_part` family
  functions in a native loop for every part.
- Call `zlink_recv_part`, `zlink_subscribe_part`, and `zlink_router_recv_part`
  family functions in a native loop and return one result containing the
  private receive owner and metadata.
- Close remaining native parts on failed send/recv and return enough
  errno/result information for the Python runtime to raise the public exception
  domain.
- Release the GIL while blocking send/recv/request work does not touch Python
  objects.

The Python runtime layer wraps this private result in public objects such as
`Received`, `TopicMessage`, `SubscriptionEvent`, or request results. Public
objects manage the private receive owner's lifetime. That owner may be
native-backed or bytes-backed, but raw pointers and capsules must not be
user-accessible.

### Buffer And Copy Policy

Send payloads accept values that support the Python buffer protocol, such as
`bytes`, `bytearray`, and `memoryview`. The native bridge checks contiguity and
readonly state, copying only when required. Calls where core takes message
ownership must make native part lifetime explicit. Calls that borrow Python
buffers must keep the Python owner alive until native submission is complete.

Receive payloads should be exposable as buffer views that remain valid while
the public received object is open. A native-backed owner may expose the core
buffer directly; a bytes-backed owner may expose private immutable bytes
storage. `to_bytes()` is either the explicit copy path or the bytes-backed
storage return path. After a public `Received` object is closed, accessing its
received parts must fail as before.

### Callback And Threading

Native callback trampolines belong at the private boundary between `_runtime`
and `_native`. They acquire the GIL only when they need to call a Python
handler, and handler execution follows the existing dispatcher rules. Callback
paths should let public received objects manage lifetime through a private
owner.

### Build And Packaging

Python wheels must include the compiled extension module. The extension module
must use the same runtime lookup rules as the packaged `libzlink` artifact so a
default wheel works without extra environment variables.

Source builds may lack the required C/C++ compiler or Python development
headers. In that case the build must fail clearly. It must not silently fall
back to a pure Python hot path and distort performance measurements. If a
fallback is needed, perf runners must print that the fallback is active and must
not treat those results as official performance numbers.

## Construction Entry Points

Public construction is provided by package-root factories and public owner
methods.

- `create_context()` creates the native-backed context implementation.
- `create_pair_socket(...)`, `create_dealer_socket(...)`,
  `create_router_socket(...)`, `create_pub_socket(...)`,
  `create_sub_socket(...)`, `create_xpub_socket(...)`,
  `create_xsub_socket(...)`, and `create_stream_socket(...)` create
  native-backed socket implementations.
- `create_spot_node(...)` creates the service-layer implementation.
- `Spot` handles are obtained through `SpotNode.create_spot()`,
  `entry_spot()`, `get_or_create_spot(...)`, or `spot_lookup(...)`; direct
  `Spot` construction is not public.
- Actor handles are created through `SpotNode.create_actor(...)`; direct Actor
  construction is not public.
- `create_poller()`, `create_poll_events(...)`, `create_timer()`, and
  `create_timer_from_spot(...)` create eventing resources.
- `create_message(...)`, `allocate_message(...)`,
  `create_received()`, `create_topic_message()`, and
  `create_subscription_event()` create reusable messaging storage.
- Version, capability, strerror, proxy, sleep, and multipart cleanup helpers
  are public package functions. Native calls behind those functions stay in
  private modules.

## Contract Category Map

The `zlink/contracts/` package is the source ownership map for names exported
from `zlink`.

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
- `errors/`: typed exception domains.
- Enum, flag, and result types live in the category that defines their meaning.
  Do not create an `enums` package just to group declarations by syntax.

## Canonical Interface Rules

- Data-plane `recv_into`, routed recv, `subscribe_into`, and
  subscription-event receive fill caller-provided `Received`, `TopicMessage`,
  or `SubscriptionEvent` objects and return `bool`.
- Send, routed send, publish, request, reply, SPOT operations, and Actor
  location/session operations return fluent builders.
- Builder start methods take only the target identity, topic, channel, routing
  id, or request sequence. Payload, flags, timeout, callback, and async submit
  choices are builder steps.
- SPOT channel-targeted operations use `send_to_channel(...)` and
  `request_to_channel(...)`. SPOT topic publish stays `publish(topic)`.
- Do not add single-payload shortcut methods with the same name as an operation
  start method. `send(message)`, `send(routing_id, message)`,
  `publish(topic, message)`, `send_to_channel(channel, message)`, and
  `send_to_spot(..., message)` are not public contract members; callers use
  `send(...).message(message).submit()`.
- Multipart payload is accumulated by repeated `message(...)` calls. A
  Python-style `messages(*parts)` convenience may delegate to the same builder.
  That convenience is public contract when exported and belongs in the public
  package category.
- Dealer sockets must not expose protocol envelope helpers such as
  `request_frame(...)` or `reply(request_token, parts)`. A dealer can start a
  request through `request()`, but it cannot reply to an arbitrary token
  because it has no API-level peer routing id.
- Message payload factories use `Message.from_(...)`; Python keeps the trailing
  underscore because `from` is a reserved word. `create_message_from`,
  `copy_from`, and `from_bytes` are not part of the public contract.
- Do not add operation-start method families such as `send_no_wait`,
  `publish_with_flags`, or `request_async`; keep one operation name and let
  the builder absorb the variation. Request completion is delivered through
  `submit(callback)`, and Python coroutine surfaces are owned by the framework.

## Public Package Shape

The `zlink` package should expose domain-level groups.

- Core: context, version/capability helpers, options, and utilities.
- Messaging: message, routing id, received metadata, topic message,
  subscription event, and stream packet data.
- Sockets: pair, dealer, router, pub, sub, xpub, xsub, stream, typed options,
  callbacks, request/reply, publish/subscribe, and stream packet APIs.
- Eventing: monitor, monitor snapshot/event, poller, poll event, and timer.
- Service: SPOT node, SPOT handle, topology snapshots,
  actor refs, actor lifecycle, and operation builders.
- Errors: typed exception classes preserving core result domains.

## Required Capability Coverage

The public package must cover these stable user-facing capabilities when the
binding is aligned to the shared .NET-standard policy.

- Context lifecycle, options, shutdown, auto-HWM recalculation, version,
  capability, and strerror.
- Message ownership, multipart payloads, routing ids, received metadata, topic
  messages, subscription events, and stream packet callbacks.
- All socket families and their typed options. `SubSocket.subscription_at(index)`
  and `XSubSocket.subscription_at(index)` return the subscription filter and
  pattern flag for the index, or `None` when the index is absent.
- Monitor, poller, timer, and readiness semantics.
- SPOT node, SPOT handle, topology snapshots, actors, and
  stream actor binding.

Python names may follow Python style, but behavior must match the core
capability meaning.

## Spot Get-Or-Create

Python exposes `SpotNode.get_or_create_spot(spot_rid)`. It maps directly to
`zlink_spot_node_spot_get_or_new(...)`; it must not be implemented by composing
`spot_lookup` and `create_spot`.

The method returns `(spot, created)`. The returned `Spot` is caller-owned and
must be closed normally. `created` is `True` only for the call that created the
logical spot.

## Receive And Subscribe Shape

- Data-plane receive and subscribe APIs must use caller-provided result objects
  for reusable storage.
- Nonblocking no-data returns `False` and is distinct from hard receive
  failure.
- Hard receive failures raise the documented zlink exception.
- SPOT readable dispatch events are readiness notifications. Callers drain the
  matching receive API until no-data.
- Service control/admission receive paths such as Actor join request receive may
  use `None`, optional, or typed result-return forms when they are clearer than
  reusable data-plane storage. They must still distinguish no-data from hard
  receive failure.

## Error And Validation Policy

- Validate native fixed-size ids and strings before calling extension or FFI
  code.
- Do not silently truncate routing ids, actor ids, endpoints, channel names, or
  topics.
- Preserve submit, request, recv, handler, close, bind, connect, and config
  error domains in typed exceptions.
- Public APIs must not require callers to inspect native errno directly.

## Performance Policy

- Hot paths must not use reflection-style attribute lookup, dynamic dispatch by
  string, avoidable allocation, avoidable `bytes` copies, hidden sleeps, busy
  waits, broad locks, or thread joins.
- Per-message hot paths must go through the private compiled extension module.
  Repeating `ctypes`/CFFI ABI calls from a per-part Python loop is not the
  official performance implementation.
- Native extension code should materialize public Python values directly from
  the core part substrate.
- Blocking native work should be separated from Python object access and should
  release the GIL while possible.
- Avoid one polling thread or timer per request when progress can be shared by
  handle.
- Perf, samples, and tests use public `zlink` exports only.

## Implementation Checklist

- `zlink.__all__` or the package export surface matches the intended public
  contract.
- Underscore modules do not leak through public signatures.
- Resource classes have explicit close semantics.
- Exported module functions and builder convenience methods are declared in
  public package modules, not only in runtime helpers.
- Receive/subscription semantics match the shared binding policy.
- Service control/admission receive exceptions are documented where they differ
  from data-plane caller-provided storage.
- Perf meaning matches `bindings/c/perf`.
- `zlink/contracts` has no import or export dependency on `zlink/_runtime` or
  `zlink/_native`.
- `zlink.__init__` imports private runtime modules only for factory wiring and
  does not export private implementation type names.
- Tests, samples, examples, and perf do not import underscore modules.
- Native-backed resources are created through package-root factories or
  contract methods and are typed as contract classes/protocols.
- Official perf paths use the compiled extension module, and fallback paths do
  not silently mix into performance results.
- No old aliases, duplicate operation-start names, or deprecated wrappers are
  kept only for compatibility.

Required verification after the Python refactor. Run these commands from
`bindings/python/`:

- Run `python -m pip install -e .` or the repository's standard editable build
  command when required by local tests.
- Run `./tests/run_tests.sh`.
- Run `./samples/run_samples.sh` when public examples or construction paths
  changed.
- Run `./perf/run_benchmarks.sh` and `./perf/run_benchmarks_multi.sh` as smoke
  gates when hot path, receive, send, request, poller, timer, or service
  behavior changed.
- Run the package type-check or lint gate if one exists in the Python binding.
- Search `src/zlink/contracts`, `tests`, `samples`, `examples`, and `perf` for
  imports from `zlink._runtime`, `zlink._native`, private extension modules, or
  generated private files. Check `src/zlink/__init__.py` separately to confirm
  private imports are factory wiring only and do not appear in public type
  hints.

## Actor And Spot Route Results

Python exposes Actor and Spot route lookup results through public result
objects.

- `ActorRoute` preserves the resolved Actor ref, Actor node RID, current Spot
  RID, and current Spot kind.
- `SpotRoute` preserves Spot RID, owner node RID, and Spot kind.
- `SpotKind` distinguishes Entry Spot from user Spot. Invalid kind is not a
  successful route result.
- SpotNode snapshot entries expose the same Spot kind/current Spot fields as the
  core snapshots.

Python exposes `SpotNode.send_to_actor(actor_ref)` and
`SpotNode.request_to_actor(actor_ref)` for resolved Actor refs. The send
operation consumes one or more message parts on successful submit and completes when the
Actor owner mailbox accepts the handoff. The request operation consumes
request parts on successful submit and delivers the Actor handler reply parts.
Python must not reintroduce the removed Discovery route table or resolver APIs
as compatibility helpers.
