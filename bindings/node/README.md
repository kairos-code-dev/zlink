# Node Bindings

Aligned Node bindings for `libzlink`.

## Canonical Raw API

- `new Socket(ctx, SocketType.PAIR)`
- `socket.send(Message.copyOf(data))`
- `socket.sendParts([Message.copyOf(a), Message.wrap(b)])`
- `socket.recv()` -> `Received`
- `socket.recvInto(buffer)`

`Message.copyOf()` copies payload ownership into the message.
`Message.wrap()` keeps the caller-owned buffer as the payload source.

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
cd bindings/node && node-gyp rebuild
cd bindings/node && npm test
cd bindings/node && node --test tests/*.test.js
```
