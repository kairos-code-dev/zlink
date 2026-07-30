[English](README.md) | [한국어](README.ko.md)

[Spec Index](../../../../core/doc/spec/README.md) · [Bindings Policy](../README.md)

# .NET Binding Implementation Blueprint

This document defines the expected .NET library shape. It is not an exhaustive
list of every interface member. The concrete public contract source is
`bindings/dotnet/src/Zlink/Contracts/`.

A .NET implementation is aligned when `Contracts/`, the runtime implementation
classes, tests, samples, perf runners, and package behavior follow this
blueprint and map the stable capabilities of `core/include/zlink.h` into
.NET-idiomatic APIs.

This README describes the completed .NET binding shape, not a temporary target
draft. It is also the reference guide for keeping other wrapper binding
documents aligned to the same architecture map. When another binding uses
language-specific naming, it should still preserve the same contract/runtime
ownership, public contract categories, file granularity, and verification
intent described here.

This binding follows the shared bindings architecture map with .NET naming:
`Contracts/<Category>` owns public contract source and `Runtime/<Category>`
owns implementation. Other bindings may use different casing or package names,
but this document is the .NET projection of the same map.

The first code a reviewer reads should be the public contract under
`Contracts/`. Runtime files must implement that contract; they must not be the
place where new user-facing behavior is discovered.

## Public Contract Source

- Public namespace: `Systems.Zlink`.
- Package identity: `Systems.Zlink`.
- Public contract: `bindings/dotnet/src/Zlink/Contracts/`.
- Runtime implementation: `bindings/dotnet/src/Zlink/Runtime/`.
- Internal implementation: P/Invoke declarations, `SafeHandle` or native
  handle ownership, callback trampolines, request progress pumps, native model
  converters, socket kernels, option accessors, buffer codecs, and validation
  helpers.
- Documentation role: this README defines the library shape and review rules.
  `Contracts/` owns the exact public behavior surface.
- API reference comments: [`api-reference-comments.md`](api-reference-comments.md)
  defines how XML comments in `Contracts/` are written and reviewed.

Runtime implementation files must not define user-facing behavior that cannot
be understood through `Contracts/` or documented construction entrypoints.

## Repository Layout

Use these paths consistently when changing the .NET binding.

- Public contract: `bindings/dotnet/src/Zlink/Contracts/`.
- Runtime implementation: `bindings/dotnet/src/Zlink/Runtime/`.
- Native bridge/artifacts: `bindings/dotnet/src/Zlink/Runtime/Native/` and
  `bindings/dotnet/native/`. NuGet packages place these files under
  `runtimes/<rid>/native/`.
- Codec packages: not provided. .NET bindings keep only raw `Message` and byte
  payload APIs.
- Tests: `bindings/dotnet/tests/Zlink.Tests/`.
- Samples: `bindings/dotnet/samples/`.
- Perf: `bindings/dotnet/perf/`.

`Contracts/` public signatures must stay free of P/Invoke declarations,
`SafeHandle` details, native struct mirrors used only for marshalling, and
request pump types. Concrete value types may use internal native-backed
storage when that is required for ownership, but .NET must not expose or use
VM-managed buffer borrowed/zero-copy send paths as public or default behavior.
Native bridge declarations and marshalling-only mirrors still belong in
`Runtime/Native/`.
`Contracts/` and `Runtime/` are fixed repository folders. The
`Systems.Zlink` namespace and NuGet package surface are the .NET projection of
that contract.
Do not expose namespace segments named `Contracts` or `Runtime` as the primary
user-facing namespace.
The following tree is normative for ownership and shows representative files.
It is not a complete file inventory. Files that define public behavior must be
placed under `Contracts/`; files that exist to call native code, own handles,
marshal structs, or run callback/request progress logic must be placed under
`Runtime/`, with native bridge code under `Runtime/Native/`.

