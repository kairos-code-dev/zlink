# Channel Messaging — request, send, publish

channel messaging 은 서버 간 호출을 다룬다. request/reply 는 응답이 필요할 때,
send 는 one-way 명령에, publish 는 여러 subscriber 로 이벤트를 보낼 때 사용한다.

## 1. request

```ts
await client
  .requestToChannel('profile', { userId: 'u1' })
  .packetName('GetProfile')
  .timeout(1000)
  .submit();
```

`timeout(...)` 은 reply 대기 시간을 뜻한다. submit 지연과 reply timeout 은 별도
정책으로 다룬다.

## 2. send

```ts
await client
  .sendToChannel('profile', { userId: 'u1' })
  .packetName('WarmProfile')
  .submit();
```

## 3. publish

```ts
await fanout
  .publish('profile.changed', { userId: 'u1' })
  .packetName('ProfileChanged')
  .submit();
```

## 4. handler 노출

handler 를 scan 했다고 자동으로 노출하지 않는다. 등록 정책이 선택한 handler group
또는 직접 등록한 handler 만 dispatch 대상이 된다.

자동 등록은 기본으로 권장하는 방식이다. handler class 에
`@zlinkRequestHandler(...)`, `@zlinkSendHandler(...)`,
`@zlinkPublishHandler(...)` 를 붙이고, module provider 에
`zlinkDiscoverProviders(...)` 결과를 넣은 뒤 channel builder 에
`handlerGroup(...)` 을 지정한다. Bingo.Ts 샘플은 이 방식을 사용해서 handler 파일을
추가하는 흐름을 보여 준다.

수동 등록도 가능하다. handler class 를 module `providers` 에 직접 넣고, channel
builder 에서 사용할 packet 만 명시한다. TicTacToe.Ts 샘플은 이 방식을 사용해서
노출되는 handler 를 구성 코드에서 바로 확인할 수 있게 한다.

```ts
zlinkFramework()
  .clientServerChannel('play', (channel) => channel
    .server(playEndpoint)
    .requestHandler('CreateGame', CreateGameHandler)
    .sendHandler('PlayerLeft', PlayerLeftHandler))
  .fanoutChannel('events', (channel) => channel
    .subscriber(eventsEndpoint)
    .publishHandler('RoomChanged', RoomChangedHandler))
  .routerMesh('route', (mesh) => mesh
    .sendHandler('ActorLeft', ActorLeftRouteHandler)
    .requestHandler('ActorLookup', ActorLookupRouteHandler));
```

## 회귀 테스트

channel request/reply, send, publish, handler 노출 규칙은
`test/contract/channel-client.test.js` 와 `test/contract/handler-runtime.test.js` 에서
확인한다.
