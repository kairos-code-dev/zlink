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
  .requestToChannel('services', 'profile', new GetProfileReq('u1'))
  .submit();
```

`timeout(...)` 은 reply 대기 시간을 뜻한다. 호출별 timeout이 있으면 그 값을 쓰고,
없으면 channel builder의 `setDefaultRequestTimeout(...)`, 마지막으로 framework 전역
`requestTimeoutMs` 값을 사용한다. 전역 기본값은 30000ms(30초)다. submit 지연과
reply timeout 은 별도 정책으로 다룬다.

## 2. send

```ts
import { ZLinkPacket } from '@zlink-systems/framework';

@ZLinkPacket('WarmProfile')
class WarmProfileCmd {
  constructor(readonly userId: string) {}
}

await client
  .sendToChannel('services', 'profile', new WarmProfileCmd('u1'))
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
  .publish('profile-events', 'profile.changed', new ProfileChangedEvent('u1'))
  .submit();
```

channel 호출에는 이름이 있는 class payload를 사용한다. framework는 타입에 직접 선언한
`@ZLinkPacket` metadata 또는 생성자 이름으로 packet을 식별하며, 호출별 이름 변경 표면을
제공하지 않는다. 구조적 타입이 필요하면 명시적인 class로 정의한다.

```ts
@ZLinkPacket('WarmProfile')
class WarmProfilePayload {
  constructor(readonly userId: string) {}
}

client.sendToChannel('services', 'profile', new WarmProfilePayload('u1')).submit();
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
const framework = zlinkFramework();
const mesh = framework.addRouteMesh('game')
  .listen(playEndpoint)
  .routingId(playNodeRid);

mesh.channelName('play')
  .addRequestHandler(CreateGameHandler)
  .addSendHandler(PlayerLeftHandler);
mesh.addRouteSendHandler(ActorLeftRouteHandler);
mesh.addRouteRequestHandler(ActorLookupRouteHandler);

framework.addFanoutChannel('events')
  .enableSubscriber(eventsEndpoint);
```

## 5. Route mesh 호출

RID direct 호출은 target node `RoutingId`를 application이 직접 알고 있을 때만 쓴다.
`ZLinkRouteClient`는 특정 MeshNode 하나에 묶인 client가 아니며, 호출할 때 MeshName과
target `RoutingId`를 함께 받는다.

```ts
const target = 'play-node-1';

const allocated = await routeClient
  .requestToNode('game', target, new AllocateRoom('alice'))
  .submit();
```

같은 route channel 로 반복 호출하면 application 코드에서 작은 wrapper 를 만들어도 된다.
이 wrapper 는 framework API 가 아니라 application 이 정한 이름이다. 그래서 업무 코드는
매번 channel 문자열을 반복하지 않고, wrapper 내부에서 어떤 route channel 로 나가는지만
한 곳에 둔다.

```ts
class PlayRoutes {
  constructor(private readonly routes: ZLinkRouteClient) {}

  request(request: AllocateRoom, targetNodeRid: RoutingId): ZLinkRequestCall {
    return this.routes.requestToNode('game', targetNodeRid, request);
  }
}
```

## 6. 커스텀 codec (Avro 예시)

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
@ZLinkPacket('PlaceOrder')
class PlaceOrderRequest {
  constructor(readonly sku: string, readonly qty: number) {}
}

await client
  .requestToChannel('services', 'orders', new PlaceOrderRequest('A-1', 3))
  .submit();
```

framework당 custom serializer는 하나만 둔다(둘 이상이면 모호성 구성 오류). 다른 언어의
등록 표면은 [framework-api §9](../../spec/05-framework-api.ko.md#9-codec)의 표를 본다.

## 회귀 테스트

channel request/reply, send, publish, handler 노출 규칙은
`test/contract/channel-client.test.js` 와 `test/contract/handler-runtime.test.js` 에서
확인한다. custom serializer 라운드트립은 같은 파일의
`ZLinkFrameworkRuntimeHost uses channel serializer registry for typed request replies`
테스트가 확인한다.