```text
bindings/dotnet/
+-- src/
|   +-- Zlink/
|   |   +-- Contracts/
|   |   |   +-- Core/
|   |   |   |   +-- Context.cs
|   |   |   |   +-- ContextOptions.cs
|   |   |   |   +-- RoutingId.cs
|   |   |   |   +-- Zlink.cs
|   |   |   +-- Messaging/
|   |   |   |   +-- Message.cs
|   |   |   |   +-- Received.cs
|   |   |   |   +-- TopicMessage.cs
|   |   |   |   +-- SubscriptionEvent.cs
|   |   |   |   +-- OperationContracts.cs
|   |   |   +-- Sockets/
|   |   |   |   +-- ISocket.cs
|   |   |   |   +-- MessageSocketContracts.cs
|   |   |   |   +-- RoutedSocketContracts.cs
|   |   |   |   +-- PubSubSocketContracts.cs
|   |   |   |   +-- IStreamSocket.cs
|   |   |   |   +-- SocketOptionFacades.cs
|   |   |   +-- Eventing/
|   |   |   |   +-- Monitor.cs
|   |   |   |   +-- Poller.cs
|   |   |   |   +-- PollEvent.cs
|   |   |   |   +-- Timer.cs
|   |   |   |   +-- ZlinkPoll.cs
|   |   |   +-- Service/
|   |   |   |   +-- SpotNode.cs
|   |   |   |   +-- Spot.cs
|   |   |   |   +-- Actor.cs
|   |   |   |   +-- SpotNodeModels.cs
|   |   |   +-- Errors/
|   |   |   |   +-- Errors.cs
|   |   +-- Runtime/
|   |   |   +-- Core/
|   |   |   +-- Handles/
|   |   |   +-- Messaging/
|   |   |   +-- Sockets/
|   |   |   +-- Eventing/
|   |   |   +-- Service/
|   |   |   +-- Errors/
|   |   |   +-- Buffers/
|   |   |   +-- Options/
|   |   |   +-- Native/
+-- tests/
+-- samples/
+-- perf/
+-- native/
+-- runtimes/
```

The `Contracts` and `Runtime` folder names are repository ownership boundaries.
They are not a license to expose `Systems.Zlink.Contracts` or
`Systems.Zlink.Runtime` as user-facing namespaces. Public construction should
return public contracts such as `IContext`, socket interfaces, `ISpotNode`,
`IPoller`, or `IZlinkTimer` unless the public contract explicitly requires a
concrete value type. Runtime classes such as `Context`, socket classes,
`SpotNode`, `Poller`, and `Timer` are implementation owners, not the preferred
consumer-facing surface.

`Runtime/Buffers`, `Runtime/Handles`, and `Runtime/Options` are implementation
support categories. They exist because the .NET binding has real native
ownership, routing-id encoding, and option-validation decisions to hide. Other
bindings may name these support areas differently, but they should not move
those details into public contract files.

## API Change Workflow

When mapping a new core capability:

1. Add the user-facing behavior to the correct `Contracts/` category.
2. Use a concrete DTO/value/record type unless callers need substitutable
   behavior.
3. Add or update the `Runtime/` implementation without exposing native bridge
   types.
4. Document any new construction entrypoint if an interface alone cannot
   create the object.
5. Add tests against the public contract, not `internal` members.
6. Update samples and perf only through public contracts and public factories.
7. Verify framework adapters do not use reflection or `InternalsVisibleTo` to
   reach private binding members.

When refactoring existing .NET code:

1. Move user-facing declarations to the matching `Contracts/` category.
2. Move native-backed implementation, handle ownership, request progress,
   marshalling, and option validation to `Runtime/`.
3. Keep P/Invoke declarations and native struct mirrors in `Runtime/Native/`.
4. Remove duplicate public entrypoints that preserve an old shape without
   reducing caller complexity.
5. Update samples, perf, and framework adapters through public contracts and
   documented construction entrypoints only.
6. Add or update tests from the public `Systems.Zlink` surface.

The refactor is complete only when .NET-specific shortcuts below are absent.

- Public contracts do not mention P/Invoke, `SafeHandle`, native structs, raw
  option ids, callback userdata, request pump state, or part-loop helpers.
- Runtime classes do not introduce public behavior that cannot be found from
  `Contracts/`.
- Framework adapters, samples, perf, and tests do not use reflection,
  `NonPublic` lookup, or private runtime shortcuts.
