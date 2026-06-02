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

## 회귀 테스트

channel request/reply, send, publish, handler 노출 규칙은
`test/contract/channel-client.test.js` 와 `test/contract/handler-runtime.test.js` 에서
확인한다.
