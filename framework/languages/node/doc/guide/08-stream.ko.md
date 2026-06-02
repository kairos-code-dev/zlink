# STREAM — 외부 client 연결

stream 은 외부 client 와 장기 연결을 유지하는 표면이다. framework server 쪽은 session
lifecycle 을 받고, client 쪽은 `@zlink-systems/stream-connector` 를 사용한다.

## 1. server session

session 은 하나의 `onDispatch(header, payload)` 로 packet 을 받는다.

```ts
export class GameSession {
  constructor(readonly context: ZLinkSessionContext) {}

  async onDispatch(header: ZlinkStreamHeader, payload: Message) {
    await this.context.client
      .reply({ ok: true })
      .submit();
  }
}
```

## 2. connector

```ts
const connector = zlinkStreamConnectorFactory.create({
  endpoint: 'tcp://127.0.0.1:9000',
});

await connector.connect();
await connector.send('Join', { playerId: 'p1' }).submit();
```

json, messagepack, protobuf helper 는 connector 전용 패키지에서 제공한다.

## 회귀 테스트

stream frame, session lifecycle, connector codec 과 heartbeat 는
`test/contract/stream-runtime.test.js`, `stream-session-runtime.test.js`,
`stream-connector*.test.js` 에서 확인한다.