- Compatibility wrappers are not kept only to preserve an older public shape.

## Library Shape

The .NET binding uses a contract/runtime split.

- Behavior contracts are public `I*` interfaces in `Contracts/`.
  Operation builder contracts may use domain names such as `SendOperation` or
  `RequestOperation` when that is the package's established public shape.
- Native-backed implementations are internal sealed classes in `Runtime/` when
  callers should construct resources through public factories, such as
  `Context`, `DealerSocket`, `RouterSocket`, `SpotNode`, `Poller`, and
  `Timer`.
- Non-constructible abstract base classes may exist in `Runtime/` only as
  implementation support for those runtime implementation classes. They are
  not construction entrypoints, and their public behavior must be covered by
  `Contracts/` interfaces or value types.
- DTO, value, result, option, enum, and exception types stay concrete. Use
  `record`, `sealed class`, `readonly struct`, or `enum` according to normal
  .NET usage. Envelopes that own message parts and must be disposed use
  `sealed class`, not `record`.
- Operation builders are interfaces because they hide staged native request
  state and multipart accumulation.
- Public static facades, extension methods, and builder convenience helpers are
  part of the contract when callers can invoke them directly. Define them under
  the owning `Contracts/` category even when their implementation delegates to
  runtime code.
- Native handles, request pumps, callback bridge state, part-loop sequencing,
  and raw option ids stay in `Runtime/` or `internal` implementation types.
- Disposable native resources implement both `IDisposable` and
  `IAsyncDisposable`.

Do not make DTOs such as `Message`, `RoutingId`, `Received`, or
`TopicMessage` into interfaces only for symmetry. They are concrete domain
values with clear ownership and allocation behavior. `Received` is created
with `Received.Create()` because it is caller-provided reusable recv storage.

The following .NET types define the standard interface classification that
other wrapper binding documents mirror:

- Core resource: `IContext`.
- Socket resource roles: `ISocket`, `IMessageSocket`, routed socket contracts,
  pub/sub socket contracts, and family interfaces for pair, dealer, router,
  pub, sub, xpub, xsub, and stream sockets when the family has native-backed
  behavior.
- Eventing resource roles: monitor socket contract, `IPoller`, poll event
  source contracts, and `IZlinkTimer`.
  `ISpotNode`, `ISpot`, and `IActor` or the equivalent actor resource contract
  when actor handles are exposed.
- Operation builder roles: send, routed send, request, reply, publish, channel
  send/request, SPOT send/request/reply, actor create, actor join, and actor
  join reply operations.
- Callback roles: stream packet handlers, monitor handlers, poll handlers,
  SPOT dispatch handlers, route handlers, admission handlers, request
  callbacks, and reply callbacks.

### RoutingId String And Binary Helpers

`RoutingId` remains a binary-safe value type. The public .NET helpers use these
meanings:

- `RoutingId.From(string value)` encodes a user routing id string as UTF-8.
- `RoutingId.From(byte[] value)` and `RoutingId.From(ReadOnlySpan<byte> value)`
  preserve raw routing id bytes.
- `RoutingId.FromHex(value)` restores bytes previously emitted by `ToHex()`.
- `RoutingId.From(uint value)` writes a 4-byte big-endian `uint32` routing id.
- `RoutingId.From(Guid value)` writes a 16-byte UUID routing id.
- `ToString()` is display-oriented: printable UTF-8 text, then `uint32`, then
  UUID, then `hex:` plus raw hex when no clearer representation exists.

Use `ToHex()` / `FromHex(value)` for durable raw-byte round trips.

`RoutingId` caching is an internal optimization only. The binding may cache
hashes or short-lived receive-path values, but equality and public behavior are
defined only by the immutable byte value.

## Contract / Runtime Placement Rules

- Public interfaces, concrete DTO/value types, enums, and public exception
  domains belong in `Contracts/`.
- Public static facades, extension methods, module-style helpers, and builder
  convenience helpers belong in `Contracts/`.
- Default implementations, socket kernels, request pumps, callback bridge
  state, and lifecycle owners belong in `Runtime/`.
