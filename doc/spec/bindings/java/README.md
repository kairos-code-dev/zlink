[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Java Binding Implementation Blueprint

This document defines the expected Java library shape. It is not an exhaustive
list of every class or method. The concrete public contract is
`bindings/java/src/main/java/systems/zlink/contracts/`. Service APIs use
documented public subpackages below that path.

A Java implementation is aligned when the `systems.zlink.contracts` and
non-exported `systems.zlink.runtime` package trees, tests, samples, perf
runners, and runtime behavior follow this blueprint and map the stable
capabilities of `core/include/zlink.h` into Java-idiomatic APIs.

## Public Contract Source

- Public contract source:
  `bindings/java/src/main/java/systems/zlink/contracts/`.
- Runtime implementation:
  `bindings/java/src/main/java/systems/zlink/runtime/`.
- Native bridge:
  `bindings/java/src/main/java/systems/zlink/runtime/nativebridge/`,
  `bindings/java/src/main/resources/native/`, and `bindings/java/native/`.
- Public package projection: documented packages under
  `systems.zlink.contracts`.
- Runtime packages: `systems.zlink.runtime.*`; these packages are not public
  API and must not be exported by JPMS.
- Module boundary: if JPMS is used, only `systems.zlink.contracts` and
  documented service contract subpackages are exported.
- Documentation role: this README defines the library shape and required
  semantic coverage. The `contracts` package tree owns the exact public member
  list. Java source and generated API docs must project it intentionally.

Applications, perf, and samples must import only `systems.zlink.contracts` and
documented service contract subpackages. They must not import
`systems.zlink.runtime.*` or native bridge classes.

## Repository Layout

Use these paths consistently when changing the Java binding.

- Public contract:
  `bindings/java/src/main/java/systems/zlink/contracts/`.
- Public service contract:
  `bindings/java/src/main/java/systems/zlink/contracts/service/registry/`,
  `bindings/java/src/main/java/systems/zlink/contracts/service/discovery/`,
  and `bindings/java/src/main/java/systems/zlink/contracts/service/spot/`.
- Runtime implementation:
  `bindings/java/src/main/java/systems/zlink/runtime/`.
- Native bridge/artifacts:
  `bindings/java/src/main/java/systems/zlink/runtime/nativebridge/`,
  `bindings/java/src/main/resources/native/`, and `bindings/java/native/`.
- Codec extensions: `bindings/java/codec/`.
- Tests: `bindings/java/src/test/` and `bindings/java/tests/`.
- Samples: `bindings/java/samples/`.
- Perf: `bindings/java/perf/`.

If the project uses JPMS, package exports must match the public contract package
list. Java uses URL-style package naming in its source tree, so the role names
are lower-case Java packages. Do not create `systems.zlink.Contracts` or
`systems.zlink.Runtime` packages. Use `systems.zlink.contracts.*` and
`systems.zlink.runtime.*` exactly as shown below. The package tree below is the
implementation and review structure.

```text
bindings/java/
+-- src/
|   +-- main/
|   |   +-- java/
|   |   |   +-- systems/
|   |   |   |   +-- zlink/
|   |   |   |   |   +-- contracts/
|   |   |   |   |   |   +-- Context.java
|   |   |   |   |   |   +-- Zlink.java
|   |   |   |   |   |   +-- RoutingId.java
|   |   |   |   |   |   +-- Message.java
|   |   |   |   |   |   +-- Received.java
|   |   |   |   |   |   +-- TopicMessage.java
|   |   |   |   |   |   +-- PairSocket.java
|   |   |   |   |   |   +-- DealerSocket.java
|   |   |   |   |   |   +-- RouterSocket.java
|   |   |   |   |   |   +-- MonitorSocket.java
|   |   |   |   |   |   +-- Poller.java
|   |   |   |   |   |   +-- service/
|   |   |   |   |   |   |   +-- registry/
|   |   |   |   |   |   |   +-- discovery/
|   |   |   |   |   |   |   +-- spot/
|   |   |   |   |   +-- runtime/
|   |   |   |   |   |   +-- nativebridge/
|   |   +-- resources/
|   |   |   +-- native/
|   |   |   |   +-- linux-x86_64/
|   |   |   |   +-- linux-aarch64/
|   |   |   |   +-- darwin-x86_64/
|   |   |   |   +-- darwin-aarch64/
|   |   |   |   +-- windows-x86_64/
|   |   |   |   +-- windows-aarch64/
|   +-- test/
|   |   +-- java/
|   |   |   +-- systems/
|   |   |   |   +-- zlink/
+-- native/
+-- codec/
+-- tests/
+-- samples/
+-- perf/
```

The Java binding currently keeps core, messaging, sockets, monitoring, errors,
and enums in one exported `systems.zlink.contracts` package. This keeps
package-private native and marshalling helpers hidden from reflection-based API
guards while still separating the public contract package from the non-exported
runtime package. Service contracts use subpackages because their public surface
does not require the same package-private sharing.

The category table below is the review and ownership map, not a request to
create Java subpackages for every category.

| Contract category | Java package path |
|---|---|
| Core | `systems/zlink/contracts` |
| Messaging | `systems/zlink/contracts` |
| Sockets | `systems/zlink/contracts` |
| Monitoring | `systems/zlink/contracts` |
| Service | `systems/zlink/contracts/service` and its service subpackages |
| Errors | `systems/zlink/contracts` |
| Enums | `systems/zlink/contracts` |
| Runtime/Core | `systems/zlink/runtime` |
| Runtime/Messaging | `systems/zlink/runtime` |
| Runtime/Sockets | `systems/zlink/runtime` |
| Runtime/Monitoring | `systems/zlink/runtime` |
| Runtime/Service | `systems/zlink/runtime` |
| Runtime/Errors | `systems/zlink/runtime` |
| Runtime/Enums | `systems/zlink/runtime` |
| Runtime/Native | `systems/zlink/runtime/nativebridge` |

If a public class appears under `systems.zlink.contracts.*`, reviewers must be
able to explain which contract category it belongs to from this table. If a
class exists only to hold JNI/Panama calls, raw handles, native struct mirrors,
marshalling, request progress, part loops, or callback trampolines, it must
stay under `systems.zlink.runtime.*`, and JPMS must not export that package.

## API Change Workflow

When mapping a new core capability:

1. Choose the shared contract category that owns the domain.
2. Add a Java class, record, interface, builder, or exception using Java
   conventions.
3. Update the public `contracts` package and JPMS projection when the new API
   is public.
4. Keep native handles, downcalls, callback userdata, and part loops inside
   `runtime` packages.
5. Add tests that import only public packages.
6. Update samples and perf only when public user workflows or measurement
   behavior changes.
7. If a runtime package becomes necessary for users, redesign the public facade
   instead of exporting the runtime package.

## Library Shape

The Java binding should look like a Java library, not a C header translated
method by method.

- Resource types implement `AutoCloseable`.
- Public resource abstractions may use interfaces when they let callers depend
  on behavior instead of implementation classes.
- Concrete values use records or final classes where appropriate.
- Exceptions represent typed zlink error domains.
- Builders are required for multipart send, publish, request, reply, SPOT, and
  actor operations so native request state stays hidden.
- JNI, Panama, native handles, callback userdata, part-loop sequencing, and
  request progress helpers stay inside non-exported runtime packages.

Do not introduce interfaces for pure DTOs only for symmetry. Messages,
routing ids, received metadata, topic messages, result values, snapshots, and
options should remain concrete unless Java callers gain a real abstraction.

## Contract / Runtime Placement Rules

- Public interfaces, records/classes, enums, exceptions, and builder contracts
  belong in `systems.zlink.contracts` or a documented service contract
  subpackage.
- Public static helpers, factory facades, package-level utility classes, and
  builder convenience helpers belong in `contracts` packages when callers can
  use them directly.
- Runtime implementation classes, handle owners, request pumps, callback
  adapters, and part-loop helpers belong in `systems.zlink.runtime.*`.
- JNI/Panama downcalls, native struct mirrors, marshalling helpers, and
  platform loading code belong in `systems.zlink.runtime.nativebridge` or the
  native artifact/resource area.
- Public contract packages must project the contract categories, not expose
  runtime packages.
- If a runtime concrete class is public for construction, its public behavior
  must still be described by the public contract.

## Contract Category Map

The public `systems.zlink.contracts` package and documented service
subpackages are the source ownership map for public Java APIs.

- `Core/`: context, context options, routing id, version/capability helpers, and
  utility contracts.
- `Messaging/`: message, received metadata, topic messages, subscription events,
  stream packet callbacks, and builder payload helpers.
- `Sockets/`: socket behavior, socket families, typed options, request/reply,
  and publish/subscribe surfaces.
- `Monitoring/`: monitor, monitor snapshot/event, poller, poll event, timer, and
  public poll helpers.
- `Service/`: registry, discovery, SPOT node, SPOT handle, topology models,
  actor refs, actor lifecycle, and operation builders.
- `Errors/`: exception and typed error-result domains.
- `Enums/`: public enum domains shared across the binding.

## Canonical Interface Rules

- Data-plane `recv`, routed recv, `subscribe`, and subscription-event receive
  fill caller-provided `Received`, `TopicMessage`, or `SubscriptionEvent`
  objects and return `boolean`.
- Send, routed send, publish, request, reply, SPOT operations, and Actor
  location/session operations return staged builders.
- Builder start methods take only the target identity, topic, channel, routing
  id, or request sequence. Payload, flags, timeout, callback, and async submit
  choices are builder steps.
- Multipart payload is accumulated by repeated `message(...)` calls.
  `messages(...)` convenience is allowed when it delegates to the same builder
  contract and is declared in the public package category.
- Do not add operation-start method families such as `sendNoWait`,
  `publishWithFlags`, or `requestAsync`; keep one operation name and let the
  builder absorb the variation. Terminal builder methods may use idiomatic
  names such as `submitAsync`.

## Package Layout

The public package layout should make API ownership easy to inspect without
forcing package-private native helpers into the public surface.

- `systems.zlink.contracts`: core, messaging, sockets, monitoring, errors, and
  enum contracts.
- `systems.zlink.contracts.service.registry`: registry public API and registry
  snapshot models.
- `systems.zlink.contracts.service.discovery`: discovery public API and topology
  models.
- `systems.zlink.contracts.service.spot`: SPOT node, SPOT handle, SPOT
  topology, actor refs, actor lifecycle, and SPOT operation builders.
- `systems.zlink.runtime.*`: non-exported implementation packages.
- `systems.zlink.runtime.nativebridge`: native downcalls, handle ownership,
  callback trampolines, request pumps, converters, and native struct mirrors.

If a package is not exported or documented, it is not public contract even if
Java visibility is broader for implementation reasons.

## Required Capability Coverage

The Java public contract must cover stable user-facing core capabilities.

- Context lifecycle, context options, version, capability, strerror, shutdown,
  and auto-HWM recalculation.
- Message ownership, multipart payloads, routing ids, received metadata, topic
  messages, subscription events, and stream packet callbacks.
- Pair, dealer, router, pub, sub, xpub, xsub, and stream sockets.
- Common options, typed socket options, TLS, bind/connect/disconnect, routing
  id, channel name, request/reply, publish/subscribe, and callback surfaces.
- Monitor, monitor event/snapshot, poller, poll event, timer, and timer/SPOT
  integration.
- Registry, discovery, SPOT node, SPOT handle, topology snapshots, actor refs,
  actor operations, actor lifecycle, and stream actor binding.
- Typed exceptions that preserve core submit/request/recv/handler/close/bind/
  connect/config result meanings.

Raw `*_part` loops, callback userdata, and native handle helpers are internal
implementation primitives unless the public Java API intentionally exposes a
typed facade for the same capability.

## Spot Get-Or-Create

Java exposes `SpotNode.getOrCreateSpot(RoutingId)`. It maps directly to
`zlink_spot_node_spot_get_or_new(...)`; it must not be implemented by composing
`spotLookup` and `createSpot`.

The method returns a concrete result containing the caller-owned `Spot` facade
and a `created` boolean. `created` is `true` only for the call that created the
logical spot.

## Receive And Subscribe Shape

Java receive APIs should avoid unnecessary allocation while staying idiomatic.

- Data-plane receive and subscribe APIs must use caller-provided result objects
  for reusable storage.
- No-data must be distinguishable from hard receive failure.
- Hard receive failures raise the documented zlink exception type.
- SPOT readable dispatch events are readiness notifications. Callers drain the
  corresponding receive API until no-data.
- Service control/admission receive paths such as Actor join request receive may
  use `Optional`, nullable, or typed result-return forms when they are clearer
  than reusable data-plane storage. They must still distinguish no-data from hard
  receive failure.

## Error And Validation Policy

- Validate fixed-size native boundary values before calling core.
- Do not silently truncate routing ids, actor ids, endpoints, channel names, or
  topics.
- Preserve core result-domain meaning in Java exceptions and result values.
- Do not expose native errno as the primary public error API.

## Performance Policy

- Hot paths must not use reflection, dynamic method lookup, classpath scanning,
  avoidable allocation, avoidable buffer copies, hidden waits, sleeps, busy
  waits, broad locks, or thread joins.
- Callback stub or method-handle setup may happen during registration, not in
  the per-message processing loop.
- Native bridge code should materialize Java values directly from the core
  part substrate.
- Perf, samples, and tests use exported public packages only.

## Implementation Checklist

- Exported packages cover all public behavior.
- Internal packages do not leak through public signatures.
- Resource classes close deterministically.
- DTOs and records remain concrete.
- Public static helpers and builder convenience methods are declared in public
  packages, not only in runtime helpers.
- Receive/subscription semantics match the shared binding policy.
- Service control/admission receive exceptions are documented where they differ
  from data-plane caller-provided storage.
- Perf measurement meaning matches `bindings/c/perf`.
