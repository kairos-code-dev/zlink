# Node Bindings

Aligned Node bindings for `libzlink`.

## Canonical Raw API

- `new PairSocket(ctx)`, `new DealerSocket(ctx)`, `new RouterSocket(ctx)`,
  `new StreamSocket(ctx)`
- `new PubSocket(ctx)`, `new XPubSocket(ctx)`
- `new SubSocket(ctx)`, `new XSubSocket(ctx)`
- publisher sockets: `publish(topic, message|parts)`,
  `tryPublish(topic, message|parts)`
- message sockets: `send(message|parts)`, `trySend(message|parts)`,
  `receive()`, `tryReceive()`, `recvHandler(handler)`
- routed sockets: `send(routingId, message|parts)`,
  `trySend(routingId, message|parts)`, `receive()`, `tryReceive()`,
  `recvHandler(handler)`
- subscriber sockets: `setSubscription(topicOrPattern)`,
  `unsetSubscription(topicOrPattern)`, `subscribe()`, `trySubscribe()`,
  `subscribeHandler(handler)`
- `XPubSocket`: `receiveSubscriptionEvent()`,
  `tryReceiveSubscriptionEvent()`, `setVerbose()`, `setVerboser()`,
  `setNoDrop()`
- monitors: `recv()`, `tryRecv()`

`Message.copyOf()` copies payload ownership into the message.
`Message.wrap()` keeps the caller-owned buffer as the payload source.
Canonical raw sockets intentionally hide opposite-direction methods, so
`PubSocket` does not expose `send()` or `receive()`, `SubSocket` does not
expose `send()`, and `StreamSocket` does not expose active stream helpers on
the canonical path.

Canonical receive results are domain objects:

- `Received`: `{ routingId: Buffer | null, parts: Message[] }`
- `Subscribed`: `{ routingId: Buffer | null, topic: string, parts: Message[] }`
- `SubscriptionEvent`:
  `{ routingId: Buffer | null, topic: string, subscribed: boolean }`
- `SendResult`: `Sent`, `Backpressured`, `NotReady`

## Compatibility Socket

- `new Socket(ctx, SocketType.X)` remains as a deprecated compatibility path
- legacy `recv(size, flags)` and raw `streamAttach` / `streamDetach` /
  `streamPeerRoutingId` / `streamSend` stay on compat `Socket` only
- legacy flags-based send/recv and raw socket option APIs stay on compat
  `Socket` only

Not part of the canonical or sample contract:
length-prefixed stream framing such as `len32be`.

Not yet part of the canonical raw surface:
raw socket TLS convenience helpers, raw publish(topic, payload), raw socket
unbind/disconnect helpers.

## Service Surface

- `new Discovery(ctx, serviceType, serviceName)`
- `new Registry(ctx)` + `registry.bind(pubEndpoint, routerEndpoint)`
- `new RegistryQueryClient(ctx)`
- `new Spot(ctx)`
- `new SpotNode(ctx)`

`Spot` follows the same multipart/domain-return direction:
`publish()` / `tryPublish()`, `setSubscription()` / `unsetSubscription()`,
`subscribe()` / `trySubscribe()`, `subscribeHandler()`.

`Receiver` is removed from the aligned public API.
`Discovery` requires a non-empty `serviceName`.
`Registry.bind()` maps directly to the native bind lifecycle and replaces legacy
`setEndpoints()` / `start()`.
`Registry.setSockOpt()` and `SpotNode.setDiscovery()` are not part of the
aligned canonical surface.

## Verification

```bash
cd bindings/node && npm run build
cd bindings/node && node-gyp rebuild
cd bindings/node && npm test
cd bindings/node && npm run samples
cd bindings/node && npm run perf:single -- --recv recv --warmup 0.2 --duration 0.5
cd bindings/node && node --test tests/*.test.js
```