- P/Invoke declarations, `SafeHandle` implementations, native struct mirrors,
  marshalling helpers, and platform loading code belong in `Runtime/Native/`.
- `Contracts/` public signatures must not mention `Runtime/Native/` types.
- If a runtime class is ever intentionally exposed for direct construction, its
  public behavior must still be described by `Contracts/`. This is an
  exception, not the default shape.

## Canonical Interface Rules

- Data-plane `Recv`, routed recv, `Subscribe`, and subscription-event receive
  fill caller-provided `Received`, `TopicMessage`, or `SubscriptionEvent`
  instances and return `bool`.
- .NET callers create reusable receive storage with `Received.Create()`.
  `Received` has no public constructor.
- `Send`, routed send, `Publish`, `Request`, `Reply`, SPOT operations, and
  Actor location/session operations return fluent operation builders.
- Builder start methods take only the target identity, topic, channel, routing
  id, or request sequence. Payload, flags, timeout, callback, and async submit
  choices are builder steps.
- Reply builders do not have a send-flags step. Core reply functions do not
  accept send flags, so the .NET binding must not expose a no-op public
  `Flags(...)` contract for replies.
- Do not add single-payload shortcut overloads with the same name as an
  operation start method. `Send(Message)`, `Send(RoutingId, Message)`,
  `Publish(string, Message)`, `SendToChannel(string, Message)`, and
  `SendToSpot(..., Message)` are not public contract members; callers use
  `Send(...).Message(message).Submit()`.
- Multipart payload is accumulated by repeated `Message(...)` calls.
  `Messages(...)` style convenience methods are allowed, but they are public
  builder contract members and belong in `Contracts/`.
- `IDealerSocket` must not expose protocol envelope helpers such as
  `RequestFrame(...)` or `Reply(requestToken, parts)`. A dealer can start a
  request through `Request()`, but it cannot reply to an arbitrary token
  because it has no API-level peer routing id. Reply starts from a received
  request context or from router/SPOT reply surfaces where the target context
  is explicit.
- Message payload factories use `Message.From(...)` overloads. Source-type
  suffixes such as `FromBytes` and value-style factories such as `Of` are not
  part of the public contract.
- Do not add operation-start method families such as `SendNoWait`,
  `PublishWithFlags`, or `RequestAsync`; keep one operation name and let the
  builder absorb the variation. Awaitable terminal builder methods use
  `Async(...)`; callback completion surfaces may use `Submit(callback)`.

## Contract Folder Layout

`Contracts/` should be readable as the public API map.

- `Core/`: context, context options, routing id, utility resource contracts.
- `Messaging/`: message, received metadata, topic messages, subscription
  events, common send/request/reply operation contracts, and message-domain
  convenience helpers.
- `Sockets/`: socket behavior contracts, socket capability interfaces, and
  typed option facades.
- `Eventing/`: monitor, monitor snapshot/event, poller, timer, and poll
  event contracts. Static poll helpers are included here when public.
- `Service/`: SPOT node, SPOT handle, topology models,
  actor refs, actor lifecycle, and service-specific operation builders.
- `Errors/`: exception hierarchy and error-domain mapping.

Files inside each category follow the user-facing concept, not implementation
sequence. Common messaging operations are split as send, request, and reply;
service topology models are split between SPOT node models and shared topology
enums. Request result and callback types belong with
messaging request contracts, not socket enum files. Received message kind
belongs with received-message metadata. SPOT node modes, socket snapshots, Spot
snapshots, and actor snapshots belong with SPOT node models.

SPOT remains a single handle contract through `ISpot`. Do not split it into
role interfaces unless callers actually need to accept those roles separately.
`ISpotNode` may compose separate role interfaces for node configuration, peer
connections, Spot creation, Actor operations, and topology queries. The
default factory return type and user-facing handle remain `ISpotNode`, and
role interfaces must not expose runtime implementation types.
Use named callback delegates for SPOT callback registration so public
signatures describe the callback meaning without adding wrapper context
objects. Registration methods use `Set...Handler` names because the current
handler is stored or replaced; `On...` names are reserved for methods that are
called when an event happens. Declare those delegates next to `ISpot` because
they are only used by the SPOT handle contract. Lifecycle data types stay
with actor models. Lifecycle event envelopes that own message parts use
sealed classes rather than copyable records. Actor operation contracts are
split by join, management, and session binding.

