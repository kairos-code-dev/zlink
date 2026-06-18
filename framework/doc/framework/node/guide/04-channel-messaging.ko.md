# Channel Messaging — request, send, publish

channel messaging 은 서버 간 호출을 다룬다. request/reply 는 응답이 필요할 때,
send 는 one-way 명령에, publish 는 여러 subscriber 로 이벤트를 보낼 때 사용한다.

## 1. request

```ts
import { ZLinkPacket } from '@zlink-systems/framework';

@ZLinkPacket('GetProfile')
class GetProfileReq {
  constructor(readonly userId: string) {}
}

await client
  .requestToChannel('profile', new GetProfileReq('u1'))
  .submit();
```

`timeout(...)` 은 reply 대기 시간을 뜻한다. submit 지연과 reply timeout 은 별도
정책으로 다룬다.

## 2. send

```ts
import { ZLinkPacket } from '@zlink-systems/framework';

@ZLinkPacket('WarmProfile')
class WarmProfileCmd {
  constructor(readonly userId: string) {}
}

await client
  .sendToChannel('profile', new WarmProfileCmd('u1'))
  .submit();
```

## 3. publish

```ts
import { ZLinkPacket } from '@zlink-systems/framework';

@ZLinkPacket('ProfileChanged')
class ProfileChangedEvent {
  constructor(readonly userId: string) {}
}

await fanout
  .publish('profile.changed', new ProfileChangedEvent('u1'))
  .submit();
```

guide의 기본 예시는 이름이 있는 class payload를 기준으로 둔다. plain object literal이나
구조적 타입 payload를 바로 넘기는 것은 예외 경로다. 이 경우 framework가 packet 이름을
자동으로 얻을 수 없으면 `.packetName(...)` override가 필요하다.

```ts
interface WarmProfilePayload {
  readonly userId: string;
}

await client
  .sendToChannel('profile', { userId: 'u1' } satisfies WarmProfilePayload)
  .submit();
```

## 4. handler 노출

handler 를 scan 했다고 자동으로 노출하지 않는다. 등록 정책이 선택한 handler group
또는 직접 등록한 handler 만 dispatch 대상이 된다.

자동 등록은 기본으로 권장하는 방식이다. handler class 에
`@zlinkRequestHandler(...)`, `@zlinkSendHandler(...)`,
`@zlinkPublishHandler(...)` 를 붙이고, module provider 에
`zlinkDiscoverProviders(...)` 결과를 넣은 뒤 channel builder 에
`addHandlerGroup(...)` 을 지정한다. Bingo.Ts 샘플은 이 방식을 사용해서 handler 파일을
추가하는 흐름을 보여 준다.

수동 등록도 가능하다. handler class 를 module `providers` 에 직접 넣고, channel
builder 에서 사용할 packet 만 명시한다. TicTacToe.Ts 샘플은 이 방식을 사용해서
노출되는 handler 를 구성 코드에서 바로 확인할 수 있게 한다.

```ts
zlinkFramework()
  .addClientServerChannel('play')
    .enableServer(playEndpoint)
    .addRequestHandler('CreateGame', CreateGameHandler)
    .addSendHandler('PlayerLeft', PlayerLeftHandler)
  .addFanoutChannel('events')
    .enableSubscriber(eventsEndpoint)
    .addPublishHandler('RoomChanged', RoomChangedHandler)
  .addRouteMeshChannel('route')
    .addSendHandler('ActorLeft', ActorLeftRouteHandler)
    .addRequestHandler('ActorLookup', ActorLookupRouteHandler);
```

## 5. 커스텀 codec (Avro 예시)

기본 제공 codec(JSON/Protobuf/MessagePack) 외의 직렬화 포맷이 필요하면 codec registry에
custom serializer를 등록한다. serializer는 업무 객체 ↔ `Message`(byte payload) 변환만
담당하고, packet name 결정과 codec 선택은 그대로 framework가 처리한다. 아래는 Avro
(`avsc`)를 끼우는 예시다.

```ts
import avro from 'avsc';

const orderType = avro.Type.forSchema({
  type: 'record',
  name: 'PlaceOrder',
  fields: [{ name: 'sku', type: 'string' }, { name: 'qty', type: 'int' }]
});

const avroSerializer = {
  serialize(value) {
    return zlink.Message.from(orderType.toBuffer(value));
  },
  deserialize(message) {
    return orderType.fromBuffer(Buffer.from(message.data()));
  }
};

const registration = framework.createFrameworkRegistration({
  codecs: { serializers: [{ contentType: 'application/avro', serializer: avroSerializer }] },
  channels: { orders: { server: { bind: endpoint } } }
});
```

등록 후에는 high-level 호출이 그대로 업무 객체를 주고받고, 직렬화는 Avro로 처리된다.

```ts
await client.requestToChannel('orders', { sku: 'A-1', qty: 3 }).packetName('PlaceOrder').submit();
```

framework당 custom serializer는 하나만 둔다(둘 이상이면 모호성 구성 오류). 다른 언어의
등록 표면은 [framework-api §2.2](../../common/spec/framework-api.ko.md)의 표를 본다.

## 회귀 테스트

channel request/reply, send, publish, handler 노출 규칙은
`test/contract/channel-client.test.js` 와 `test/contract/handler-runtime.test.js` 에서
확인한다. custom serializer 라운드트립은 같은 파일의
`ZLinkFrameworkRuntimeHost uses channel serializer registry for typed request replies`
테스트가 확인한다.
