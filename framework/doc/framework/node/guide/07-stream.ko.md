# STREAM — 외부 client 연결

stream 은 외부 client 와 장기 연결을 유지하는 표면이다. framework server 쪽은 session
lifecycle 을 받고, 브라우저 client 쪽은 `@zlink-systems/stream-connector` 를 사용한다.

server application 은 stream socket, frame 길이, header codec, payload codec 을 직접
다루지 않는다. server 는 `ZLinkModule` 에 stream node 와 session 을 등록하고,
framework 가 connection accept, frame decode, session dispatch, reply frame 작성을
맡는다. `@zlink-systems/stream-connector` 는 외부 client 코드에서 server 에
접속할 때 사용하는 패키지다.

## 1. server session

session 은 하나의 `onDispatch(dispatch, payload)` 로 packet 을 받는다. `payload` 는
binding `Message`나 `Buffer`가 아니라 framework runtime이 codec registry와 함께
감싸서 넘기는 `ZLinkMessage`다. session은 필요한 packet만 `payload.decode<T>()`로
DTO를 얻고, actor relay처럼 decode를 미룰 수 있는 경계에는 그대로 넘긴다.

```ts
class Pong {
}

export class GameSession {
  constructor(readonly context: ZLinkSessionContext) {}

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage) {
    await this.context.client
      .reply(new Pong())
      .submit();
  }
}
```

NestJS server 에서는 stream endpoint 와 session type 만 등록한다.

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .addStreamNode('game.stream')
      .bind('ws://127.0.0.1:9000')
      .registerSession(GameSession)
    .build()
);
```

위 코드가 server 쪽의 STREAM 구성이다. application 이 `net.createServer(...)` 나
`ZlinkStreamFrameCodec` 을 호출해야 한다면 framework 표면을 우회하고 있는 것이다.

session 이 NestJS provider 로 등록된 repository, handler, channel client 같은 객체를
사용해야 하면 session class 를 직접 provider 로 만들지 않고 factory 를 등록한다.
factory 는 필요한 provider 를 주입받고, 연결이 생길 때마다 `create(context)` 에서
session 을 만든다.

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .addStreamNode('game.stream')
      .bind('ws://127.0.0.1:9000')
      .registerSession(GameSessionFactory)
    .build()
);
```

이때 NestJS `providers` 에는 `GameSessionFactory` 와 factory 가 주입받는 application
service 만 등록한다. stream socket, frame codec, session token alias 같은 framework
배선은 application provider 로 등록하지 않는다.

stream session 이 player actor 를 사용해야 하면 actor 생성과 보관은 framework actor
manager 에 맡긴다. session 안에서 `Map<string, Actor>` 같은 저장소를 만들고 모든
연결을 순회해 push 를 flush 하면 framework 의 actor/session 경계를 다시 application
으로 끌어올리는 구조가 된다.

## 2. connector

```ts
class Join {
  constructor(readonly playerId: string) {}
}

const connector = zlinkStreamConnectorFactory.create({
  endpoint: 'ws://127.0.0.1:9000', // 브라우저가 접속할 WebSocket endpoint를 지정한다.
});

await connector.connect();
void connector.send(new Join('p1')).submit();
```

JSON은 connector core의 기본 codec이다. 기본 예시는
이름이 있는 payload 타입을 바로 넘기는 경로를 기준으로 둔다. plain object literal 같은
구조적 payload를 보내면 connector가 packet 이름을 자동으로 얻을 수 없을 수 있으므로,
그 경우에만 `.packetName(...)` 또는 `messageType` 인자를 명시한다.

MessagePack이나 Protobuf가 필요하면 connector 전용 패키지가 아니라 framework codec extension
package를 참조한다. 단, 현재 Node server STREAM runtime은 dispatch/send/reply 경로에서 JSON
frame을 만든다. MessagePack/Protobuf codec 공유는 connector 측 표면에 적용되며, server stream
codec 적용은 추후 범위다.

connector도 framework처럼 **custom codec**을 끼울 수 있다. 사용자 codec은 framework
extension과 `ZlinkStreamPayloadCodec`(`encode`/`decode` 구현)을 함께 제공한다. server
framework 쪽 등록(`codecs.use(...)`)과 대칭이며, 두 표면의 전체 목록은
[framework-api §2.2](../../spec/05-framework-api.ko.md) 표를 본다.

```ts
const avroStreamCodec = {
  encode(payload) { return { name: 'PlaceOrder', codec: 'raw', payload: orderType.toBuffer(payload) }; },
  decode(encoded) { return orderType.fromBuffer(encoded.payload); }
};
const connector = zlinkStreamConnectorFactory.create({ endpoint, codec: avroStreamCodec });
```

server push 를 한 번 기다릴 때는 connector의 `waitFor(...)` builder를 사용한다.
connector에 JSON codec을 설정해 두면 기다린 message의 payload도 같은 codec으로
decode된다.

```ts
const joined = await connector
  .waitFor<PlayerJoinedNotify>('PlayerJoinedNotify')
  .where((message) => message.payload.actorId === 'player-2')
  .submit();
```

샘플 client는 connector wait builder로 push를 기다리고, 받은 payload의 actor id, room id, state 같은
값을 직접 확인한다.

## 회귀 테스트

stream frame, session lifecycle, connector codec 과 heartbeat 는
`test/contract/stream-runtime.test.js`, `stream-session-runtime.test.js`,
`stream-connector*.test.js` 에서 확인한다.