If a user or framework adapter needs a public API, it should be discoverable
from this folder without reading P/Invoke or runtime bridge code.

## Runtime Folder Layout

`Runtime/` mirrors the same standard map, but it contains implementation only.

- `Core/`: context lifecycle, counters, stopwatch, thread helpers, and runtime
  version/capability calls.
- `Handles/`: native resource ownership, close state, lifetime checks, and
  reference tracking.
- `Messaging/`: multipart message materialization, request/reply progress,
  request state, received handlers, and topic encoding.
- `Sockets/`: socket base classes, socket kernels, socket implementations,
  callback adapters, option accessors, receive helpers, and operation
  implementation classes.
- `Eventing/`: poller, timer, monitor state, callback delivery, and event
  materialization helpers.
- `Service/`: SPOT node, Spot, Actor, topology converters,
  service option support, and service operation implementations.
- `Errors/`: boundary validation, native result mapping, and errno translation.
- `Buffers/`: routing-id codec, payload buffer ownership, copy/borrow policy,
  and snapshot buffer helpers.
- `Options/`: context/socket option constants, validation, and runtime option
  conversion.
- `Native/`: P/Invoke declarations, platform loading, native type mirrors, and
  marshalling helpers.

Runtime code may depend on public contract types. Contract files may internally
delegate to runtime code for public factory/static facade wiring, but their
public signatures must not expose runtime implementation details.

## Construction Entry Points

Interfaces define behavior; construction is provided by public factories.

- `Zlink.CreateContext()` creates the runtime context implementation.
- `Zlink.CreateAtomicCounter()`, `CreateStopwatch()`, and
  `CreateThread(...)` create utility resources through public contracts.
- `IContext.CreatePairSocket()`, `CreateDealerSocket()`,
  `CreateRouterSocket()`, `CreatePubSocket()`, `CreateSubSocket()`,
  `CreateXPubSocket()`, `CreateXSubSocket()`, and `CreateStreamSocket()`
  create runtime socket implementations.
  create runtime socket implementations.
- `IContext.CreateSpotNode()` and `CreateSpotNode(SpotNodeMode)` create
  service-layer implementations.
- `Spot` handles are obtained through `ISpotNode.CreateSpot()`,
  `ISpotNode.EntrySpot()`, `ISpotNode.GetOrCreateSpot(...)`, or
  `ISpotNode.SpotLookup(...)`; direct `Spot` construction is not public.
  `GetOrCreateSpot(...)` maps directly to
  `zlink_spot_node_spot_get_or_new(...)` and must not be implemented by
  combining lookup and create in managed code.
- `Actor` handles are created through `ISpotNode.CreateActor(...)`; direct
  Actor construction is not public.
- `Zlink.CreatePoller()`, `Zlink.CreateTimer()`, and
  `Zlink.CreateTimer(ISpot)` create eventing resources.
- `Zlink.Version()`, `Zlink.Has(...)`, `Zlink.Strerror(...)`, `Zlink.Proxy(...)`,
  `Zlink.ProxySteerable(...)`, `Zlink.Sleep(...)`, `Zlink.MultipartClose(...)`,
  and `ZlinkPoll.Poll(...)` are public static facades. Their callable behavior
  is contract surface even though native calls stay in `Runtime/`.

Factory return types should prefer public contracts where callers do not need
the concrete runtime type.

## Required Capability Coverage

The .NET public contract must cover every stable user-facing core capability.
The shape may be narrower or more idiomatic than C, but the meaning must stay
the same.

- Context lifecycle, options, shutdown, auto-HWM recalculation, version,
  capability, and strerror helpers.
- Message ownership, multipart payloads, routing ids, received metadata, topic
  messages, and subscription events.
