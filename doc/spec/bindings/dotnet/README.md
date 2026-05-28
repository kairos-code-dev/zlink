[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# .NET Binding Implementation Blueprint

This document defines the expected .NET library shape. It is not an exhaustive
list of every interface member. The concrete public contract source is
`bindings/dotnet/src/Zlink/Contracts/`.

A .NET implementation is aligned when `Contracts/`, the default runtime
classes, tests, samples, perf runners, and package behavior follow this
blueprint and map the stable capabilities of `core/include/zlink.h` into
.NET-idiomatic APIs.

This binding follows the shared bindings architecture map with .NET naming:
`Contracts/<Category>` owns public contract source and `Runtime/<Category>`
owns implementation. Other bindings may use different casing or package names,
but this document is the .NET projection of the same map.

## Public Contract Source

- Public namespace: `Systems.Zlink`.
- Package identity: `Systems.Zlink`.
- Public contract: `bindings/dotnet/src/Zlink/Contracts/`.
- Default runtime implementation: `bindings/dotnet/src/Zlink/Runtime/`.
- Internal implementation: P/Invoke declarations, `SafeHandle` or native
  handle ownership, callback trampolines, request progress pumps, native model
  converters, and socket kernels.
- Documentation role: this README defines the library shape and review rules.
  `Contracts/` owns the exact public behavior surface.

Runtime implementation files must not define user-facing behavior that cannot
be understood through `Contracts/` or documented construction entrypoints.

## Repository Layout

Use these paths consistently when changing the .NET binding.

- Public contract: `bindings/dotnet/src/Zlink/Contracts/`.
- Runtime implementation: `bindings/dotnet/src/Zlink/Runtime/`.
- Native bridge/artifacts: `bindings/dotnet/src/Zlink/Runtime/Native/`,
  `bindings/dotnet/runtimes/`, and `bindings/dotnet/native/`.
- Codec extensions: `bindings/dotnet/src/Zlink.Codecs.*` and
  `bindings/dotnet/codecs/`.
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
The following tree is normative for implementation work. Files that define
public behavior must be placed under `Contracts/`; files that exist to call
native code, own handles, marshal structs, or run callback/request progress
logic must be placed under `Runtime/`, with native bridge code under
`Runtime/Native/`.

```text
bindings/dotnet/
+-- src/
|   +-- Zlink/
|   |   +-- Contracts/
|   |   |   +-- Core/
|   |   |   +-- Messaging/
|   |   |   +-- Sockets/
|   |   |   +-- Eventing/
|   |   |   +-- Service/
|   |   |   +-- Errors/
|   |   +-- Runtime/
|   |   |   +-- Core/
|   |   |   +-- Messaging/
|   |   |   +-- Sockets/
|   |   |   +-- Eventing/
|   |   |   +-- Service/
|   |   |   +-- Errors/
|   |   |   +-- Native/
|   +-- Zlink.Codecs.Json/
|   +-- Zlink.Codecs.MessagePack/
|   +-- Zlink.Codecs.Protobuf/
+-- codecs/
+-- tests/
+-- samples/
+-- perf/
+-- native/
+-- runtimes/
```

The `Contracts` and `Runtime` folder names are repository ownership boundaries.
They are not a license to expose `Systems.Zlink.Contracts` or
`Systems.Zlink.Runtime` as user-facing namespaces. Public construction can
return default concrete runtime classes such as `Context`, socket classes,
`SpotNode`, `Poller`, or `Timer`, but their observable behavior must be
specified by contracts in the matching category.

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
6. Update samples and perf only through public contracts/default constructors.
7. Verify framework adapters do not use reflection or `InternalsVisibleTo` to
   reach private binding members.

## Library Shape

The .NET binding uses a contract/runtime split.

- Behavior contracts are public `I*` interfaces in `Contracts/`.
  Operation builder contracts may use domain names such as `SendOperation` or
  `RequestOperation` when that is the package's established public shape.
- Default implementations are public sealed classes in `Runtime/` when direct
  construction is part of the package contract, such as `Context`,
  `DealerSocket`, `RouterSocket`, `SpotNode`, `Poller`, and `Timer`.
- Non-constructible abstract base classes may exist in `Runtime/` only as
  implementation support for those default classes. They are not construction
  entrypoints, and their public behavior must be covered by `Contracts/`
  interfaces or value types.
- DTO, value, result, option, enum, and exception types stay concrete. Use
  `record`, `sealed class`, `readonly struct`, or `enum` according to normal
  .NET usage.
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
- If a `Runtime/` class is directly constructible, its public behavior must
  still be described by `Contracts/`.

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
  builder absorb the variation. Terminal builder methods may use idiomatic
  names such as `SubmitAsync`.

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
- `Service/`: registry, discovery, SPOT node, SPOT handle, topology models,
  actor refs, actor lifecycle, and service-specific operation builders.
- `Errors/`: exception hierarchy and error-domain mapping.

Files inside each category follow the user-facing concept, not implementation
sequence. Common messaging operations are split as send, request, and reply;
service topology models are split between registry models, SPOT node models,
and shared topology enums. Request result and callback types belong with
messaging request contracts, not socket enum files. Received message kind
belongs with received-message metadata. SPOT node modes, socket snapshots, Spot
snapshots, and actor snapshots belong with SPOT node models.

SPOT remains a single handle contract through `ISpot`. Do not split it into
role interfaces unless callers actually need to accept those roles separately.
Use named callback delegates for SPOT callback registration so public
signatures describe the callback meaning without adding wrapper context
objects. Registration methods use `Set...Handler` names because the current
handler is stored or replaced; `On...` names are reserved for methods that are
called when an event happens. Declare those delegates next to `ISpot` because
they are only used by the SPOT handle contract. Lifecycle data records stay
with actor models. Actor operation contracts are split by join, management,
and session binding.

If a user or framework adapter needs a public API, it should be discoverable
from this folder without reading P/Invoke or runtime bridge code.

## Construction Entry Points

Interfaces define behavior; construction is provided by public factories.

- `Zlink.CreateContext()` creates the default context implementation.
- `IContext.CreatePairSocket()`, `CreateDealerSocket()`,
  `CreateRouterSocket()`, `CreatePubSocket()`, `CreateSubSocket()`,
  `CreateXPubSocket()`, `CreateXSubSocket()`, and `CreateStreamSocket()`
  create default socket implementations.
- `IContext.CreateRegistry()`, `CreateDiscovery(...)`, and
  `CreateSpotNode(...)` create service-layer implementations.
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
- Registry, discovery, SPOT node, SPOT handle, topology snapshots, actor refs,
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
  discovery/channel attachments, external pub ingress attachment, topology
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
- `AttachPubIngress(IPubSocket)` registers an external raw `PUB` as ingress;
  it is separate from `Spot.Publish(...)`.
- Actor location and stream session binding are independent. A bound stream
  session is not required for an actor to join a user Spot.

## Error And Validation Policy

- Fixed-size native boundary values are validated before calling core.
- Invalid routing ids, actor ids, endpoints, channel names, and topics raise
  .NET argument/config exceptions before truncation can occur.
- Submit, request, recv, handler, close, bind, connect, and config errors map
  to typed zlink exceptions.
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

## Actor And Spot Route Results

`.NET` exposes route lookup results through public contract records.

- `ActorRoute` preserves the resolved `ActorRef`, `Actor.NodeRid`,
  `CurrentSpotRid`, and `CurrentSpotKind`.
- `SpotRoute` preserves `SpotRid`, `OwnerNodeRid`, and `SpotKind`.
- `SpotKind` distinguishes Entry Spot from user Spot. Invalid kind is not a
  successful route result.
- `SpotNodeSpotEntry` and `SpotNodeActorEntry` expose the same Spot kind/current
  Spot fields as the core snapshots.

The binding must not add ROUTER-to-Actor or Actor-to-ROUTER direct messaging
methods. Framework code and applications resolve Actor or Spot routes through
`Discovery.ResolveActor()` and `Discovery.ResolveSpot()`, then use existing Spot
routed send/request APIs.

## SpotNode Router Channel Peers

`.NET` exposes router channel peer wiring on the public `SpotNode` and
`ISpotNode` surface: `ConnectRouterChannelPeer(string channelName, string endpoint)`,
`DisconnectRouterChannelPeer(string channelName, string endpoint)`,
`DisconnectRouterChannelPeerRid(string channelName, RoutingId peerRid)`, and
`AttachSpotRouteChannelDiscovery(string channelName, IDiscovery discovery)`.
The default runtime maps these methods directly to the core C APIs. Framework
code must call these public methods only; reflection, `NonPublic` lookup, and
extra `InternalsVisibleTo` access are not allowed for this integration.
