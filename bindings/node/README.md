# Node Bindings

Aligned Node bindings for `libzlink`.

## Canonical Raw API

- `new PairSocket(ctx)`, `new DealerSocket(ctx)`, `new RouterSocket(ctx)`,
  `new StreamSocket(ctx)`
- `new PubSocket(ctx)`, `new XPubSocket(ctx)`
- `new SubSocket(ctx)`, `new XSubSocket(ctx)`
- duplex sockets: `send(...)`, `sendParts(...)`, `recv()`, `recvInto(buffer)`
- `RouterSocket`: `recv()`, `recvInto(buffer)`, `sendTo(routingId, message)`,
  `sendPartsTo(routingId, parts)`
- send sockets: `send(...)`, `sendParts(...)`
- subscriber sockets: `subscribe(...)`, `unsubscribe(...)`, `recv()`

`Message.copyOf()` copies payload ownership into the message.
`Message.wrap()` keeps the caller-owned buffer as the payload source.
Canonical raw sockets intentionally hide opposite-direction methods, so
`PubSocket` does not expose `recv()` and `SubSocket` does not expose `send()`.
`RouterSocket` also does not expose generic `send(...)`; replies must include a
routing ID. `StreamSocket` also does not expose active stream helpers on the
canonical path.

## Compatibility Socket

- `new Socket(ctx, SocketType.X)` remains as a deprecated compatibility path
- legacy `recv(size, flags)` and `streamAttach` / `streamDetach` /
  `streamPeerRoutingId` / `streamSend` stay on compat `Socket` only

Not yet part of the canonical raw surface:
raw socket TLS convenience helpers, raw publish(topic, payload), raw socket
unbind/disconnect helpers.

## Service Surface

- `new Discovery(ctx, serviceType, serviceName)`
- `new Registry(ctx)` + `registry.bind(pubEndpoint, routerEndpoint)`
- `new RegistryQueryClient(ctx)`
- `new Spot(ctx)`
- `new SpotNode(ctx)`

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
cd bindings/node && node --test tests/*.test.js
```
