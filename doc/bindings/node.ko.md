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
