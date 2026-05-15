[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Python Binding Implementation Blueprint

This document defines the expected Python library shape. It is not an
exhaustive list of every class or method. The concrete public contract is
`bindings/python/src/zlink/Contracts/`.

A Python implementation is aligned when `Contracts/`, `zlink` export
projections, type hints, tests, samples, perf runners, and runtime behavior
follow this blueprint and map stable `core/include/zlink.h` capabilities into
Python-idiomatic APIs.

## Public Contract Source

- Public contract: `bindings/python/src/zlink/Contracts/`.
- Package projection: names exported from `zlink`.
- Internal implementation: underscore-prefixed modules such as `_native`,
  `_ffi`, `_core`, private extension modules, callback bridge code, request
  progress helpers, and raw part-loop helpers.
- Documentation role: this README defines shape and semantic coverage.
  `Contracts/` owns the exact member list; public exports and generated API
  reference must project it intentionally.

Perf, samples, and tests must import from `zlink`, not from underscore modules.

## Repository Layout

Use these paths consistently when changing the Python binding.

- Public contract: `bindings/python/src/zlink/Contracts/`.
- Runtime implementation: `bindings/python/src/zlink/Runtime/`.
- Native bridge/artifacts: `bindings/python/src/zlink/Runtime/Native/`.
- Codec extensions: `bindings/python/codecs/`.
- Tests: `bindings/python/tests/`.
- Samples: `bindings/python/samples/` and `bindings/python/examples/`.
- Perf: `bindings/python/perf/`.

Underscore-prefixed modules are implementation detail. If a user needs a name,
re-export it from `zlink` intentionally and document the public behavior.
`Contracts/` and `Runtime/` are fixed repository folders. `__init__.py`, type
hints, and generated API reference are the Python package projection of that
contract.
Do not expose `zlink.Contracts` or `zlink.Runtime` as public import paths.

```text
bindings/python/
+-- src/
|   +-- zlink/
|   |   +-- __init__.py
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
+-- codecs/
+-- tests/
+-- samples/
+-- examples/
+-- perf/
```

## API Change Workflow

When mapping a new core capability:

1. Add the public class, function, enum, exception, or type alias to the
   correct `Contracts/` category.
2. Update the `zlink` package export, type hints, and API reference projection.
3. Keep native extension/FFI calls and request progress helpers in private
   modules.
4. Add tests that import `zlink`, not private modules.
5. Update samples and perf only through public exports.
6. Check that private extension objects do not leak through return values or
   exceptions.

## Library Shape

The binding should feel like a Python package with a native backend.

- Public classes own native resource lifetime and provide `close()`.
- Resource classes should support context manager usage when practical.
- Type hints describe public call shapes, but private native state remains
  hidden.
- `Protocol` may be used for static typing when it removes real caller
  complexity. It must not replace a clear runtime API.
- Values such as message, routing id, received metadata, topic message,
  snapshots, options, enums, and exceptions stay concrete Python types.
- Native handles, raw FFI pointers, callback userdata, request pumps, and
  part-loop sequencing stay in private modules.

Do not expose private extension objects for convenience in perf or samples.

## Contract / Runtime Placement Rules

- Public classes, type aliases, exceptions, enums, and builder contracts belong
  in `Contracts/`.
- Public module functions, class/static helpers, convenience methods, and builder
  helper functions belong in `Contracts/` when callers can use them directly.
- Python runtime implementations, handle owners, request pumps, callback
  adapters, and part-loop helpers belong in `Runtime/`.
- Native extension bindings, FFI declarations, native struct mirrors,
  marshalling helpers, and platform loading code belong in `Runtime/Native/`.
- `zlink.__init__`, type hints, and generated API reference must project
  `Contracts/`, not expose `Runtime/` modules.
- If a runtime concrete class is exported for construction, its public behavior
  must still be described by `Contracts/`.

## Contract Folder Layout

`Contracts/` is the source ownership map for names exported from `zlink`.

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
- `Errors/`: typed exception domains.
- `Enums/`: public enum domains shared across the binding.

## Canonical Interface Rules

- Data-plane `recv_into`, routed recv, `subscribe_into`, and
  subscription-event receive fill caller-provided `Received`, `TopicMessage`,
  or `SubscriptionEvent` objects and return `bool`.
- Send, routed send, publish, request, reply, SPOT operations, and Actor
  location/session operations return fluent builders.
- Builder start methods take only the target identity, topic, channel, routing
  id, or request sequence. Payload, flags, timeout, callback, and async submit
  choices are builder steps.
- Multipart payload is accumulated by repeated `message(...)` calls. A
  Python-style `messages(*parts)` convenience may delegate to the same builder.
  That convenience is public contract when exported and belongs in `Contracts/`.
- Do not add operation-start method families such as `send_no_wait`,
  `publish_with_flags`, or `request_async`; keep one operation name and let
  the builder absorb the variation. Terminal builder methods may use idiomatic
  names such as `submit_async`.

## Public Package Shape

The `zlink` package should expose domain-level groups.

- Core: context, version/capability helpers, options, and utilities.
- Messaging: message, routing id, received metadata, topic message,
  subscription event, and stream packet data.
- Sockets: pair, dealer, router, pub, sub, xpub, xsub, stream, typed options,
  callbacks, request/reply, publish/subscribe, and stream packet APIs.
- Monitoring: monitor, monitor snapshot/event, poller, poll event, and timer.
- Service: registry, discovery, SPOT node, SPOT handle, topology snapshots,
  actor refs, actor lifecycle, and operation builders.
- Errors: typed exception classes preserving core result domains.

## Required Capability Coverage

The public package must cover stable user-facing core capabilities.

- Context lifecycle, options, shutdown, auto-HWM recalculation, version,
  capability, and strerror.
- Message ownership, multipart payloads, routing ids, received metadata, topic
  messages, subscription events, and stream packet callbacks.
- All socket families and their typed options.
- Monitor, poller, timer, and readiness semantics.
- Registry, discovery, SPOT node, SPOT handle, topology snapshots, actors, and
  stream actor binding.

Python names may follow Python style, but behavior must match the core
capability meaning.

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
- Native extension or FFI code should materialize public Python values
  directly from the core part substrate.
- Avoid one polling thread or timer per request when progress can be shared by
  handle.
- Perf, samples, and tests use public `zlink` exports only.

## Implementation Checklist

- `zlink.__all__` or the package export surface matches the intended public
  contract.
- Underscore modules do not leak through public signatures.
- Resource classes have explicit close semantics.
- Exported module functions and builder convenience methods are declared in
  `Contracts/`, not only in runtime helpers.
- Receive/subscription semantics match the shared binding policy.
- Service control/admission receive exceptions are documented where they differ
  from data-plane caller-provided storage.
- Perf meaning matches `bindings/c/perf`.
