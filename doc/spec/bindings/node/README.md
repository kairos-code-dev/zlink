[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Node / TypeScript Binding Implementation Blueprint

This document defines the expected Node/TypeScript library shape. It is not an
exhaustive list of every class or type member. The concrete public contract is
the package root export declared by `bindings/node/src/index.ts`,
`package.json` exports, and the generated `.d.ts` surface.

A Node/TypeScript implementation is aligned when the source package tree,
package exports, `.d.ts` types, tests, samples, perf runners, and runtime
behavior follow this blueprint and map stable `core/include/zlink.h`
capabilities into TypeScript-idiomatic APIs.

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
- Codec extensions: `bindings/node/packages/`.
- Tests: `bindings/node/tests/`.
- Samples: `bindings/node/samples/`.
- Perf: `bindings/node/perf/`.

`package.json` exports and generated `.d.ts` files must agree with the public
entrypoint. Do not document or test deep source imports as public API.
`index.ts`, published `.d.ts` files, and `package.json` exports are the
TypeScript package projection of the contract. Do not expose deep source paths
as public API unless they are deliberately listed in `package.json` exports.
The following tree is the target implementation structure. Use lower-case
source directory names. Do not create `src/zlink/Contracts` or
`src/zlink/Runtime`; those names can be mistaken for public deep-import
surfaces. `src/zlink/contracts` owns public TypeScript types, classes,
builders, enums, and errors. `src/zlink/runtime` owns native addon calls,
handle owners, callback trampolines, request progress helpers, marshalling,
and platform loading.

```text
bindings/node/
+-- src/
|   +-- index.ts
|   +-- zlink/
|   |   +-- contracts/
|   |   |   +-- core/
|   |   |   +-- messaging/
|   |   |   +-- sockets/
|   |   |   +-- monitoring/
|   |   |   +-- service/
|   |   |   +-- errors/
|   |   |   +-- enums/
|   |   +-- runtime/
|   |   |   +-- core/
|   |   |   +-- messaging/
|   |   |   +-- sockets/
|   |   |   +-- monitoring/
|   |   |   +-- service/
|   |   |   +-- errors/
|   |   |   +-- enums/
|   |   |   +-- native/
+-- native/
+-- packages/
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
4. Choose a class, interface, type alias, or tagged object according to normal
   TypeScript usage.
5. Add runtime tests and type-surface tests against the package entrypoint.
6. Update samples and perf only through public imports.
7. Verify generated `dist` and `.d.ts` output do not expose private bridge
   modules.

## Library Shape

The binding should feel like a TypeScript package with a native backend.

- Public classes own resource lifetime and expose `close()` or equivalent
  lifecycle methods.
- Public TypeScript interfaces/types describe structural contracts where that
  helps callers, but runtime-only native state remains hidden.
- Values such as message, routing id, received metadata, topic message,
  snapshots, options, enums, and errors stay concrete or structural according
  to normal TypeScript practice.
- Operation builders are required for multipart send, publish, request, reply,
  SPOT, and actor operations.
- Native addon handles, raw pointers, callback userdata, request pumps, and
  part-loop sequencing are never exposed.

Do not rely on undocumented deep import paths to give perf or samples faster
access to native objects.

## Contract / Runtime Placement Rules

- Exported TypeScript classes, interfaces, type aliases, error types, and
  builder contracts belong in `src/zlink/contracts` or the package entrypoint.
- Exported package functions, static helpers, convenience methods, and builder
  helper functions belong in the contract source when callers can use them
  directly.
- JavaScript runtime implementations, native handle owners, request pumps,
  callback adapters, and part-loop helpers belong in `src/zlink/runtime`.
- N-API bindings, native addon handles, marshalling helpers, and platform
  loading code belong in `src/zlink/runtime/native`.
- Package exports and published `.d.ts` files must project the contract source,
  not expose runtime modules.
- If a runtime concrete class is exported for construction, its public behavior
  must still be described by the public contract.

## Contract Category Map

`src/zlink/contracts` is the source ownership map for the package entrypoint
and published TypeScript declarations when category separation is needed.

- `Core/`: context, context options, routing id, version/capability helpers, and
  utility contracts.
- `Messaging/`: `Message`, received metadata, topic messages, subscription
  events, stream packet data, and builder payload helpers.
- `Sockets/`: socket behavior, socket families, typed options, request/reply,
  and publish/subscribe surfaces.
- `Monitoring/`: monitor, monitor snapshot/event, poller, poll event, timer, and
  public poll helpers.
- `Service/`: registry, discovery, SPOT node, SPOT handle, topology models,
  actor refs, actor lifecycle, and operation builders.
- `Errors/`: typed error classes or tagged error domains.
- `Enums/`: public enum or literal-union domains shared across the binding.

## Canonical Interface Rules

- Data-plane `recv`, routed recv, `subscribe`, and subscription-event receive
  fill caller-provided `Received`, `TopicMessage`, or `SubscriptionEvent`
  objects and return `boolean`.
- Send, routed send, publish, request, reply, SPOT operations, and Actor
  location/session operations return fluent builders.
- Builder start methods take only the target identity, topic, channel, routing
  id, or request sequence. Payload, flags, timeout, callback, and async submit
  choices are builder steps.
- Multipart payload is accumulated by repeated `message(...)` calls.
  `messages(...)` convenience is allowed when it delegates to the same builder
  contract and is declared in the contract source.
- Do not add operation-start method families such as `sendNoWait`,
  `publishWithFlags`, or `requestAsync`; keep one operation name and let the
  builder absorb the variation. Terminal builder methods may use idiomatic
  names such as `submitAsync`.

## Public Entry Shape

The package entrypoint should group the API around domain concepts.

- Core: context, version/capability helpers, options, and utility functions.
- Messaging: `Message`, routing id values, received metadata, topic messages,
  subscription events, and stream packet data.
- Sockets: pair, dealer, router, pub, sub, xpub, xsub, stream, typed options,
  callbacks, request/reply, publish/subscribe, and stream packet APIs.
- Monitoring: monitor, monitor snapshot/event, poller, poll event, and timer.
- Service: registry, discovery, SPOT node, SPOT handle, topology snapshots,
  actor refs, actor lifecycle, and operation builders.
- Errors: typed error classes or tagged error objects preserving core result
  domains.

## Required Capability Coverage

The public entrypoint must cover stable user-facing core capabilities.

- Context lifecycle, options, shutdown, auto-HWM recalculation, version,
  capability, and strerror.
- Message ownership, multipart payloads, routing ids, received metadata, topic
  messages, subscription events, and stream packet callbacks.
- All socket families and their typed options.
- Monitor, poller, timer, and readiness semantics.
- Registry, discovery, SPOT node, SPOT handle, topology snapshots, actors, and
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

Node must not add ROUTER-to-Actor or Actor-to-ROUTER direct messaging methods.
Callers compose `resolveActor()` or `resolveSpot()` with the existing Spot
routed APIs.

## SpotNode Router Channel Peers

Node exposes router channel peer wiring on the public `SpotNode` object:
`connectRouterChannelPeer(channelName, endpoint)`,
`disconnectRouterChannelPeer(channelName, endpoint)`,
`disconnectRouterChannelPeerRid(channelName, peerRid)`, and
`attachSpotRouteChannelDiscovery(channelName, discovery)`. These methods call
the matching core C APIs through the native addon and use the existing Node
error mapping.