- Pair, dealer, router, pub, sub, xpub, xsub, and stream sockets.
- Common options, typed socket options, TLS, bind/connect/disconnect, routing
  id, channel name, request/reply, publish/subscribe, and callback surfaces.
- Socket monitor, monitor event/snapshot, poller, poll events, timer, and
  timer integration with SPOT.
- SPOT node, SPOT handle, topology snapshots, actor refs,
  actor operations, actor lifecycle, and stream actor binding.
- Typed exceptions for submit, request, recv, handler, close, bind, connect,
  and config failures.

Native helper functions that exist only to support part loops, callback
userdata, interop marshalling, or request progress stay internal.

## Receive And Subscribe Shape

.NET recv-style data-plane APIs use caller-provided output storage for
allocation-free draining.

- Message/routed receive fills a caller-provided `Received` object created by
  `Received.Create()` and returns `bool`.
- Raw `SUB` / `XSUB` and SPOT subscribe fill a caller-provided `TopicMessage`
  or `SubscriptionEvent` object and return `bool`.
- `false` means no data only for nonblocking receive with `RecvFlags.DontWait`.
- Hard receive failures throw `ZlinkRecvException`.
- Control-plane APIs such as monitor recv and timer recv may keep nullable
  return forms when no-data is the natural value shape.
- Service control/admission APIs such as `RecvActorJoin(...)` may also keep
  nullable return forms. They are not data-plane drain APIs, but no-data and
  hard receive failures must remain distinct.

SPOT `SubscribeReadable` and `RoutedReadable` dispatch events are readiness
notifications. Callers drain the matching receive API until it reports no data.

## Service And SPOT Shape

SPOT is a service-layer API, not a raw socket leak.

- `ISpotNode` owns node lifecycle, route identity, peer connections,
  route bridge/channel coordination, external pub ingress attachment, topology
  snapshots, spot creation, and actor creation.
- `ISpot` owns SPOT topic publish/subscribe, routed send/request/reply,
  routed receive, dispatch events, actor join receive/reply, and actor
  lifecycle callbacks.
- `Spot.Publish(topic)` enters the owning node's SPOT topic plane. It does not
  expose or select a raw `PUB` socket.
- `Spot.Publish(topic)` keeps the short publish name because the receiver is
  already a publish-capable `Spot`. Do not rename it to `PublishSpot` or
  `PublishToTopic` in the binding contract.
- Channel-targeted SPOT operations use `SendToChannel(...)` and
  `RequestToChannel(...)` so destination-bearing send/request names align with
  `SendToSpot(...)`, `RequestToSpot(...)`, and `RequestToRouter(...)`.
- Actor location and stream session binding are independent. A bound stream
  session is not required for an actor to join a user Spot.

## Byte HWM And Monitoring ABI v2

An HWM limits accounted bytes computed by Core, not the number of queued
messages. Its public type is `ulong`, which preserves the full range of Core's
`uint64_t`. Zero means unlimited, and the manual default is `4_096_000 bytes`.
The binding passes an exact eight-byte value to Core. It does not provide the
former `int` overload, an alias, or a count-unit adapter.

```csharp
public interface IContextOptions
{
    ulong AutoHwmMessageUnitBytes { get; set; } // Zero selects the socket-type planning-unit default.
}

public partial class CommonSocketOptions
{
    public ulong SendHighWaterMark { get; set; }    // Outbound accounted-byte limit.
    public ulong ReceiveHighWaterMark { get; set; } // Inbound accounted-byte limit.
}
```

`MonitorStatus` exposes the same fields as native `zlink_monitor_status_t` ABI
version 2. Planned, applied, and deferred HWMs and in-flight usage are `ulong`
byte values. A deferred value is meaningful only when its matching
`AutoHwmDeferredSendHighWaterMarkValid` or
`AutoHwmDeferredReceiveHighWaterMarkValid` property is `true`. Pending messages
remain count diagnostics named `SndPendingMsgs` and `RcvPendingMsgs`; byte fields
do not reuse those names. A snapshot whose `AbiVersion` is not `2` or whose
`StructSize` differs from the binding layout causes `NotSupportedException`.
The former 32-bit monitoring layout is not accepted.

