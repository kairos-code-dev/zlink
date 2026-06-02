# Actor와 Session Relay

actor 는 사용자나 플레이어 같은 논리 객체다. stream session 과 actor 를 bind 하면
server 쪽 actor 가 client stream 으로 push 하거나, stream packet 을 actor handler 로
relay 할 수 있다.

## 1. actor context

actor 는 `context.boundSession` 으로 현재 session 에 메시지를 보낼 수 있다.

```ts
await actor.context.boundSession
  .send({ type: 'GameStarted' })
  .packetName('GameStarted')
  .submit();
```

session 에 아직 bind 되지 않은 actor 에서 호출하면 retriable framework error 로
실패한다.

## 2. session bind

stream session 은 `context.actors.bind(actor)` 로 actor 를 session 에 연결한다.
binding 은 native ActorGateway 를 통해 등록되고, 실패하면 local binding 도 남기지
않는다.

## 3. ordering

actor별 mailbox 는 같은 actor packet 순서를 보장한다. join 이후 대기 중이던 packet 은
이전 위치가 아니라 현재 actor 위치를 다시 확인해 dispatch 된다.

## 회귀 테스트

actor manager, mailbox, session relay, bound session 동작은
`test/contract/actor-manager.test.js`, `stream-runtime.test.js`,
`stream-session-runtime.test.js` 에서 확인한다.
