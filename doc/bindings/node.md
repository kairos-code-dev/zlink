[English](node.md) | [한국어](node.ko.md)

# Node.js Binding

## 1. Overview

- **N-API** C++ addon
- Prebuilds: Pre-built binaries provided per platform
- Includes TypeScript type definitions

## 2. Installation

```bash
npm install zlink
```

The appropriate prebuild is automatically selected. Platforms without a prebuild fall back to node-gyp build.

## 3. Basic Example

```javascript
const zlink = require('zlink');

const ctx = new zlink.Context();
const server = new zlink.Socket(ctx, zlink.SocketType.PAIR);
server.bind('tcp://*:5555');

const client = new zlink.Socket(ctx, zlink.SocketType.PAIR);
client.connect('tcp://127.0.0.1:5555');

client.send(zlink.Message.copyOf('Hello'));

const received = server.recv();
console.log(received.parts[0].toString());

client.close();
server.close();
ctx.close();
```

## 4. TypeScript

```typescript
import { Context, Message, Socket, SocketType } from 'zlink';

const ctx = new Context();
const socket = new Socket(ctx, SocketType.PAIR);
socket.send(Message.copyOf('ping'));
```

Type definitions: `src/index.d.ts`

## 5. Canonical Raw Socket Surface

```javascript
const received = socket.recv();
for (const part of received.parts) {
  console.log(part);
}

const scratch = Buffer.alloc(1024);
const bytes = socket.recvInto(scratch);
console.log(bytes);
```

`Socket` canonical methods:
- `send(message, flags?)`
- `sendParts(parts, flags?)`
- `recv(flags?)`
- `recvInto(buffer, flags?)`

`Message.copyOf()` and `Message.wrap()` make the copy/borrow boundary explicit.

## 6. Service Surface

Aligned service entry points:
- `new Discovery(ctx, serviceType, serviceName)`
- `Registry.bind(pubEndpoint, routerEndpoint)`
- `new RegistryQueryClient(ctx)`
- `new Spot(ctx)`
- `new SpotNode(ctx)`

`Receiver` is removed from the aligned public API.
`Discovery` requires a non-empty `serviceName`.
`Registry.bind()` maps directly to the native bind lifecycle and replaces legacy
`setEndpoints()` / `start()`.
`Registry.setSockOpt()` and `SpotNode.setDiscovery()` are compatibility leftovers
that are intentionally rejected on the aligned public API.

## 7. Prebuilds

Platform-specific binaries in the `prebuilds/` directory:
- `linux-x64/`, `linux-arm64/`
- `darwin-x64/`, `darwin-arm64/`
- `win32-x64/`

## 8. Testing

```bash
cd bindings/node && npm test
cd bindings/node && node --test tests/*.test.js
```

Uses the node:test framework.

## 9. STREAM Callback API

`Socket` STREAM helpers:
- `streamAttach((routingId, packets) => { ... }, mode?)`
- `streamDetach()`
- `streamPeerRoutingId(index?)`
- `streamSend(routingId, payload, flags?)`
- Legacy `streamEchoStart/Stop` and `streamSinkStart/Stop` APIs were removed.
- The aligned surface keeps these names, but the native lifecycle rework is still
  in progress and unsupported paths currently throw.
- `streamDetach()` is a safe no-op cleanup boundary even when `streamAttach()`
  throws.
