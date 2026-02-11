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
const server = ctx.socket(zlink.PAIR);
server.bind('tcp://*:5555');

const client = ctx.socket(zlink.PAIR);
client.connect('tcp://127.0.0.1:5555');

client.send(Buffer.from('Hello'));

const reply = server.recv();
console.log(reply.toString());

client.close();
server.close();
ctx.close();
```

## 4. TypeScript

```typescript
import { Context, PAIR } from 'zlink';

const ctx = new Context();
const socket = ctx.socket(PAIR);
```

Type definitions: `src/index.d.ts`

## 5. Discovery/Gateway/Spot

```javascript
const discovery = new zlink.Discovery(ctx);
discovery.connectRegistry('tcp://registry:5550');
discovery.subscribe('payment-service');

const gateway = new zlink.Gateway(ctx, discovery);
```

## 6. Prebuilds

Platform-specific binaries in the `prebuilds/` directory:
- `linux-x64/`, `linux-arm64/`
- `darwin-x64/`, `darwin-arm64/`
- `win32-x64/`

## 7. Testing

```bash
cd bindings/node && npm test
```

Uses the node:test framework.
