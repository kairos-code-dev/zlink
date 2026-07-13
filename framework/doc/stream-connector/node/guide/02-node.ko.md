# 02 — Node

[← 목차](INDEX.ko.md) | [이전: 개요](01-overview.ko.md)

---

Node 런타임에서 connector를 사용하는 방법이다. 서버 E2E 테스트, 운영 도구, 부하 생성 봇이
주요 사용처다.

> 브라우저에서는 package의 `/browser` 진입점을 사용한다.
> [03 — 브라우저](03-browser.ko.md)를 본다.

## 설치

```bash
npm install @zlink-systems/stream-connector
```

## 연결

```ts
import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode
} from '@zlink-systems/stream-connector';

const client = zlinkStreamConnectorFactory.create({
  endpoint: 'tcp://game.example.com:7000',
  codec: zlinkStreamJsonCodec,
  dispatchMode: ZlinkStreamDispatchMode.Immediate,
  waitTimeoutMs: 10_000
});

await client.connect();
```

Node에서는 `tcp`·`tls`·`ws`·`wss`를 모두 사용한다. endpoint scheme이 transport를 결정한다.

## dispatch mode

| 모드 | 동작 |
|------|------|
| `Immediate` | receive 경로에서 handler를 직접 실행한다. **Node에서는 대개 이쪽이다** |
| `Manual`(기본) | handler를 내부 queue에 넣는다. 사용자가 명시적으로 펌프한다 |

기본값이 `Manual`인 이유는 게임 엔진의 main thread 제약 때문이다. Node에는 그 제약이 없으므로
E2E 테스트와 도구는 `Immediate`를 명시한다.

## request / reply

```ts
const auth = await client
  .request({ actorId, displayName: actorId, nodeRid: 'play-a' })
  .packetName('AuthReq')
  .timeout(5_000)
  .submit<AuthRes>();
```

- `packetName(...)`을 생략하면 payload 타입 이름이 기본 packet 이름이 된다.
- `timeout(...)`을 생략하면 connector option의 `waitTimeoutMs`를 사용한다.
- 서버가 error kind로 응답하면 `submit()`이 실패한다.

## 서버 push 수신

핸들러를 등록하는 방식과, 특정 packet 하나를 기다리는 방식이 있다.

```ts
client.on<GameUpdate>('GameUpdate', (message) => {
  console.log(message.payload.tick);
});
```

E2E 테스트처럼 **정해진 push 하나가 도착하는지**를 검증할 때는 `waitFor(...)`를 쓴다. 요청을
보내기 **전에** 먼저 대기를 걸어야 push를 놓치지 않는다.

```ts
const pushed = client.waitFor<ActorPushNotify>('ActorPushNotify')
  .timeout(10_000)
  .submit();

await triggerServerSidePush();

const notify = await pushed;
```

## 연결 종료

```ts
await client.close();
```

`close()` 후에는 같은 connector 객체를 다시 연결하지 않는다. 새 연결이 필요하면 factory로 새
connector를 만든다.

연결이 끊긴 이유는 connector의 `closeReason` 속성으로 확인한다. disconnect handler 안에서 읽으면
방금 끊긴 이유를 알 수 있다.

```ts
client.onDisconnected(() => {
  if (client.closeReason === 'ServerDrain') {
    // 서버가 우아한 종료로 세션을 닫았다. 백오프 후 다른 노드로 재접속한다.
  }
});
```

값의 정본은
[Node 공개 계약 §5](../../../framework/common/spec/languages/node/03-stream-connector.ko.md)다.

## 그다음

- 옵션·codec·inbound observer·오류 코드의 정확한 표면:
  [Node 공개 계약](../../../framework/common/spec/languages/node/03-stream-connector.ko.md)
- wire 계약과 연결 생명주기의 정본:
  [Stream Connector 공통 스펙](../../../framework/common/spec/32-stream-connector.ko.md)
