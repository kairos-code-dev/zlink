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

타입 정의: `src/index.d.ts`

## 5. Discovery/Gateway/Spot

```javascript
const discovery = new zlink.Discovery(ctx);
discovery.connectRegistry('tcp://registry:5550');
discovery.subscribe('payment-service');

const gateway = new zlink.Gateway(ctx, discovery);
```

## 6. Prebuilds

`prebuilds/` 디렉토리에 플랫폼별 바이너리:
- `linux-x64/`, `linux-arm64/`
- `darwin-x64/`, `darwin-arm64/`
- `win32-x64/`

## 7. 테스트

```bash
cd bindings/node && npm test
```

node:test 프레임워크 사용.

## 8. STREAM 콜백 API

`Socket` STREAM 헬퍼:
- `streamAttach((routingId, packets) => { ... }, mode?)`
- `streamDetach()`
- `streamPeerRoutingId(index?)`
- `streamSend(routingId, payload, flags?)`
- 기존 `streamEchoStart/Stop`, `streamSinkStart/Stop` API는 제거되었습니다.

`mode`는 `StreamDispatchMode.NONE` 또는 `StreamDispatchMode.LEN32BE`를 사용합니다.

모드 규칙:
- attach 상태에서는 콜백에서 STREAM 페이로드를 소비합니다.
- attach 상태에서 STREAM 페이로드 수신에 `recv()`를 혼용하지 않습니다.
- `streamDetach()` 이후에는 기존 `recv()` 경로를 다시 사용할 수 있습니다.

```javascript
stream.streamAttach((routingId, packets) => {
  for (const packet of packets) {
    const copy = Buffer.from(packet);   // echo 전 명시적 복사
    stream.streamSend(routingId, copy, zlink.SendFlag.NONE);
  }
  return 0;
}, zlink.StreamDispatchMode.LEN32BE);
```
