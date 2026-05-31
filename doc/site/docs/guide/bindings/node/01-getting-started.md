[Node.js 가이드](./index.md) · [다음: 메시징 →](./02-messaging.md)

# 시작하기

## 설치

```bash
npm install @zlink-systems/zlink
```

- **Node.js 18** 이상.
- 네이티브 코어가 플랫폼별 prebuild로 번들됩니다.

```javascript
const zlink = require('@zlink-systems/zlink');
// 또는 ESM / TypeScript
import * as zlink from '@zlink-systems/zlink';
```

---

## 5분 예제 — PING/ACK

```javascript
const zlink = require('@zlink-systems/zlink');

// 서버
const ctx = zlink.createContext();
const server = zlink.createPairSocket(ctx);
server.bind('tcp://127.0.0.1:5555');

const received = new zlink.Received();
server.recv(received);
console.log(received.parts[0].data().toString()); // PING
received.close();

server.send().message(Buffer.from('ACK')).submit();

server.close();
ctx.close();
```

```javascript
// 클라이언트
const ctx = zlink.createContext();
const client = zlink.createPairSocket(ctx);
client.connect('tcp://127.0.0.1:5555');

client.send().message(Buffer.from('PING')).submit();

const received = new zlink.Received();
client.recv(received);
console.log(received.parts[0].data().toString()); // ACK
received.close();

client.close();
ctx.close();
```

---

## 핵심 타입

### 컨텍스트

```javascript
const ctx = zlink.createContext();
// 사용 후 반드시 close — 하위 소켓의 블로킹 작업이 중단됩니다
ctx.close();
```

### 메시지

Node 바인딩은 `Buffer`를 메시지로 직접 사용합니다. `message()` 호출 시 복사본을
만들므로 원본 Buffer를 자유롭게 재사용할 수 있습니다.

```javascript
socket.send().message(Buffer.from('hello')).submit();
socket.send().message(Buffer.from([0x01, 0x02])).submit();

// 수신 후 페이로드 접근
const received = new zlink.Received();
socket.recv(received);
const data = received.parts[0].data();   // Buffer
const text = data.toString('utf8');
received.close();
```

### Received — 수신 봉투

```javascript
const received = new zlink.Received();
socket.recv(received);                  // 동기 블로킹
try {
  const parts = received.parts;         // Message[]
  const rid = received.routingId;       // RoutingId 또는 undefined
  const seq = received.requestSeq;      // bigint 또는 undefined
} finally {
  received.close();
}
```

### 라우팅 ID

```javascript
const rid = zlink.RoutingId.from(Buffer.from('server-01'));
socket.setRoutingId(rid);
```

---

## 소유권 규칙

| 상황 | 규칙 |
|------|------|
| `submit()` 성공 | 전달된 Buffer는 내부 복사되므로 원본 재사용 가능 |
| `recv()` 성공 | `received.close()` 필수 (finally 블록 권장) |
| `submitAsync()` 완료 | 회신 파트 배열을 각각 `part.close()` |
| `ctx.close()` | 하위 소켓의 블로킹 작업 중단 |

```javascript
const received = new zlink.Received();
socket.recv(received);
try {
  // 파트 처리
} finally {
  received.close();
}
```

---

## 비동기 패턴

요청/응답은 `Promise` 기반입니다.

```javascript
const reply = await dealer.request()
  .message(Buffer.from('ping'))
  .timeout(2000)   // 밀리초
  .submitAsync();
try {
  console.log(reply[0].data().toString());
} finally {
  for (const part of reply) part.close();
}
```