Request/reply APIs do not take an HWM argument. Core owns backpressure and
completion handling, while the binding preserves the existing request/reply
lifetime and ownership contract.

## Error And Validation Policy

- Fixed-size native boundary values are validated before calling core.
- Invalid routing ids, actor ids, endpoints, channel names, and topics raise
  .NET argument/config exceptions before truncation can occur.
- Submit, request, recv, handler, close, bind, connect, and config errors map
  to typed zlink exceptions.
- Public constructors on typed zlink exceptions must reject the success value
  `Ok`. The `Ok` enum member stays as the native result mirror, but public
  constructors accept only failure codes. Constructors that also accept native
  errno are for runtime-internal conversion and are not public surface.
- No-data and temporary backpressure are not reported as generic exceptions.
- Public APIs must not require callers to inspect native errno directly.

## Performance Policy

- Hot paths must not use reflection, dynamic invocation, repeated boxing,
  avoidable allocation, avoidable buffer copies, hidden sleeps, busy waits,
  thread joins, or broad locks.
- Native interop constructs managed `Message`, `Received`, and `TopicMessage`
  values directly from the core part substrate. Public caller-owned
  `Received` buffers are created through `Received.Create()`.
- Request progress is shared per handle where possible; do not create one
  polling thread or timer per request.
- Perf, samples, and framework adapters must use public contracts and
  construction entrypoints only.

## Implementation Checklist

Before declaring the .NET binding aligned:

- `Contracts/` exposes all public behavior needed by users and framework
  adapters.
- `Runtime/` implements those contracts without adding hidden user-facing API.
- Concrete value types remain concrete.
- Default construction paths are documented and tested.
- Public static facades, extension helpers, and builder convenience methods are
  discoverable from `Contracts/`.
- Recv/sub APIs use the caller-provided storage shape.
- Service control/admission receive exceptions are documented where they differ
  from data-plane caller-provided storage.
- Perf semantics match `bindings/c/perf`; private runtime shortcuts are not
  used to change measurement meaning.
- `Contracts/` public signatures do not expose `Runtime/Native/`, raw handles,
  native struct mirrors, request progress types, or runtime implementation
  classes. Internal delegation from static facades to runtime code is allowed.
- Runtime classes do not become a second contract surface.
- Framework adapters call public binding APIs directly.
- No old aliases, duplicate operation-start names, or deprecated wrappers are
  kept only for compatibility.

Required verification after .NET binding changes. Run these commands from
`bindings/dotnet/`:

- Run `dotnet test Zlink.sln` or the repository's current .NET binding test
  solution.
- Run `./tests/run_tests.sh`.
- Run `./samples/run_samples.sh` when public examples or construction paths
  changed.
- Run `./perf/run_benchmarks.sh` and `./perf/run_benchmarks_multi.sh` as smoke
  gates when hot path, receive, send, request, poller, timer, or service
  behavior changed.
- Search framework adapters, samples, perf, and tests for reflection,
  `NonPublic`, `InternalsVisibleTo`, `Runtime.Native`, raw handle usage, or
  direct request pump access.

## Actor And Spot Route Results

`.NET` exposes route lookup results through public contract records.

- `ActorRoute` preserves the resolved `ActorRef`, `Actor.NodeRid`,
  `CurrentSpotRid`, and `CurrentSpotKind`.
- `SpotRoute` preserves `SpotRid`, `OwnerNodeRid`, and `SpotKind`.
- `SpotKind` distinguishes Entry Spot from user Spot. Invalid kind is not a
  successful route result.
- `SpotNodeSpotEntry` and `SpotNodeActorEntry` expose the same Spot kind/current
  Spot fields as the core snapshots.

The binding exposes `ISpotNode.SendToActor(ActorRef)` and
`ISpotNode.RequestToActor(ActorRef)` for resolved Actor refs. `SendToActor`
consumes one or more message parts on successful submit and completes when the Actor owner
mailbox accepts the handoff. `RequestToActor` consumes request parts on
successful submit and delivers the Actor handler reply parts through the task
or callback. The binding must not reintroduce the removed Discovery route
table or resolver APIs as compatibility helpers.
