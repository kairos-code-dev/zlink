[English](node.md) | [한국어](node.ko.md)

# Node.js 바인딩

## 1. 개요

- **N-API** C++ addon
- Prebuilds: 플랫폼별 사전 빌드 바이너리 제공
- TypeScript 타입 정의 포함

## 2. 설치

```bash
npm install zlink
```

prebuild가 자동 선택됨. 없는 플랫폼은 node-gyp 빌드.

## 3. 기본 예제

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

타입 정의: `src/index.d.ts`

## 5. canonical raw socket surface

```javascript
const received = socket.recv();
for (const part of received.parts) {
  console.log(part);
}

const scratch = Buffer.alloc(1024);
const bytes = socket.recvInto(scratch);
console.log(bytes);
```

`Socket` canonical 메서드:
- `send(message, flags?)`
- `sendParts(parts, flags?)`
- `recv(flags?)`
- `recvInto(buffer, flags?)`

`Message.copyOf()` / `Message.wrap()`로 copy/borrow 경계를 명시합니다.

## 6. service surface

정렬된 service 진입점:
- `new Discovery(ctx, serviceType, serviceName)`
- `Registry.bind(pubEndpoint, routerEndpoint)`
- `new RegistryQueryClient(ctx)`
- `new SpotNode(ctx)`
- `new Spot(node)`

`Receiver`는 aligned public API에서 제거되었습니다.
`Discovery`는 비어 있지 않은 `serviceName`이 필요합니다.
`Registry.bind()`가 native bind lifecycle에 직접 매핑되며 기존
`setEndpoints()` / `start()`를 대체합니다.
`Registry.setSockOpt()`와 `SpotNode.setDiscovery()`는 aligned canonical surface에
포함되지 않으며 호환 경계에서 명시적으로 거부됩니다.

## 7. Prebuilds

`prebuilds/` 디렉토리에 플랫폼별 바이너리:
- `linux-x64/`, `linux-arm64/`
- `darwin-x64/`, `darwin-arm64/`
- `win32-x64/`

## 8. 테스트

```bash
cd bindings/node && npm test
cd bindings/node && node --test tests/*.test.js
```

node:test 프레임워크 사용.

## 9. STREAM 콜백 API

`Socket` STREAM 헬퍼:
- `streamAttach((routingId, packets) => { ... }, mode?)`
- `streamDetach()`
- `streamPeerRoutingId(index?)`
- `streamSend(routingId, payload, flags?)`
- 기존 `streamEchoStart/Stop`, `streamSinkStart/Stop` API는 제거되었습니다.
- aligned surface에서는 이름만 유지하며, native lifecycle 재정렬이 끝나기 전까지는
  unsupported 경로에서 예외를 던질 수 있습니다.
- `streamDetach()`는 `streamAttach()`가 예외를 던진 뒤에도 안전한 no-op cleanup
  경계로 유지됩니다.
