[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# C++ Binding Implementation Blueprint

This document defines the expected C++ library shape. It is not an exhaustive
list of every method. The concrete public contract is
`bindings/cpp/src/zlink/Contracts/`.

A C++ implementation is aligned when `Contracts/`, installed header
projections, tests, samples, perf runners, and runtime behavior follow this
blueprint and map the stable capabilities of `core/include/zlink.h` into
C++-idiomatic types.

## Public Contract Source

- Public contract: `bindings/cpp/src/zlink/Contracts/`.
- Installed projection: `bindings/cpp/include/zlink/*.hpp` and
  `bindings/cpp/include/zlink/service/*.hpp`.
- Namespace: all public types live under `zlink`; service types live under
  `zlink::service`.
- Internal implementation: `.cpp` files, native bridge helpers, callback
  trampolines, request progress helpers, non-installed headers, and
  `detail`-style helpers.
- Documentation role: this README defines the shape, boundaries, and required
  semantic coverage. `Contracts/` owns the exact member list; installed headers
  must project it intentionally.

Do not copy the .NET contract-interface layout into C++. C++ uses installed
headers, RAII classes, concrete values, and optional private detail helpers as
its natural boundary.

## Repository Layout

Use these paths consistently when changing the C++ binding.

- Public contract: `bindings/cpp/src/zlink/Contracts/`.
- Runtime implementation: `bindings/cpp/src/zlink/Runtime/`.
- Native bridge/artifacts: `bindings/cpp/src/zlink/Runtime/Native/` and
  `bindings/cpp/native/`.
- Installed header projection: `bindings/cpp/include/zlink/`.
- Codec extensions: `bindings/cpp/codecs/`.
- Tests: `bindings/cpp/tests/`.
- Samples: `bindings/cpp/samples/`.
- Perf: `bindings/cpp/perf/`.

`Contracts/` and `Runtime/` are fixed repository folders. Installed headers and
the `zlink` namespace are the C++ projection of that contract.
Do not expose `Contracts` or `Runtime` as public namespace segments.

Non-installed helper headers are implementation detail. If a helper must be
included by users, promote it into the installed include tree deliberately.

```text
bindings/cpp/
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
|   +-- zlink/
+-- native/
+-- codecs/
+-- tests/
+-- samples/
+-- perf/
```

## API Change Workflow

When mapping a new core capability:

1. Add the public type or method to the correct `Contracts/` category.
2. Update the installed header projection under `bindings/cpp/include/zlink/`.
3. Decide the C++ domain owner: context, message, socket, monitor, timer,
   service, SPOT, actor, error, or option.
4. Keep raw C handle access, `*_part` loops, and callback userdata inside
   implementation files or non-installed helpers.
5. Add public-header tests and at least one sample/perf update when the new
   capability affects user workflows or measurement.
6. Check that the new public API is not just a shallow C wrapper. If it only
   forwards without improving ownership, validation, or shape, keep it
   internal.

## Library Shape

The C++ binding should feel like a small native C++ library over the core C
contract.

- Public resource objects are RAII classes that own or borrow native handles
  according to their documented lifetime.
- Destructors release resources without requiring callers to know native close
  sequencing.
- Public methods use `snake_case`.
- Public value types such as message, routing id, received metadata, topic
  message, result, error, enum, and option types stay concrete.
- Use templates, overloads, and move semantics only when they simplify caller
  ownership or avoid copies. Do not expose template machinery as a substitute
  for a clear domain type.
- Use virtual interfaces only when callers need substitutable behavior. Do not
  wrap every handle in an abstract interface by default.
- Operation builders are required for multipart send, publish, request, reply,
  actor, and SPOT operations so native request state stays hidden and ownership
  stays clear.

## Contract / Runtime Placement Rules

- Public declarations and user-visible behavior belong in `Contracts/`.
- Public free functions, static helpers, extension-style helpers, and builder
  convenience helpers belong in `Contracts/` when users can call them directly.
- Runtime RAII classes, socket kernels, request pumps, callback trampolines,
  and part-loop helpers belong in `Runtime/`.
- FFI declarations, raw C handles, native struct mirrors, marshalling helpers,
  and platform loading code belong in `Runtime/Native/`.
- Installed headers must project `Contracts/`, not `Runtime/` internals.
- If a runtime concrete class is directly constructible, its public behavior
  must still be described by `Contracts/`.

## Contract Folder Layout

`Contracts/` is the source ownership map for public C++ declarations. Installed
headers project these categories into the `zlink` namespace.

- `Core/`: context, context options, routing id, utility resources, and public
  free functions such as version or capability helpers.
- `Messaging/`: message, received metadata, topic messages, subscription
  events, stream packet callbacks, and builder payload helpers.
- `Sockets/`: socket behavior, socket families, typed options, request/reply,
  and publish/subscribe surfaces.
- `Monitoring/`: monitor, monitor snapshot/event, poller, poll event, timer, and
  public poll helpers.
- `Service/`: registry, discovery, SPOT node, SPOT handle, topology models,
  actor refs, actor lifecycle, and operation builders.
- `Errors/`: exception or typed error-result domains.
- `Enums/`: public enum domains shared across the binding.

## Canonical Interface Rules

- Data-plane `recv`, routed recv, subscribe, and subscription-event receive use
  caller-provided output storage such as `received_t&`,
  `topic_message_t&`, or `subscription_event_t&`.
- Send, routed send, publish, request, reply, SPOT operations, and Actor
  location/session operations return move-only fluent builders.
- Builder start methods take only the target identity, topic, channel, routing
  id, or request sequence. Payload, flags, timeout, callback, and async submit
  choices are builder steps.
- Multipart payload is accumulated by repeated `message(...)` calls.
  `messages(...)` convenience is allowed when it delegates to the same builder
  contract and is declared in `Contracts/`.
- Do not add operation-start overload families such as `send_no_wait`,
  `publish_with_flags`, or `request_async`; keep one operation name and let
  the builder absorb the variation. Terminal builder methods may use idiomatic
  names such as `submit_async`.

## Required Capability Coverage

The public headers must cover these groups.

- Core runtime: context, version/capability helpers, context options, shutdown,
  and auto-HWM recalculation.
- Messaging: message ownership, multipart input, received metadata, topic
  messages, subscription events, routing ids, and callback types.
- Socket families: pair, dealer, router, pub, sub, xpub, xsub, stream, common
  options, typed socket options, bind/connect/disconnect, TLS, callbacks, and
  request/reply surfaces.
- Monitoring: socket monitor, monitor event, monitor snapshot, poller, poll
  event, timer, and readiness flags.
- Services: registry, discovery, SPOT node, SPOT handle, topology snapshots,
  actor refs, actor lifecycle, and actor operations.
- Errors: typed exception or error-result surfaces that preserve the core
  result domains.

The C++ surface should not expose raw native handles, `*_part` loops, callback
userdata, internal inproc endpoints, or request pump objects as public concepts.

## Lifetime And Ownership

C++ callers should not have to reason about C handle cleanup.

- Resource classes release their native handle in their destructor and support
  explicit `close` or equivalent lifecycle methods when the handle can fail to
  close.
- Move-only resource classes are preferred over shared mutable handle
  ownership.
- Message values should support efficient move and explicit copy when copying
  is requested.
- Data-plane receive and subscribe paths use caller-provided storage.
- Service control/admission receive paths such as Actor join request receive may
  use optional or typed result-return forms when that is clearer for C++ callers.
  They must still distinguish no-data from hard receive failure.
- Callbacks must keep native callback lifetime and user callable lifetime
  internally consistent.

## Error And Result Policy

The binding may use exceptions or typed result objects, but the public shape
must preserve core semantics.

- No-data and temporary backpressure remain distinct from hard failures.
- Request, submit, recv, bind, connect, config, handler, and close failures
  keep their result-domain meaning.
- `pollout` is a send-recovery readiness signal, not a generic writable bit.
- ROUTER/PUB defaults, SPOT HWM defaults, and SPOT dispatch worker semantics
  follow the core header.

## Performance Policy

- Build multipart values directly from the core part substrate.
- Avoid unnecessary heap allocation, avoidable copies, reflection-like dynamic
  dispatch, hidden waits, sleeps, busy waits, broad locks, and joins in hot
  paths.
- Perf and samples must include installed public headers only.
- The C++ perf meaning must match `bindings/c/perf`: same pattern semantics,
  same transport meaning, same client-count policy, and no private fast path.

## Implementation Checklist

Before declaring the C++ binding aligned:

- Installed headers expose all stable user-facing core capabilities.
- Public headers are enough for applications, perf, samples, and framework
  adapters.
- Private helper headers are not needed by users.
- Value types remain concrete unless abstraction removes real complexity.
- Public APIs hide native part loops, raw handles, and callback userdata.
- Public helper/free functions and builder convenience methods are declared in
  `Contracts/`, not only in runtime helpers.
- Service control/admission receive exceptions are documented where they differ
  from data-plane caller-provided storage.
- Perf tests use the same measurement meaning as C perf.
