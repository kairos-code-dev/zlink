[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Java Binding Implementation Blueprint

This document defines the expected Java library shape. It is not an exhaustive
list of every class or method. The concrete public contract is
`bindings/java/src/main/java/systems/zlink/` and its documented public
subpackages.

A Java implementation is aligned when the `systems.zlink` package tree, tests,
samples, perf runners, and runtime behavior follow this blueprint and map the
stable capabilities of `core/include/zlink.h` into Java-idiomatic APIs.

## Public Contract Source

- Public contract source:
  `bindings/java/src/main/java/systems/zlink/` and documented
  `bindings/java/src/main/java/systems/zlink/service/*/` packages.
- Runtime implementation:
  `bindings/java/src/main/java/systems/zlink/internal/`.
- Native bridge:
  `bindings/java/src/main/java/systems/zlink/internal/Native*.java`,
  `bindings/java/src/main/resources/native/`, and `bindings/java/native/`.
- Public package projection: `systems.zlink` and documented service packages
  under `systems.zlink.service`.
- Internal packages: `systems.zlink.internal` and any non-exported native
  bridge package.
- Module boundary: if JPMS is used, only public packages are exported.
- Documentation role: this README defines the library shape and required
  semantic coverage. The Java package tree owns the exact public member list.
  Java source and generated API docs must project it intentionally.

Applications, perf, and samples must not import internal packages or native
bridge classes.

## Repository Layout

Use these paths consistently when changing the Java binding.

- Public contract:
  `bindings/java/src/main/java/systems/zlink/`.
- Public service contract:
  `bindings/java/src/main/java/systems/zlink/service/registry/`,
  `bindings/java/src/main/java/systems/zlink/service/discovery/`, and
  `bindings/java/src/main/java/systems/zlink/service/spot/`.
- Runtime implementation:
  `bindings/java/src/main/java/systems/zlink/internal/`.
- Native bridge/artifacts:
  `bindings/java/src/main/java/systems/zlink/internal/`,
  `bindings/java/src/main/resources/native/`, and `bindings/java/native/`.
- Codec extensions: `bindings/java/codec/`.
- Tests: `bindings/java/src/test/` and `bindings/java/tests/`.
- Samples: `bindings/java/samples/`.
- Perf: `bindings/java/perf/`.

If the project uses JPMS, package exports must match the public package list.
Java uses URL-style package naming in its source tree. Do not create
`bindings/java/src/zlink/Contracts/` or `bindings/java/src/zlink/Runtime/`.
Do not create `systems.zlink.Contracts` or `systems.zlink.Runtime` packages.
The package tree below is the implementation and review structure.

```text
bindings/java/
+-- src/
|   +-- main/
|   |   +-- java/
|   |   |   +-- systems/
|   |   |   |   +-- zlink/
|   |   |   |   |   +-- AtomicCounter.java
|   |   |   |   |   +-- Context.java
|   |   |   |   |   +-- Message.java
|   |   |   |   |   +-- PairSocket.java
|   |   |   |   |   +-- DealerSocket.java
|   |   |   |   |   +-- RouterSocket.java
|   |   |   |   |   +-- PubSocket.java
|   |   |   |   |   +-- SubSocket.java
|   |   |   |   |   +-- XPubSocket.java
|   |   |   |   |   +-- XSubSocket.java
|   |   |   |   |   +-- StreamSocket.java
|   |   |   |   |   +-- Received.java
|   |   |   |   |   +-- TopicMessage.java
|   |   |   |   |   +-- SubscriptionEvent.java
|   |   |   |   |   +-- Poller.java
|   |   |   |   |   +-- Timer.java
|   |   |   |   |   +-- ZlinkException.java
|   |   |   |   |   +-- service/
|   |   |   |   |   |   +-- registry/
|   |   |   |   |   |   |   +-- Registry.java
|   |   |   |   |   |   |   +-- RegistryQueryClient.java
|   |   |   |   |   |   +-- discovery/
|   |   |   |   |   |   |   +-- Discovery.java
|   |   |   |   |   |   +-- spot/
|   |   |   |   |   |   |   +-- SpotNode.java
|   |   |   |   |   |   |   +-- Spot.java
|   |   |   |   |   |   |   +-- Actor.java
|   |   |   |   |   |   |   +-- SendOp.java
|   |   |   |   |   |   |   +-- RequestOp.java
|   |   |   |   |   |   |   +-- ReplyOp.java
|   |   |   |   |   |   |   +-- ActorJoinOp.java
|   |   |   |   |   +-- internal/
|   |   |   |   |   |   +-- Native.java
|   |   |   |   |   |   +-- NativeLayouts.java
|   |   |   |   |   |   +-- NativeHelpers.java
|   |   |   |   |   |   +-- LibraryLoader.java
|   |   |   |   |   |   +-- MessagePartsBuffer.java
|   |   |   |   |   |   +-- RequestProgressPump.java
|   |   |   |   |   |   +-- RequestReplySupport.java
|   |   |   |   |   |   +-- SocketOperations.java
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

The package tree itself is the review and ownership layout. Public contract
categories map to Java packages as follows:

| Contract category | Java package path |
|---|---|
| Core | `systems/zlink/*.java` for context, runtime helpers, ids, and utility resources |
| Messaging | `systems/zlink/Message.java`, `Received.java`, `TopicMessage.java`, subscription and callback types |
| Sockets | `systems/zlink/*Socket.java` and socket option/value types |
| Monitoring | `systems/zlink/Monitor*.java`, `Poller.java`, `PollEvent.java`, `Timer.java` |
| Service | `systems/zlink/service/registry`, `systems/zlink/service/discovery`, and `systems/zlink/service/spot` |
| Errors | `systems/zlink/*Exception.java` and result-domain enums |
| Enums | public enum classes under `systems/zlink` or the matching service package |
| Runtime/Native | `systems/zlink/internal` only |

If a public class appears in `systems.zlink` or `systems.zlink.service.*`,
reviewers must be able to explain which contract category it belongs to from
this table. If a class exists only to hold JNI/Panama calls, raw handles,
native struct mirrors, marshalling, request progress, part loops, or callback
trampolines, it must stay under `systems.zlink.internal`, and JPMS must not
export that package.

## API Change Workflow

When mapping a new core capability:

1. Choose the shared contract category that owns the domain.
2. Add a Java class, record, interface, builder, or exception using Java
   conventions.
3. Update the public package and JPMS projection when the new API is public.
4. Keep native handles, downcalls, callback userdata, and part loops inside
   internal packages.
5. Add tests that import only public packages.
6. Update samples and perf only when public user workflows or measurement
   behavior changes.
7. If an internal package becomes necessary for users, redesign the public
   facade instead of exporting the internal package.

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
  request progress helpers stay inside non-exported implementation packages.

Do not introduce interfaces for pure DTOs only for symmetry. Messages,
routing ids, received metadata, topic messages, result values, snapshots, and
options should remain concrete unless Java callers gain a real abstraction.

## Contract / Runtime Placement Rules

- Public interfaces, records/classes, enums, exceptions, and builder contracts
  belong in `systems.zlink` or a documented public `systems.zlink.service.*`
  package.
- Public static helpers, factory facades, package-level utility classes, and
  builder convenience helpers belong in public packages when callers can use
  them directly.
- Runtime implementation classes, handle owners, request pumps, callback
  adapters, and part-loop helpers belong in `systems.zlink.internal`.
- JNI/Panama downcalls, native struct mirrors, marshalling helpers, and
  platform loading code belong in `systems.zlink.internal` or the native
  artifact/resource area.
- Public `systems.zlink` packages must project the contract categories, not
  expose runtime packages.
- If a runtime concrete class is public for construction, its public behavior
  must still be described by the public contract.

## Contract Category Map

The public `systems.zlink` packages are the source ownership map for public
Java APIs.

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

The public package layout should make API ownership easy to inspect.

- `systems.zlink`: context, messages, sockets, monitoring, errors, options,
  routing ids, poller, timer, and utility entrypoints.
- `systems.zlink.service.registry`: registry public API and registry snapshot
  models.
- `systems.zlink.service.discovery`: discovery public API and topology models.
- `systems.zlink.service.spot`: SPOT node, SPOT handle, SPOT topology, actor
  refs, actor lifecycle, and SPOT operation builders.
- `systems.zlink.internal`: native downcalls, handle ownership, callback
  trampolines, request pumps, and converters.

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
