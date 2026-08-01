[Spec Index](https://kairos-code-dev.github.io/zlink/en/spec/) · [Bindings Policy](../README.en.md)

# Node / TypeScript Binding Implementation Blueprint

This document defines the expected Node/TypeScript library shape. It is not an
exhaustive list of every class or type member. The concrete public contract is
the package root export declared by `bindings/node/src/index.ts`,
`package.json` exports, and the generated `.d.ts` surface.

A Node/TypeScript implementation is aligned when the source package tree,
package exports, `.d.ts` types, tests, samples, perf runners, and runtime
behavior follow this blueprint and map stable `core/include/zlink.h`
capabilities into TypeScript-idiomatic APIs.

This README describes the completed Node/TypeScript binding shape after it is
aligned to the shared policy in `../README.md`, and it is also the guide for
the Node refactoring work. During the refactor, use this document to decide
where each public contract, runtime implementation, native bridge helper,
test, sample, and perf import belongs. Once the Node binding is declared
aligned, generated declarations, package exports, tests, samples, perf, and
runtime behavior must match this document.

The Node refactor is a breaking cleanup. Do not keep compatibility shims,
deprecated wrappers, duplicate construction paths, or runtime re-export aliases
only to preserve the pre-refactor public surface.

This binding follows the shared bindings architecture map with TypeScript
naming: lower-case `contracts` and `runtime` source folders, plus package
exports that decide what is public. Do not copy capitalized .NET or C++ folder
names into the Node package.

Node follows the [.NET design shape](../dotnet/README.en.md) after alignment. Native-backed resource
behavior is described by public contract interfaces and types under
`src/zlink/contracts`; native-backed runtime implementations live under
`src/zlink/runtime` and are obtained through package-root factory functions.
Concrete value classes, DTO-like objects, enums, literal unions, results, and
errors stay in the contract source.

The first code a reviewer reads should be the public contract under
`src/zlink/contracts`, in the same way the .NET binding starts from
`Contracts/`. Runtime files must implement those contracts; they must not be
the place where new user-facing behavior is discovered.

Keep TypeScript and Node conventions while using the same architecture map.
Use lower-case folders, camelCase methods, PascalCase public types, structural
interfaces where they fit TypeScript, plain objects for small DTO-like results
when that is clearer, and package-root exports as the consumer surface. Do not
copy C# interface prefixes, namespace casing, or file names literally when a
TypeScript idiom is clearer.

## Public Contract Source

- Public contract projection: `bindings/node/src/index.ts`, generated `.d.ts`,
  and `package.json` exports.
- Contract source: `bindings/node/src/zlink/contracts/`.
- Package projection: symbols exported from the package entrypoint and
  declared in the published TypeScript definitions.
- Internal implementation: native addon modules, private source modules,
  N-API handles, callback trampolines, request progress helpers, converters,
  and raw part-loop helpers.
- Package boundary: `package.json` exports should expose only documented
  public entrypoints.
- Documentation role: this README defines shape and semantic coverage.
  package entrypoint and declarations own the exact public member list.

Deep imports into source files or native bridge modules are not public API.

## Repository Layout

Use these paths consistently when changing the Node/TypeScript binding.

- Public entrypoint: `bindings/node/src/index.ts`.
- Contract source: `bindings/node/src/zlink/contracts/`.
- Runtime implementation: `bindings/node/src/zlink/runtime/`.
- Native bridge/artifacts: `bindings/node/src/zlink/runtime/native/`,
  `bindings/node/native/`, `bindings/node/prebuilds/`, and generated runtime
  loading code.
- Generated output: `bindings/node/dist/`, not source contract.
- Codec packages: not provided. Node bindings keep only raw `Message` and byte
  payload APIs.
- Tests: `bindings/node/tests/`.
- Samples: `bindings/node/samples/`.
- Perf: `bindings/node/perf/`.

`package.json` exports and generated `.d.ts` files must agree with the public
entrypoint. Do not document or test deep source imports as public API.
`index.ts`, published `.d.ts` files, and `package.json` exports are the
TypeScript package projection of the contract. Do not expose deep source paths
as public API unless they are deliberately listed in `package.json` exports.
The following tree is the aligned implementation structure. Use lower-case
source directory names. Do not create `src/zlink/Contracts` or
`src/zlink/Runtime`; those names can be mistaken for public deep-import
surfaces. `src/zlink/contracts` owns public TypeScript types, classes,
builders, enums, errors, and factory return contracts. The package entrypoint
or a runtime factory module owns factory implementation. `src/zlink/runtime`
owns native-backed implementations, native addon calls, handle owners, callback
trampolines, request progress helpers, marshalling, and platform loading.

File granularity follows the common policy in `../README.md`: keep one file
per independent public concept or tight operation/model group. Very small type
aliases, callback types, enum-only files, or pass-through helper modules should
be merged into the nearby contract file when that makes the public shape easier
to read.

```text
bindings/node/
+-- src/
|   +-- index.ts
|   +-- zlink/
|   |   +-- contracts/
|   |   |   +-- core/
|   |   |   |   +-- context.ts
|   |   |   |   +-- zlink.ts
|   |   |   |   +-- routing_id.ts
|   |   |   +-- messaging/
|   |   |   |   +-- message.ts
|   |   |   |   +-- received.ts
|   |   |   |   +-- topic_message.ts
|   |   |   |   +-- subscription_event.ts
|   |   |   +-- sockets/
|   |   |   |   +-- socket.ts
|   |   |   |   +-- pair_socket.ts
|   |   |   |   +-- dealer_socket.ts
|   |   |   |   +-- router_socket.ts
|   |   |   |   +-- pubsub_sockets.ts
|   |   |   |   +-- stream_socket.ts
|   |   |   |   +-- socket_options.ts
|   |   |   |   +-- socket_operations.ts
|   |   |   +-- eventing/
|   |   |   |   +-- monitor.ts
|   |   |   |   +-- poller.ts
|   |   |   |   +-- timer.ts
|   |   |   +-- service/
|   |   |   |   +-- spot/
|   |   |   |   |   +-- spot_node.ts
|   |   |   |   |   +-- spot.ts
|   |   |   |   |   +-- actor.ts
|   |   |   |   |   +-- spot_operations.ts
|   |   |   |   |   +-- spot_models.ts
|   |   |   +-- errors/
|   |   |   |   +-- errors.ts
|   |   |   |   +-- results.ts
|   |   +-- runtime/
|   |   |   +-- core/
|   |   |   |   +-- context.ts
|   |   |   |   +-- context_options.ts
|   |   |   |   +-- runtime_info.ts
|   |   |   +-- handles/
|   |   |   |   +-- native_handle.ts
|   |   |   |   +-- lifetime.ts
|   |   |   +-- messaging/
|   |   |   |   +-- message_materializer.ts
|   |   |   |   +-- request_progress.ts
|   |   |   +-- buffers/
|   |   |   |   +-- message_conversion.ts
|   |   |   |   +-- buffer_policy.ts
|   |   |   +-- sockets/
|   |   |   |   +-- socket_base.ts
|   |   |   |   +-- socket_options.ts
|   |   |   |   +-- socket_operations.ts
|   |   |   |   +-- pair_socket.ts
|   |   |   |   +-- dealer_socket.ts
|   |   |   |   +-- router_socket.ts
|   |   |   |   +-- pub_socket.ts
|   |   |   |   +-- sub_socket.ts
|   |   |   |   +-- xpub_socket.ts
|   |   |   |   +-- xsub_socket.ts
|   |   |   |   +-- stream_socket.ts
|   |   |   +-- eventing/
|   |   |   |   +-- monitor_socket.ts
|   |   |   |   +-- poller.ts
|   |   |   |   +-- poll_events.ts
|   |   |   |   +-- timer.ts
|   |   |   +-- options/
|   |   |   |   +-- option_mapping.ts
|   |   |   |   +-- validation.ts
|   |   |   +-- service/
|   |   |   |   +-- spot/
|   |   |   |   |   +-- spot_node.ts
|   |   |   |   |   +-- spot.ts
|   |   |   |   |   +-- actor.ts
|   |   |   |   |   +-- spot_operations.ts
|   |   |   +-- errors/
|   |   |   |   +-- native_errors.ts
|   |   |   +-- native/
|   |   |   |   +-- native.ts
|   |   |   +-- internal/
|   |   |   |   +-- request_pump.ts
|   |   |   |   +-- service_mapping.ts
+-- native/
+-- tests/
+-- samples/
+-- perf/
+-- prebuilds/
+-- dist/
```

The package root export must be the consumer entrypoint. Tests, samples, and
perf must import from that entrypoint or another documented package export,
not from `src/zlink/runtime`, native addon modules, or generated private files.
If a symbol appears in the package root or generated `.d.ts`, reviewers must
be able to point to its contract owner under `src/zlink/contracts` or the
package root entrypoint.

## API Change Workflow

When mapping a new core capability:

1. Add the public symbol to the correct contract source category.
2. Update the package entrypoint, declaration surface, and `package.json`
   projection.
3. Keep native addon calls, N-API handles, and request progress helpers behind
   private modules.
4. Choose a class, interface, type alias, literal union, or plain object shape
   according to normal TypeScript usage.
5. Add runtime tests and type-surface tests against the package entrypoint.
6. Update samples and perf only through public imports.
7. Verify generated `dist` and `.d.ts` output do not expose private bridge
   modules.

When refactoring existing code to this shape:

1. Move public behavior declarations to `src/zlink/contracts/<category>/`.
2. Move native-backed implementations to `src/zlink/runtime/<category>/`.
3. Keep native addon loading and N-API calls under `src/zlink/runtime/native/`.
4. Replace direct runtime construction in public code with package-root
   factories or contract methods.
5. Remove compatibility exports that expose runtime modules as public API.
6. Remove deprecated wrappers, duplicate overload families, and old naming
   aliases instead of preserving them as shims.
7. Update tests, samples, and perf to import from the package root only.
8. Regenerate declarations and verify that `dist/index.d.ts` contains the
   contract surface, not runtime implementation modules.

The refactor is complete only when the old Node-specific shortcuts below are
removed. These items are not optional compatibility layers.

- `src/zlink/contracts` must not re-export runtime handle modules.
- Contract files must not import runtime resource classes to describe public
  service models.
- A public runtime aggregate such as `runtime/handles/canonical.ts` must not
  remain the source of public resource behavior. Split those declarations into
  named contract files and resource-named runtime implementation files.
- `src/index.ts` must export package contract names and factories, not runtime
  implementation modules.
- `package.json` must not expose runtime, native, generated, or private source
  subpaths.
- Generated declarations must not mention runtime implementation module paths
  as public types.

For handoff work, the short task statement should be enough: refactor the Node
binding according to this README and `../README.md`, use the .NET design shape,
preserve TypeScript naming style, remove compatibility shims, and pass the
verification gates in this document.

## Library Shape

The binding should feel like a TypeScript package with a native backend.

- Native-backed resource behavior contracts are public TypeScript interfaces
  under `src/zlink/contracts`.
- Native-backed runtime implementations live under `src/zlink/runtime`.
  They are not package exports and are not construction entrypoints.
- The public contract files must be readable without opening runtime files.
  A reviewer should be able to understand callable methods, return values,
  lifecycle, error behavior, and builder shape from `contracts/`.
- Resource contracts expose `close()` or equivalent lifecycle methods.
- Values such as message, routing id, received metadata, topic message,
  snapshots, options, enums, literal unions, and errors stay concrete or
  structural according to normal TypeScript practice.
- Operation builders use public contract interfaces because they hide staged
  native request state and multipart accumulation.
- Native addon handles, raw pointers, callback userdata, request pumps, and
  part-loop sequencing are never exposed.

Do not introduce interfaces for pure DTO/value objects only for symmetry.
`Message`, `RoutingId`, `Received`, `TopicMessage`, route results, snapshots,
option objects, enums, literal unions, and errors remain concrete or structural
public values.

Define public TypeScript interfaces for these native-backed resources and
roles before writing or exposing runtime classes:

- `Context`.
- Socket roles: common socket behavior, `PairSocket`, `DealerSocket`,
  `RouterSocket`, `PubSocket`, `SubSocket`, `XPubSocket`, `XSubSocket`, and
  `StreamSocket`.
- Eventing roles: `MonitorSocket`, `Poller`, poll event source, `Timer`,
  `Stopwatch`, and `AtomicCounter` when those resources are present.
  `Spot`, and `Actor`.
- Operation builders: send, routed send, request, reply, publish,
  channel send/request, SPOT send/request/reply, actor create, actor join, and
  actor join reply builders.
- Callback roles: stream packet handlers, monitor handlers, poll handlers,
  SPOT dispatch handlers, route handlers, and admission handlers.

The runtime class that implements one of these roles may have a private or
unexported name, but the package-root factory and generated declarations must
use the public contract interface name.

Do not rely on undocumented deep import paths to give perf or samples faster
access to native objects.

## Contract / Runtime Placement Rules

- Exported TypeScript classes, interfaces, type aliases, error types, and
  builder contracts belong in `src/zlink/contracts` or the package entrypoint.
- Exported package functions, static helper types, convenience method
  contracts, and builder helper contracts belong in the contract source when
  callers can use them directly.
- Factory return types and callable factory signatures belong to the public
  contract. Factory implementation belongs in the package entrypoint or a
  runtime factory module so contract files do not import runtime implementations.
- JavaScript runtime implementations, native handle owners, request pumps,
  callback adapters, and part-loop helpers belong in `src/zlink/runtime`.
- N-API bindings, native addon handles, marshalling helpers, and platform
  loading code belong in `src/zlink/runtime/native`.
- Package exports and published `.d.ts` files must project the contract source,
  not expose runtime modules.
- Runtime concrete classes are construction targets behind package-root
  factories; callers should not import runtime modules directly.
- Do not export `src/zlink/runtime/*` from `src/zlink/contracts` or
  `src/index.ts`. `src/index.ts` may import runtime modules only to wire
  package-root factories. A runtime implementation type may satisfy a public
  contract, but the exported type name should come from the contract source.
- Package-root factories must declare contract return types explicitly. For
  example, `createContext(): Context` returns the public contract type even
  though it instantiates a native-backed runtime implementation.

## Contract Category Map

`src/zlink/contracts` is the source ownership map for the package entrypoint
and published TypeScript declarations.

- `core/`: context, context options, routing id, version/capability helpers, and
  utility contracts.
- `messaging/`: `Message`, received metadata, topic messages, subscription
  events, stream packet data, and builder payload helpers.
- `sockets/`: socket behavior, socket families, typed options, request/reply,
  and publish/subscribe surfaces.
- `eventing/`: monitor, monitor snapshot/event, poller, poll event, timer, and
  public poll helpers.
- `service/`: SPOT node, SPOT handle, topology models,
  actor refs, actor lifecycle, and operation builders.
- `errors/`: typed error classes or tagged error domains.
- Enum, flag, result, and literal-union types live in the category that defines
  their meaning. Do not create an `enums` folder just to group declarations by
  syntax.

## Contract File Layout

The contract source must use the same classification as the
[.NET binding blueprint](../dotnet/README.en.md), with TypeScript naming. Keep the same conceptual file grouping so a
developer who knows the .NET binding can find the same public concept in Node
quickly. The folder map is shared with .NET, but the names inside it should
stay idiomatic TypeScript.

- `core/`: `context.ts`, `zlink.ts`, `routing_id.ts`, and core option/value
  files.
- `messaging/`: `message.ts`, `received.ts`, `topic_message.ts`,
  `subscription_event.ts`, and common operation payload types.
- `sockets/`: socket interfaces, socket option types, send/request/reply
  builder contracts, stream packet handler contracts, and socket flags.
- `eventing/`: monitor, monitor event/status, poller, poll events, timer, and
  event handler contracts.
- `service/`: `spot/` subfolder for SPOT node, Spot, Actor, topology models,
  and service operation builders.
- `errors/`: public error classes, result domains, and error-code mapping.

Avoid a single aggregate `models.ts` or runtime-export barrel for public
resource behavior. Small DTO-like object shapes and literal unions may be
grouped with the contract that gives them meaning, but native-backed resources
and operation builders need named contract files.

## Runtime File Layout

Runtime source mirrors the runtime classification in the
[.NET binding blueprint](../dotnet/README.en.md) but contains only implementation.
Node runtime file names must use the same lower-case
TypeScript concept names as the contract tree. Do not use a `default_` file
prefix such as `default_context.ts` or `default_pair_socket.ts`. In this
package, resource implementation files under `src/zlink/runtime` are already
the native-backed implementation side of the contract/runtime split; their
file names should describe the resource or operation they implement.

- `core/`: `context.ts`, `context_options.ts`, and runtime helper functions
  such as version/capability wrappers.
- `handles/`: native handle owners, lifetime checks, close/dispose state, and
  reference tracking.
- `messaging/`: message materialization, request progress, request execution,
  and multipart progress helpers.
- `buffers/`: message conversion, buffer ownership, copy/borrow policy, and
  any pooled or pinned storage helpers.
- `sockets/`: `socket_base.ts`, `socket_options.ts`,
  `socket_operations.ts`, and one implementation file per socket family:
  `pair_socket.ts`, `dealer_socket.ts`, `router_socket.ts`, `pub_socket.ts`,
  `sub_socket.ts`, `xpub_socket.ts`, `xsub_socket.ts`, and
  `stream_socket.ts`.
- `eventing/`: `monitor_socket.ts`, `poller.ts`, `poll_events.ts`,
  `timer.ts`, and related event materialization helpers.
- `options/`: option validation and native option id/value mapping shared by
  context, sockets, and services.
- `service/`: SPOT node, Spot, Actor, topology, and service operation
  implementations. Use a `spot/` subfolder when the implementation is large
  enough.
- `errors/`: native error translation and validation helpers.
- `native/`: native addon loading, platform lookup, and N-API binding surface.
- `internal/`: private glue that does not fit a standard .NET runtime
  classification. It must stay small. Do not put handle ownership, buffer
  policy, option mapping, native declarations, or public resource behavior here
  when a standard runtime category exists.

Runtime files may import contract types, but contract files must not import
runtime files. The package root may instantiate native-backed runtime
implementations in factories, but it must export contract names, not runtime
implementation modules.

Category entry files should be `index.ts` barrels. During an unfinished
refactor, existing category-name files such as `runtime/sockets/sockets.ts`,
`runtime/service/service.ts`, and `runtime/eventing/eventing.ts` may remain
only if they are short barrels. An aligned final tree should use `index.ts`
barrels or no category barrel at all. Category entry files may re-export nearby
implementation files and define factory wiring that stays private to runtime,
but they must not contain native-backed resource class bodies, operation
builders, or marshalling logic. If a reviewer must read a category entry file
to understand how `RouterSocket`, `SpotNode`, or `Poller` works, the file
split is not aligned.

Runtime implementation files should be named after the resource or operation
they implement, not after the fact that they are native-backed
implementations. Use `router_socket.ts`, `spot_node.ts`, `poller.ts`, and
`timer.ts`; do not use
`default_router_socket.ts`,
`default_spot_node.ts`, `default_poller.ts`, or similar names.

Shared helpers must not become a second public implementation aggregate.
`runtime/internal/*` may own narrow private glue such as request pumping that
crosses several runtime categories, but it must not own the behavior of public
resources and must not hide standard .NET runtime categories. Native handle
ownership belongs in `runtime/handles`, buffer conversion belongs in
`runtime/buffers`, option mapping belongs in `runtime/options`, native addon
declarations belong in `runtime/native`, and public resource behavior belongs
in the resource's runtime file, for example `sockets/router_socket.ts` or
`service/spot/spot_node.ts`.

Shared helper files under a category follow the same rule. A file such as
`runtime/sockets/socket_common.ts` may hold narrow socket helper types or
private base utilities, but it must not contain several unrelated concerns at
once. If it contains operation builders, monitor socket behavior, routing
helpers, marshalling helpers, and concrete resource behavior together, it has
become a hidden aggregate and must be split into `socket_base.ts`,
`socket_options.ts`, `socket_operations.ts`, and smaller internal helpers.

The following shapes are explicit alignment failures:

- `SpotNode`, `Spot`, and `Actor` implementations in one file,
  even if it also re-exports those implementations.
- `runtime/eventing/eventing.ts` contains monitor socket, poll events, poller,
  timer, stopwatch, and counter implementations in one file, even if those
  types are all event-related.
- `runtime/core/context.ts` contains context, context options, and unrelated
  runtime helper implementation in one file.
- `runtime/core/runtime_info.ts` contains a copied implementation prelude or
  socket/service behavior only to reach helper functions.
- `runtime/sockets/socket_common.ts` owns operation builders, monitor socket
  behavior, route helpers, message conversion, and base socket behavior in one
  large file.
- `runtime/internal/*` owns public resource behavior instead of private helper
  mechanics.
- `runtime/internal/*` owns handle lifetime, buffer conversion, option mapping,
  native addon declarations, or error mapping that should be in the .NET
  standard runtime category.

## Construction Entry Points

Interfaces define behavior; construction is provided by package-root factories
and public contract methods.

- `createContext()` creates the native-backed context implementation.
- `Context.createPairSocket()`, `createDealerSocket()`,
  `createRouterSocket()`, `createPubSocket()`, `createSubSocket()`,
  `createXPubSocket()`, `createXSubSocket()`, and `createStreamSocket()`
  create native-backed socket implementations.
  `createSpotNode(...)` create service-layer implementations.
- `Spot` handles are obtained through `SpotNode.createSpot()`,
  `entrySpot()`, `getOrCreateSpot(...)`, or `spotLookup(...)`; direct `Spot`
  construction is not public.
- Actor handles are created through `SpotNode.createActor(...)`; direct Actor
  construction is not public.
- `createPoller()`, `createTimer()`, and `createTimer(spot)` create eventing
  resources.
- Package-root factory/helper functions such as version, capability, strerror,
  proxy, sleep, and multipart cleanup helpers are public contract functions.
  Native calls behind those functions stay in runtime modules.

Direct construction of native-backed runtime classes is not part of the aligned
contract. Factories are the stable creation surface.

## Function Naming Rules

Function names follow the shared binding meaning rules from `../README.md`,
but use TypeScript spelling.

- Use `camelCase` for methods and functions.
- Use the same canonical action names as other bindings after casing:
  `send`, `request`, `reply`, `publish`, `subscribe`, `unsubscribe`,
  `recv`, `recvRouted`, `receiveSubscriptionEvent`, `setSendReadyHandler`,
  `setPacketHandler`, `setDispatchHandler`, `getOrCreateSpot`,
  `sendToChannel`, `requestToChannel`, `sendToSpot`, and `requestToSpot`.
- Do not keep old aliases only for compatibility. If a pre-refactor name
  conflicts with the canonical meaning, remove it and expose the canonical
  TypeScript name.
- Do not use `on...` names for handler registration. Use `set...Handler`
  when the API stores or replaces the current handler.
- Do not create operation-start variants such as `sendNoWait`,
  `publishWithFlags`, or `requestAsync`. Keep one operation name and put
  flags, timeout, callback, and async submit choices on the builder.

## Canonical Interface Rules

- Data-plane `recv`, routed recv, `subscribe`, and subscription-event receive
  fill caller-provided `Received`, `TopicMessage`, or `SubscriptionEvent`
  objects and return `boolean`.
- Send, routed send, publish, request, reply, SPOT operations, and Actor
  location/session operations return fluent builders.
- Builder start methods take only the target identity, topic, channel, routing
  id, or request sequence. Payload, flags, timeout, callback, and async submit
  choices are builder steps.
- SPOT channel-targeted operations use `sendToChannel(...)` and
  `requestToChannel(...)`. SPOT topic publish stays `publish(topic)`.
- Do not add single-payload shortcut overloads with the same name as an
  operation start method. `send(message)`, `send(routingId, message)`,
  `publish(topic, message)`, `sendToChannel(channel, message)`, and
  `sendToSpot(..., message)` are not public contract members; callers use
  `send(...).message(message).submit()`.
- Multipart payload is accumulated by repeated `message(...)` calls.
  `messages(...)` convenience is allowed when it delegates to the same builder
  contract and is declared in the contract source.
- Dealer sockets must not expose protocol envelope helpers such as
  `requestFrame(...)` or `reply(requestToken, parts)`. A dealer can start a
  request through `request()`, but it cannot reply to an arbitrary token
  because it has no API-level peer routing id.
- Node `Buffer` / `Uint8Array` payload inputs must be copied into
  message-owned native storage before the native queue can outlive the call.
  Do not expose or use borrowed Buffer send helpers such as
  `socketSendBorrowedNoWaitResult`.
- Message payload factories use `Message.from(...)`. The public TypeScript
  contract should not require callers to use `new Message(...)` for payload
  construction.
- Operation-start naming follows the Function Naming Rules above. Terminal
  builder methods keep `submit(...)` even for Promise-returning operations.
  Do not add a separate `submitAsync` terminator.
- The MeshNode Logical Multicast publisher also provides `publishAsync(...)`
  because one blocking Core publish must run outside the Node.js event loop.
  This name is limited to that publisher and does not extend async suffixes to
  other binding operations. The binding copies payload and metadata into owned
  storage before queuing the worker. An `AbortSignal` can cancel only before
  the Core call starts. An abort after that point does not replace the started
  publish's normal submit result and detail. Programming or system failures
  remain exceptional. This operation adds no timeout option and uses the
  MeshNode Core send timeout.
- `publishAsync(...)` returns `Promise<MeshPublishResult>`. `Ok`,
  `Backpressured`, `NotFound`, `NotConnected`, `Terminated`, and `NotAdmitted`
  are normal submit outcomes and preserve the detail returned by Core. In
  particular, a partially admitted `Backpressured` result does not discard its
  non-zero detail. `InvalidArgument`, `InvalidHandle`, `InvalidState`,
  `NotSupported`, `ThreadViolation`, `OutOfMemory`, `SeqExhausted`, and
  `InternalError` are programming or system errors and throw `SubmitError`.
- When `close()` follows a queued `publishAsync(...)`, new publishes are
  rejected immediately and `close()` does not block the Node.js event loop. A
  queued or started operation retains the native publisher handle. The binding
  releases that handle after the last operation's Core call and Promise
  completion processing finish.
- `sendActorBoundSession(...)` on a MeshNode requires a non-zero
  `expectedBindingGeneration`. A call for an old generation is not redirected
  to a replacement session, and zero does not select the current binding; the
  binding preserves Core's `InvalidArgument` result.

## Public Entry Shape

The package entrypoint should group the API around domain concepts.

- Core: context, version/capability helpers, options, and utility functions.
- Messaging: `Message`, routing id values, received metadata, topic messages,
  subscription events, and stream packet data.
- Sockets: pair, dealer, router, pub, sub, xpub, xsub, stream, typed options,
  callbacks, request/reply, publish/subscribe, and stream packet APIs.
- Eventing: monitor, monitor snapshot/event, poller, poll event, and timer.
- Service: SPOT node, SPOT handle, topology snapshots,
  actor refs, actor lifecycle, and operation builders.
- Errors: typed error classes or tagged error objects preserving core result
  domains.

## 64-bit Byte HWM And Monitoring Contract

HWM values and the Auto HWM planning unit use `bigint` so every Core `uint64_t`
byte value remains lossless. The public API does not also accept `number` or
change representations at JavaScript's safe-integer boundary. `0n` means an
unlimited HWM, and the manual default is `4_096_000n` bytes. Negative values or
values above `2n ** 64n - 1n` throw `RangeError`; `number` and other types throw
`TypeError`.

```ts
interface ContextOptions {
  autoHwmMsgUnitBytes: bigint; // Planning-unit bytes; 0n selects the socket default.
}

interface CommonSocketOptions {
  sendHwm: bigint; // Directional send-pipe byte HWM; 0n means unlimited.
  recvHwm: bigint; // Directional receive-pipe byte HWM; 0n means unlimited.
}
```

Monitor snapshots project Core monitoring ABI v2. Planned, applied, deferred,
and in-flight HWM values include `Bytes` in their names and use `bigint`.
Separate booleans identify whether deferred values are valid. Pending-message
and profile-slot values remain count diagnostics and do not share names with
byte fields. Old count-oriented names such as `autoHwmAppliedSndHwm` are not
retained as aliases.

## Required Capability Coverage

The public entrypoint must cover these stable user-facing capabilities when
the binding is aligned to the shared .NET-standard policy.

- Context lifecycle, options, shutdown, auto-HWM recalculation, version,
  capability, and strerror.
- Message ownership, multipart payloads, routing ids, received metadata, topic
  messages, subscription events, and stream packet callbacks.
- All socket families and their typed options.
- Monitor, poller, timer, and readiness semantics.
- SPOT node, SPOT handle, topology snapshots, actors, and
  stream actor binding.

The binding may expose synchronous or asynchronous forms where appropriate,
but it must not change the meaning of core operations.

## Spot Get-Or-Create

Node exposes `SpotNode.getOrCreateSpot(spotRid)`. It maps directly to
`zlink_spot_node_spot_get_or_new(...)`; it must not be implemented by composing
`spotLookup` and `createSpot`.

The method returns `{ spot, created }`. The returned `Spot` is caller-owned and
must be closed normally. `created` is `true` only for the call that created the
logical spot.

## Receive And Subscribe Shape

- Data-plane receive and subscribe APIs must use caller-provided result objects
  for reusable storage.
- Nonblocking no-data returns `false` and is distinct from thrown errors.
- SPOT readable dispatch events are readiness notifications. Callers drain the
  matching receive API until no-data.
- Native-origin buffers should become owned `Message` objects without extra
  JavaScript buffer concatenation.
- Service control/admission receive paths such as Actor join request receive may
  use nullable, `undefined`, or tagged result-return forms when they are clearer
  than reusable data-plane storage. They must still distinguish no-data from
  thrown hard receive errors.

## Error And Validation Policy

- Validate fixed-size boundary strings and ids before calling the native addon.
- Do not silently truncate routing ids, actor ids, endpoints, channel names, or
  topics.
- Preserve submit, request, recv, handler, close, bind, connect, and config
  error domains.
- Public errors should carry enough structured data for callers to branch
  without parsing error text.

## Performance Policy

- Hot paths must not use reflection-style property walking, dynamic dispatch by
  string lookup, avoidable allocation, avoidable `Buffer` copies, hidden
  sleeps, busy waits, broad locks, or worker-thread joins.
- Request progress should be shared per native handle while requests are
  outstanding.
- Poll result materialization should use fixed mapping tables, not per-event
  reflective enum scanning.
- Perf, samples, and tests import only the public package entrypoint.

## Implementation Checklist

- `package.json` exports do not expose private modules.
- Published `.d.ts` files describe the public contract.
- Native addon details do not leak through public types.
- Exported helper functions and builder convenience methods are declared in
  the contract source, not only in runtime helpers.
- Receive/subscription semantics match the shared binding policy.
- Service control/admission receive exceptions are documented where they differ
  from data-plane caller-provided storage.
- Perf meaning matches `bindings/c/perf`.
- `src/zlink/contracts` has no import or export dependency on
  `src/zlink/runtime`.
- `src/index.ts` imports runtime modules only for factory wiring and does not
  export runtime modules or runtime implementation type names.
- Category entry files are short barrels only. They do not contain resource
  class bodies, operation builders, marshalling logic, or callback bridges.
- Runtime implementation files are named after resources or operations and do
  not use `default_` filename prefixes.
- Explicit alignment failure files listed in the Runtime File Layout section do
  not remain in that failed shape.
- Tests, samples, and perf do not use deep runtime imports.
- Native-backed resources are created through package-root factories or
  contract methods and are typed as contract interfaces.
- Each required native-backed resource, operation builder, and callback role
  listed in Library Shape has a public contract interface before its runtime
  implementation class is wired into factories.
- No old aliases, duplicate operation-start names, or deprecated wrappers are
  kept only for compatibility.

Required verification after the Node refactor. Run these commands from
`bindings/node/`:

- Run `npm run build`.
- Run `npm run typecheck`.
- Run `npm test`.
- Run `npm run samples` when public examples or construction paths changed.
- Run `npm run perf:single` and `npm run perf:multi` as smoke gates when hot
  path, receive, send, request, poller, timer, or service behavior changed.
- Inspect generated declarations and confirm the package root exposes contract
  types, not runtime implementation modules.
- Search the public surface for private imports. At minimum, check
  `src/zlink/contracts`, `tests`, `samples`, and `perf` for imports from
  `src/zlink/runtime`, `../runtime`, runtime handle aggregates, native addon
  modules, or generated private files. Check `src/index.ts` separately to
  confirm any runtime import is factory wiring only and does not appear in
  exported declarations.
- Confirm runtime file names do not use `default_` prefixes:
  `find src/zlink/runtime -type f -name 'default_*'` should print nothing.
- Confirm category entry files are short barrels, and the explicit alignment
  failure shapes listed in Runtime File Layout are gone.

## Actor And Spot Route Results

Node exposes Actor and Spot route lookup results through public JavaScript
objects and matching TypeScript declarations.

- `ActorRoute` preserves the resolved Actor ref, Actor node RID, current Spot
  RID, and current Spot kind.
- `SpotRoute` preserves Spot RID, owner node RID, and Spot kind.
- `SpotKind` distinguishes Entry Spot from user Spot. Invalid kind is not a
  successful route result.
- SpotNode snapshot entries expose the same Spot kind/current Spot fields as the
  core snapshots.

Node exposes `SpotNode.sendToActor` and `SpotNode.requestToActor` for resolved
Actor refs, using the language naming convention. The send operation consumes
one or more message parts on successful submit and completes when the Actor owner mailbox accepts the
handoff. The request operation consumes request parts on successful submit and
delivers the Actor handler reply parts. Node must not reintroduce the removed
Discovery route table or resolver APIs as compatibility helpers.
